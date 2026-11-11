// Connect to Server (F16 step 4): an address typed in the location line (Go → Connect to Server…
// opens it on "smb://"; nfs:// too, F16 step 6) is mounted by `gio mount` in a child, on a thread of
// its own that reads gio's questions (fs/connect.c; NFS has none) and answers them from Connect's question (ui/'s UI_PANEL_LOGIN), then
// asks `gio info` for the share's folder. ofx goes there as to any far folder (its lister reads it).
// The window never waits on any of it. gio talks to gvfsd over D-Bus, not through the share's FUSE
// folder, so a killed gio ends at once, and the thread with it (measured on the test Samba). A
// server that doesn't answer fails the mount after gvfs's own 20 s (measured): "Connecting…" until
// then.
// Passwords are asked each time and never stored (Michael, 2026-11-09). One goes to gio on its input,
// as a line: never in its arguments (any process reads those) or a log. Each copy in ofx (the field,
// the thread's) is wiped as it is handed on, and when the question goes unanswered.
// While a connection runs, ofx isn't dumpable: a crash then writes no core file holding a password
// (systemd-coredump keeps them for days), and no process of the same user can read its memory.
// One connection at a time. Included by actions.c.

#include <sys/prctl.h>

enum { CONNECT_INFO_MS = 10000 };   // `gio info` on a share just mounted answers in milliseconds
// At quit, a question up is cancelled as Cancel does it (gio's input closed, an unmount if gvfs
// mounted as a guest anyway), waited for this long; only then is gio killed. A killed gio leaves
// gvfs to mount a guest share all the same (step 4 review, measured). Mount and unmount take about
// 50 ms each on loopback.
enum { CONNECT_QUIT_MS = 1500 };

typedef enum { CN_IDLE, CN_RUN, CN_ASK, CN_DONE } CnPhase;
typedef enum { CN_WAIT, CN_ANSWER, CN_GUEST, CN_CANCEL } CnReply;

static struct {
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    CnPhase phase;
    bool    shown;    // the question for this ask is up (main thread's)
    char    uri[PATH_MAX], host[256], share[256];
    bool    browse;   // a server's address alone: its shares listed (step 7a)
    bool    ssh;      // an sftp:// one: no domain, no guest (step 7c)
    bool    key;      // the question is a key's passphrase (step 7c)
    bool    unknown;  // gvfs asked whether to trust a host it doesn't know: cancelled (step 7c)
    // gio's question (CN_ASK): whether it asks for a user, its default, and whether it asks again
    bool    ask_user, again;
    char    def_user[256];
    // the answer (the main thread's, then the thread's)
    CnReply reply;
    char    user[256], domain[256], pass[256];
    // the result (CN_DONE); a server's address (share ""): its shares (step 7a)
    int     nshares;
    char    shares[SERVER_SHARES_MAX][SERVER_SHARE_NAME];
    bool    ok, cancelled;
    char    path[PATH_MAX], why[512];
    int     pidfd;    // gio's while it runs, -1 none
    bool    quit;
    bool    running;  // the thread is there (connect_join waits for its end)
    int     asked;    // questions put, for the scripts' state line (connasked=)
} cn = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER, .pidfd = -1 };

// "priv on 127.0.0.1", as gvfs names a share; a server's address: the server (step 7a); an SFTP
// one: "user@host" (step 7c).
static const char *cn_name(void)
{
    return cn.share[0] ? arena_sprintf(&frame, "%s on %s", cn.share, cn.host) : arena_sprintf(&frame, "%s", cn.host);
}

// A server's shares go to their panel, gui/shares.c (its state lived here before the code-health
// review's step E): kept from the thread's answer, offered once nothing else is up, dropped when
// another connection starts; UA_SHARE connects to one.
static void shares_keep(const char *server, const char *host, const char (*names)[SERVER_SHARE_NAME], int n);
static int  shares_offer(void);
static void shares_drop(void);
static void share_pick(isize i);

static void cn_line(int fd, const char *s)
{
    size_t n = strlen(s);
    while (n > 0) {
        ssize_t w = write(fd, s, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return;   // (gio gone: its output says why)
        s += w, n -= (size_t)w;
    }
    while (write(fd, "\n", 1) < 0 && errno == EINTR) {}
}

static void cn_wipe(void)   // (cn.mu held)
{
    explicit_bzero(cn.pass, sizeof cn.pass);
}

// gio's output so far, into b (cap, *n), from fd; false at its end. Kept to the last cap/2 bytes when
// full (a question is at the end).
static bool cn_read(int fd, char *b, size_t cap, size_t *n)
{
    if (*n + 512 > cap) {
        memmove(b, b + *n - cap / 2, cap / 2);
        *n = cap / 2;
    }
    ssize_t r = read(fd, b + *n, cap - 1 - *n);
    if (r < 0 && errno == EINTR) return true;
    if (r <= 0) return false;
    *n += (size_t)r, b[*n] = 0;
    return true;
}

// gio's questions answered as they come, until its output and errors end (step 4): each round it
// asks is put to the user (the thread waits for the answer: cn.reply), each line it asks for given;
// Cancel or Guest close its input, and gvfs then tries the share as a guest (measured: a guest share
// mounts) and gio says how that went: a killed gio would leave gvfs to do the same unseen (a Cancel
// that mounted anyway; 2026-11-09). An SFTP host ssh doesn't know is answered Cancel (Michael,
// 2026-11-10). Its errors into eb (*en long). The answer the user gave last.
static CnReply cn_converse(Child *c, bool guest, char *eb, size_t ecap, size_t *en)
{
    static char ob[16384];
    size_t on = 0, handled = 0;
    ob[0] = eb[0] = 0;
    int answered = 0;   // the questions (rounds) answered
    char user[256] = "", domain[256] = "", pass[256] = "";
    CnReply rep = CN_ANSWER;   // (Cancel or Guest: gio's input closed, its end awaited)
    struct pollfd p[2] = { { c->out, POLLIN, 0 }, { c->errs, POLLIN, 0 } };
    while (p[0].fd >= 0 || p[1].fd >= 0) {
        if (poll(p, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (p[1].revents && !cn_read(c->errs, eb, ecap, en)) p[1].fd = -1;
        if (!p[0].revents) continue;
        if (!cn_read(c->out, ob, sizeof ob, &on)) {
            p[0].fd = -1;
            continue;
        }
        char def[256];
        int rounds;
        GioAsk k = gio_ask((Str){ ob, (isize)on }, def, sizeof def, &rounds);
        if (k == GIO_ASK_NONE || on == handled || c->in < 0) continue;
        handled = on;
        if (k == GIO_ASK_CHOICE) {
            pthread_mutex_lock(&cn.mu);
            cn.unknown = true;
            pthread_mutex_unlock(&cn.mu);
            cn_line(c->in, "2");
            close(c->in), c->in = -1;
            continue;
        }
        if (rounds > answered) {   // gio asks (again): the user answers
            pthread_mutex_lock(&cn.mu);
            cn.ask_user = k == GIO_ASK_USER, cn.again = rounds > 1, cn.key = k == GIO_ASK_KEY;
            snprintf(cn.def_user, sizeof cn.def_user, "%s", k == GIO_ASK_USER ? def : "");
            cn.phase = CN_ASK, cn.reply = guest || cn.quit ? CN_CANCEL : CN_WAIT, cn.asked++;
            pthread_mutex_unlock(&cn.mu);
            wake_main();
            pthread_mutex_lock(&cn.mu);
            while (cn.reply == CN_WAIT) pthread_cond_wait(&cn.cv, &cn.mu);
            rep = cn.reply;
            snprintf(user, sizeof user, "%s", cn.user), snprintf(domain, sizeof domain, "%s", cn.domain);
            memcpy(pass, cn.pass, sizeof pass);
            cn_wipe();
            cn.phase = CN_RUN;
            pthread_mutex_unlock(&cn.mu);
            if (rep != CN_ANSWER) {
                // (a test knob: the close that late, so a quit's kill comes first unless quit waits;
                // step 4 confirm)
                static int late_ms = -1;
                if (late_ms < 0) late_ms = env_int("OFX_TEST_CONNECT_CLOSE_MS", 0, 0, 5000);
                if (late_ms) usleep((useconds_t)late_ms * 1000);
                close(c->in), c->in = -1;
                continue;
            }
            answered = rounds;
        }
        cn_line(c->in, k == GIO_ASK_USER ? user : k == GIO_ASK_DOMAIN ? domain : pass);
        if (k == GIO_ASK_PASSWORD || k == GIO_ASK_KEY) explicit_bzero(pass, sizeof pass);
    }
    explicit_bzero(pass, sizeof pass);
    return rep;
}

// One `gio mount` of the address (-a: as a guest), its questions answered as they come
// (cn_converse). False with why when gio says it failed; true when it said nothing, which is not
// yet a mount: gio's exit status can't be read (spawn_init's SA_NOCLDWAIT), and a mount it gave up
// on can end as quietly (Guest on a share that takes none, measured), so cn_thread asks `gio info`
// for its folder. *cancelled when the user said Cancel (a mount gvfs made anyway, as a guest, is
// undone), *as_guest when Guest.
static bool cn_mount(bool guest, char *why, size_t cap, bool *cancelled, bool *as_guest)
{
    char *argv[] = { "gio", "mount", guest ? "-a" : cn.uri, guest ? cn.uri : NULL, NULL };
    Child c;
    Err e;
    if (!child_start(argv, true, true, &c, &e)) return snprintf(why, cap, "%s", e.msg), false;
    pthread_mutex_lock(&cn.mu);
    cn.pidfd = c.pidfd;
    pthread_mutex_unlock(&cn.mu);
    static char eb[4096];
    size_t en = 0;
    CnReply rep = cn_converse(&c, guest, eb, sizeof eb, &en);
    if (c.in >= 0) close(c.in);
    close(c.out), close(c.errs);
    pthread_mutex_lock(&cn.mu);
    cn.pidfd = -1;
    pthread_mutex_unlock(&cn.mu);
    close(c.pidfd);
    bool mounted = !gio_why((Str){ eb, (isize)en }, why, cap) || strcmp(why, "Location is already mounted") == 0;
    if (rep == CN_CANCEL) {   // not connected, as asked: mounted all the same (as a guest), unmounted
        if (mounted) {
            static char got[4096];
            bool late;
            char *u[] = { "gio", "mount", "-u", cn.uri, NULL };
            spawn_capture(NULL, u, got, sizeof got, CONNECT_INFO_MS, &late, &e);
        }
        return *cancelled = true, false;
    }
    *as_guest = rep == CN_GUEST;
    return mounted;
}

// The share's folder, from `gio info`, into path; false with why when it names none (not mounted).
static bool cn_folder(char *path, size_t pcap, char *why, size_t cap)
{
    static char info[8192];
    bool late;
    Err e;
    char *argv[] = { "gio", "info", cn.uri, NULL };
    if (!spawn_capture(NULL, argv, info, sizeof info, CONNECT_INFO_MS, &late, &e)) return snprintf(why, cap, "%s", e.msg), false;
    if (late) return snprintf(why, cap, "gio info gave no answer in %d s", CONNECT_INFO_MS / 1000), false;
    if (!gio_local_path(str_from_c(info), path, pcap)) return snprintf(why, cap, "it isn't mounted"), false;
    return true;
}

// A server's shares, from `gio list` once its browse is mounted (step 7a), into cn.shares; false
// with why when gio names none and says why (a server that lists none: true, none).
static bool cn_list(char *why, size_t cap)
{
    static char out[65536];
    bool late;
    Err e;
    char *argv[] = { "gio", "list", "-a", "standard::type", cn.uri, NULL };
    if (!spawn_capture(NULL, argv, out, sizeof out, CONNECT_INFO_MS, &late, &e)) return snprintf(why, cap, "%s", e.msg), false;
    if (late) return snprintf(why, cap, "gio list gave no answer in %d s", CONNECT_INFO_MS / 1000), false;
    static char names[SERVER_SHARES_MAX][SERVER_SHARE_NAME];
    int n = gio_server_shares(str_from_c(out), names, SERVER_SHARES_MAX);
    if (n == 0 && gio_why(str_from_c(out), why, cap)) return false;
    pthread_mutex_lock(&cn.mu);
    memcpy(cn.shares, names, sizeof names), cn.nshares = n;
    pthread_mutex_unlock(&cn.mu);
    return true;
}

// What follows a mount: the share's folder, or a server's shares (its address alone, step 7a).
static bool cn_after(char *path, size_t pcap, char *why, size_t cap)
{
    return cn.browse ? cn_list(why, cap) : cn_folder(path, pcap, why, cap);
}

static void *cn_thread(void *arg)
{
    (void)arg;
    char why[512] = "", path[PATH_MAX] = "";
    bool cancelled = false, guest = false, ok = cn_mount(false, why, sizeof why, &cancelled, &guest) && cn_after(path, sizeof path, why, sizeof why);
    // Guest, not mounted by it: gio -a, whose refusal says why ("Anonymous access denied").
    pthread_mutex_lock(&cn.mu);
    bool quit = cn.quit;
    pthread_mutex_unlock(&cn.mu);
    if (!ok && guest && !cancelled && !quit) ok = cn_mount(true, why, sizeof why, &cancelled, &guest) && cn_after(path, sizeof path, why, sizeof why);
    pthread_mutex_lock(&cn.mu);
    cn_wipe();
    prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);   // (every copy of a password wiped: the field as it was answered)
    cn.ok = ok, cn.cancelled = cancelled, cn.phase = CN_DONE, cn.running = false;
    pthread_cond_broadcast(&cn.cv);
    snprintf(cn.path, sizeof cn.path, "%s", path);
    snprintf(cn.why, sizeof cn.why, "%s", why);
    pthread_mutex_unlock(&cn.mu);
    wake_main();
    return NULL;
}

// The location line's text, when it is an address to connect to: the connection started (or why
// not, in the status line), and true. False for anything else (a path: the location line goes there).
static bool connect_start(Str text)
{
    char uri[PATH_MAX], host[256], share[256], why[256];
    ConnectUri c = connect_uri(text, uri, sizeof uri, host, sizeof host, share, sizeof share, why, sizeof why);
    if (c == CONNECT_NOT) return false;
    if (c == CONNECT_BAD) return set_status(true, "%s", why), true;
    pthread_mutex_lock(&cn.mu);
    bool busy = cn.phase != CN_IDLE;
    if (!busy) {
        snprintf(cn.uri, sizeof cn.uri, "%s", uri), snprintf(cn.host, sizeof cn.host, "%s", host), snprintf(cn.share, sizeof cn.share, "%s", share);
        cn.browse = c == CONNECT_SERVER, cn.ssh = strncasecmp(uri, "sftp:", 5) == 0, cn.key = cn.unknown = false;
        cn.phase = CN_RUN, cn.shown = false, cn.reply = CN_WAIT, cn.user[0] = cn.domain[0] = 0, cn.running = true;
        prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    }
    pthread_mutex_unlock(&cn.mu);
    if (busy) return set_status(true, "Still connecting to %s", cn_name()), true;
    shares_drop();   // (a server's shares not yet shown: another connection replaces them, step 7a review)
    pthread_t t;
    if (pthread_create(&t, NULL, cn_thread, NULL) != 0) {
        pthread_mutex_lock(&cn.mu);
        cn.phase = CN_IDLE, cn.running = false;
        prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
        pthread_mutex_unlock(&cn.mu);
        return set_status(true, "Can't connect: %s", strerror(errno)), true;
    }
    pthread_detach(t);
    ui_edit_close(&app.ui);
    set_status(false, "Connecting to %s\xE2\x80\xA6", cn_name());
    return true;
}

// The question's password wiped, as it is handed on (the panel is gone: every way out of it is an
// answer).
static void connect_fields_clear(void)
{
    Ui *u = &app.ui;
    explicit_bzero(u->login_pass.b, sizeof u->login_pass.b);
    UI_TEXT_CLEAR(u->login_pass);
}

// The question answered (UA_LOGIN), as a guest (UA_LOGIN_GUEST), or not (UA_LOGIN_CANCEL): handed to
// the thread, the password wiped here.
static void connect_answer(UiActionKind k)
{
    Ui *u = &app.ui;
    pthread_mutex_lock(&cn.mu);
    if (cn.phase == CN_ASK && cn.reply == CN_WAIT) {
        snprintf(cn.user, sizeof cn.user, "%.*s", u->login_user.len, u->login_user.b);
        snprintf(cn.domain, sizeof cn.domain, "%.*s", u->login_domain.len, u->login_domain.b);
        int n = u->login_pass.len < (int)sizeof cn.pass - 1 ? u->login_pass.len : (int)sizeof cn.pass - 1;
        memcpy(cn.pass, u->login_pass.b, (size_t)n), cn.pass[n] = 0;
        cn.reply = k == UA_LOGIN ? CN_ANSWER : k == UA_LOGIN_GUEST ? CN_GUEST : CN_CANCEL;
        cn.shown = false;
        pthread_cond_broadcast(&cn.cv);
    }
    pthread_mutex_unlock(&cn.mu);
    connect_fields_clear();
    if (k == UA_LOGIN_CANCEL) set_status(false, "Not connected to %s", cn_name());
    else set_status(false, "Connecting to %s%s\xE2\x80\xA6", cn_name(), k == UA_LOGIN_GUEST ? " as a guest" : "");
}

// The thread's news, each frame: its question put up, or its result taken (the share's folder gone
// to, or why not said). True when something changed.
static bool connect_take(void)
{
    Ui *u = &app.ui;
    pthread_mutex_lock(&cn.mu);
    CnPhase ph = cn.phase;
    if (ph == CN_ASK && !cn.shown && cn.reply == CN_WAIT) {
        cn.shown = true;
        ui_login_open(u, cn.key ? UI_LOGIN_KEY : cn.ssh ? UI_LOGIN_SSH : UI_LOGIN_SHARE, cn.ask_user, cn.again, str_from_c(cn.def_user));
        pthread_mutex_unlock(&cn.mu);
        return true;
    }
    if (ph != CN_DONE) return pthread_mutex_unlock(&cn.mu), false;
    bool ok = cn.ok, cancelled = cn.cancelled, server = cn.browse, unknown = cn.unknown;
    if (ok && server) shares_keep(cn.uri, cn.host, cn.shares, cn.nshares);   // (the shares gui/ keeps: the panel's)
    char path[PATH_MAX], why[512];
    snprintf(path, sizeof path, "%s", cn.path), snprintf(why, sizeof why, "%s", cn.why);
    cn.phase = CN_IDLE;
    pthread_mutex_unlock(&cn.mu);
    ui_panel_close(u, UI_PANEL_LOGIN);   // (an answer closed it: none is waited for now)
    if (cancelled) return true;   // (said as it was cancelled)
    // (libsmbclient's word for a server name it can't find, and gvfs passes it on: a hint, as a name
    // isn't looked up first, which would miss the ones only found by broadcast; gvfs-nfs's for a
    // server unknown, not answering, or without that path, step 6)
    const char *hint = strcmp(why, "Invalid argument") == 0 ? " (is the server's name right?)" : strcmp(why, "Mount point does not exist") == 0 ? " (are the server's name and the folder right?)" : "";
    if (!ok && unknown) {   // (the host alone: "u@h" names the login)
        const char *h = strrchr(cn.host, '@');
        return set_status(true, "%s isn't a known host: connect once with ssh to check its key, then try again", h ? h + 1 : cn.host), true;
    }
    if (!ok) return set_status(true, "Can't connect to %s: %s%s", cn_name(), why[0] ? why : "no reason given", hint), true;
    if (server) {   // (a server's address: its shares to pick from, step 7a)
        int n = shares_offer();
        if (n == 0) return set_status(false, "%s lists no shares", cn_name()), true;
        set_status(false, "%d share%s on %s", n, n == 1 ? "" : "s", cn_name());
        return true;
    }
    watch_gvfs_kick();   // (the share in the sidebar's Network section at once, F16 step 5)
    if (go_to(path, STR_EMPTY, 0)) set_status(false, "Connected to %s", cn_name());
    return true;
}

// At quit: a question unanswered is cancelled as Cancel cancels it (and one gio asks later, too).
static void connect_quit(void)
{
    pthread_mutex_lock(&cn.mu);
    cn.quit = true;
    if (cn.reply == CN_WAIT) cn.reply = CN_CANCEL;
    cn_wipe();
    pthread_cond_broadcast(&cn.cv);
    pthread_mutex_unlock(&cn.mu);
    connect_fields_clear();
}

// The thread's end awaited until until (quit_workers': CONNECT_QUIT_MS from the moment every worker
// was told, for its unmount of a guest mount gvfs made anyway); past that gio is killed (no question
// was answered, so gvfs hasn't reached the share: a paused server, say) and the thread left to end
// on its own. (It took its own CONNECT_QUIT_MS after the others' joins, so a quit could take
// 0.5 + 1.5 s: the code-health review's step G.)
static void connect_join(const struct timespec *until)
{
    pthread_mutex_lock(&cn.mu);
    while (cn.running && pthread_cond_timedwait(&cn.cv, &cn.mu, until) == 0) {}
    if (cn.running && cn.pidfd >= 0) syscall(SYS_pidfd_send_signal, cn.pidfd, SIGKILL, NULL, 0);
    pthread_mutex_unlock(&cn.mu);
}
