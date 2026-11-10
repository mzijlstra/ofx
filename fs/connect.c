// Connect to Server (F16 step 4): an address typed as smb://[user@]server[:port]/share[/folder],
// nfs://server/path (step 6) or sftp://[user@]server[:port][/path] (step 7c), and what ofx reads of
// `gio mount` and `gio info` as they run (gui/connect.c runs them). Measured on the test Samba (tests/net, 2026-11-09):
// - gio asks on its output, a line at a time and with no newline after the question: a header
//   ("Authentication Required", then "Enter user and password for share “priv” on “host”:"), then
//   "User [default]: ", "Domain [default]: ", "Password: " (only those gvfs needs: a user in the
//   address leaves out User). Answers are read from its input, a line each, and not echoed, so
//   the next question follows the last on the same line.
// - gvfs asks even for a guest share. A wrong password is asked again, with the same header.
// - It says why it failed on its errors, as "gio: <address>: <why>" (after the volume monitors'
//   noise), and nothing on success; "Location is already mounted" is a success for ofx.
// - `gio info <address>` names the share's folder: "local path: <rt>/gvfs/smb-share:…".
// And on the test NFS server (tests/net/nfs, 2026-11-10): gvfs asks nothing (NFS goes by the user's
// id), and mounts the export holding the path (nfs://h/srv/nfs/sub mounts /srv/nfs, its folder
// "nfs:host=h,prefix=%2Fsrv%2Fnfs"); a wrong path, an unknown server or none answering all fail as
// "Mount point does not exist". gvfs-nfs takes no user and no port (it asks the server's
// portmapper, on 111).
// And on a test sshd (step 7c, 2026-11-10): gvfs-sftp runs ssh, which reads the user's own ~/.ssh
// (its config, known_hosts, keys; OpenSSH takes the home folder from the user database, not HOME)
// and the agent. It asks "Enter password for “u” on “h”:" then "Password: " (no domain; "User: "
// first, with no default, when the address names no user), or "Enter passphrase for secure key for
// …" then "Password: " for a key the agent doesn't hold. A host not in known_hosts: "Identity
// Verification Failed … [1] Log In Anyway [2] Cancel Login / Choice: ", with no fingerprint shown
// ("???"); ofx answers Cancel and says how to check the key (Michael, 2026-11-10). gvfs passes ssh
// NoHostAuthenticationForLocalhost, so 127.0.0.1 is never asked about.
// Included by fs.c.

#include <ctype.h>
#include <strings.h>

// CONNECT_SERVER: an SMB server's address alone, whose shares Connect lists (step 7a).
typedef enum { CONNECT_NOT, CONNECT_OK, CONNECT_BAD, CONNECT_SERVER } ConnectUri;

static bool connect_ieq(Str a, const char *b)
{
    isize n = (isize)strlen(b);
    if (a.len != n) return false;
    for (isize i = 0; i < n; i++)
        if (tolower((unsigned char)a.p[i]) != (unsigned char)b[i]) return false;
    return true;
}

// text as an address to connect to: CONNECT_NOT when it isn't one (no "smb://", "nfs://" or
// "sftp://": a path), CONNECT_BAD with why when it is one ofx won't take, else CONNECT_OK with the
// address (trimmed) in uri, and the server and share in host and share for what ofx says ("priv on
// 127.0.0.1", as gvfs names it; an NFS path's last name, as gvfs names an export: "nfs" of
// /srv/nfs). An SMB server's address alone (smb://nas, smb://nas:445/) is CONNECT_SERVER, share "":
// Connect lists its shares (step 7a). An SFTP address (step 7c) leaves share "" and names its host
// as gvfs does, "user@host" when it has a user. A password in the address is refused: it would be
// in gio's arguments, which any process can read.
static ConnectUri connect_uri(Str text, char *uri, size_t ucap, char *host, size_t hcap, char *share, size_t scap, char *why, size_t wcap)
{
    text = str_trim(text);
    if (text.len < 6) return CONNECT_NOT;
    bool nfs = connect_ieq(str_cut(text, 0, 6), "nfs://"), sftp = text.len >= 7 && connect_ieq(str_cut(text, 0, 7), "sftp://");
    if (!nfs && !sftp && !connect_ieq(str_cut(text, 0, 6), "smb://")) return CONNECT_NOT;
    const char *kind = nfs ? "nfs" : sftp ? "sftp" : "smb";
    for (isize i = 0; i < text.len; i++)
        if ((unsigned char)text.p[i] < 0x20 || text.p[i] == 0x7F) return snprintf(why, wcap, "An address has no control characters"), CONNECT_BAD;
    if ((size_t)text.len >= ucap) return snprintf(why, wcap, "The address is too long"), CONNECT_BAD;
    Str rest = str_cut(text, sftp ? 7 : 6, text.len);
    isize slash = str_find(rest, '/');
    Str auth = slash < 0 ? rest : str_cut(rest, 0, slash);
    Str path = slash < 0 ? STR_EMPTY : str_cut(rest, slash + 1, rest.len);
    isize at = str_find_last(auth, '@');
    Str user = STR_EMPTY;
    if (at >= 0) {
        if (str_find(str_cut(auth, 0, at), ':') >= 0) return snprintf(why, wcap, "No password in the address: ofx asks for it"), CONNECT_BAD;
        if (nfs) return snprintf(why, wcap, "No user in an NFS address: the server goes by your user id"), CONNECT_BAD;
        user = str_cut(auth, 0, at);
        auth = str_cut(auth, at + 1, auth.len);
    }
    isize colon = str_find_last(auth, ':');
    Str name = colon < 0 ? auth : str_cut(auth, 0, colon);
    if (colon >= 0) {
        if (nfs) return snprintf(why, wcap, "No port in an NFS address: gvfs asks the server's portmapper"), CONNECT_BAD;
        Str port = str_cut(auth, colon + 1, auth.len);
        bool digits = port.len > 0 && port.len <= 5;
        for (isize i = 0; i < port.len; i++) digits = digits && port.p[i] >= '0' && port.p[i] <= '9';
        if (!digits) return snprintf(why, wcap, "Not a port: %.*s", STR_ARG(port)), CONNECT_BAD;
    }
    if (name.len == 0) return snprintf(why, wcap, "Name a server: %s://server/%s", kind, nfs ? "folder" : sftp ? "" : "share"), CONNECT_BAD;
    if (sftp) {   // (a path or none: the server's root)
        if ((size_t)(user.len + 1 + name.len) >= hcap) return snprintf(why, wcap, "The address is too long"), CONNECT_BAD;
        snprintf(uri, ucap, "%.*s", STR_ARG(text));
        if (user.len) snprintf(host, hcap, "%.*s@%.*s", STR_ARG(user), STR_ARG(name));
        else snprintf(host, hcap, "%.*s", STR_ARG(name));
        if (scap) share[0] = 0;
        return CONNECT_OK;
    }
    Str sh;
    if (nfs) {   // (the path's last name: "nfs" of srv/nfs/, as gvfs names an export)
        while (path.len > 0 && path.p[path.len - 1] == '/') path.len--;
        sh = str_cut(path, str_find_last(path, '/') + 1, path.len);
    } else {
        isize end = str_find(path, '/');
        sh = end < 0 ? path : str_cut(path, 0, end);
    }
    // (smb://server[:port][/] alone: the server, whose shares Connect lists, step 7a; gvfs-nfs lists
    // no exports)
    if (sh.len == 0 && (nfs || path.len > 0)) return nfs ? snprintf(why, wcap, "Name a folder on %.*s: nfs://%.*s/folder", STR_ARG(name), STR_ARG(name)) : snprintf(why, wcap, "Name a share on %.*s: smb://%.*s/share", STR_ARG(name), STR_ARG(auth)), CONNECT_BAD;
    if ((size_t)name.len >= hcap || (size_t)sh.len >= scap) return snprintf(why, wcap, "The address is too long"), CONNECT_BAD;
    snprintf(uri, ucap, "%.*s", STR_ARG(text));
    snprintf(host, hcap, "%.*s", STR_ARG(name));
    snprintf(share, scap, "%.*s", STR_ARG(sh));
    return sh.len == 0 ? CONNECT_SERVER : CONNECT_OK;
}

// GIO_ASK_KEY: the password of a key (ssh's passphrase, step 7c); GIO_ASK_CHOICE: a question with
// numbered answers (an SFTP host not known: ofx answers Cancel).
typedef enum { GIO_ASK_NONE, GIO_ASK_USER, GIO_ASK_DOMAIN, GIO_ASK_PASSWORD, GIO_ASK_KEY, GIO_ASK_CHOICE } GioAsk;

// The question gio's output out ends on, waiting for a line (GIO_ASK_NONE: none), its default
// (between the brackets; none without them) into def; *rounds: how many times it has asked (its
// headers so far).
static GioAsk gio_ask(Str out, char *def, size_t cap, int *rounds)
{
    static const Str HEAD = S("Authentication Required");
    *rounds = 0;
    isize last = -1;   // (the last header: what this question is about)
    for (isize i = 0; i + HEAD.len <= out.len; i++)
        if (memcmp(out.p + i, HEAD.p, (size_t)HEAD.len) == 0) ++*rounds, last = i, i += HEAD.len - 1;
    if (cap) def[0] = 0;
    if (str_ends_with(out, S("\nChoice: "))) return GIO_ASK_CHOICE;
    if (*rounds == 0) return GIO_ASK_NONE;
    if (str_ends_with(out, S("Password: "))) {
        Str about = str_cut(out, last, out.len);
        for (isize i = 0; i + 10 <= about.len; i++)
            if (memcmp(about.p + i, "passphrase", 10) == 0) return GIO_ASK_KEY;
        return GIO_ASK_PASSWORD;
    }
    if (str_ends_with(out, S("\nUser: "))) return GIO_ASK_USER;   // (sftp's: no default)
    if (!str_ends_with(out, S("]: "))) return GIO_ASK_NONE;
    isize nl = str_find_last(out, '\n');
    Str line = str_cut(out, nl + 1, out.len - 3);   // ("… User [guest" of "… User [guest]: ")
    isize open = str_find_last(line, '[');
    if (open < 1 || line.p[open - 1] != ' ') return GIO_ASK_NONE;
    Str label = str_cut(line, 0, open - 1), d = str_cut(line, open + 1, line.len);
    GioAsk k = str_ends_with(label, S("User")) ? GIO_ASK_USER : str_ends_with(label, S("Domain")) ? GIO_ASK_DOMAIN : GIO_ASK_NONE;
    if (k != GIO_ASK_NONE && cap) snprintf(def, cap, "%.*s", STR_ARG(d));
    return k;
}

// Why gio failed, from its errors err (its last "gio: " line, without the address and gvfs's
// "Failed to mount Windows share: ") into why; false when it said nothing.
static bool gio_why(Str err, char *why, size_t cap)
{
    isize at = -1;
    if (str_starts_with(err, S("gio: "))) at = 0;
    for (isize i = 0; i + 6 <= err.len; i++)
        if (err.p[i] == '\n' && memcmp(err.p + i + 1, "gio: ", 5) == 0) at = i + 1;
    if (at < 0) return false;
    Str line = str_cut(err, at + 5, err.len);
    isize nl = str_find(line, '\n');
    if (nl >= 0) line = str_cut(line, 0, nl);
    // "smb://host/share/: why": the address ends at the first ": " after its "://"
    for (isize i = 0; i + 3 <= line.len; i++)
        if (memcmp(line.p + i, "://", 3) == 0) {
            for (isize j = i + 3; j + 2 <= line.len; j++)
                if (line.p[j] == ':' && line.p[j + 1] == ' ') {
                    line = str_cut(line, j + 2, line.len);
                    break;
                }
            break;
        }
    static const Str SMB = S("Failed to mount Windows share: ");
    if (str_starts_with(line, SMB) && line.len > SMB.len) line = str_cut(line, SMB.len, line.len);
    snprintf(why, cap, "%.*s", STR_ARG(line));
    return true;
}

// ---- a server's shares (F16 step 7a) -----------------------------------------------------------
//
// `gio mount smb://server/` mounts gvfs's browse of the server (no FUSE folder: measured, it isn't
// even in `gio mount -l`), then `gio list -a standard::type smb://server/` names its shares, one a
// line: "pub\t0\t(mountable)". Each share's target-uri drops the port (smb://127.0.0.1/pub,
// measured), so ofx makes a share's address from the server's itself.

enum { SERVER_SHARES_MAX = 64, SERVER_SHARE_NAME = 256 };

static int server_share_cmp(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

// The shares in `gio list`'s output out, into names (max at most), sorted as a person reads them
// (case folded); their count. Only lines of the mountable kind; the hidden ones a Windows server
// lists ("C$", "IPC$": a name ending in $) and a name with a control character left out.
static int gio_server_shares(Str out, char (*names)[SERVER_SHARE_NAME], int max)
{
    int n = 0;
    isize at = 0;
    for (Str line; n < max && next_line(out, &at, &line);) {
        isize t1 = str_find(line, '\t');
        if (t1 <= 0) continue;
        Str name = str_cut(line, 0, t1), rest = str_cut(line, t1 + 1, line.len);
        isize t2 = str_find(rest, '\t');
        if (t2 < 0) continue;
        Str kind = str_cut(rest, t2 + 1, rest.len);
        isize t3 = str_find(kind, '\t');
        if (t3 >= 0) kind = str_cut(kind, 0, t3);
        if (!str_eq(kind, S("(mountable)")) || name.p[name.len - 1] == '$' || (size_t)name.len >= SERVER_SHARE_NAME) continue;
        bool ctl = false;
        for (isize i = 0; i < name.len; i++) ctl = ctl || (unsigned char)name.p[i] < 0x20 || name.p[i] == 0x7F;
        if (ctl) continue;
        snprintf(names[n++], SERVER_SHARE_NAME, "%.*s", STR_ARG(name));
    }
    qsort(names, (size_t)n, SERVER_SHARE_NAME, server_share_cmp);
    return n;
}

// The address of a share on the server whose address is server (smb://nas, smb://nas:445/), into
// out; false when it doesn't fit. The name as it is: Connect takes it as typed (connect_uri).
static bool server_share_uri(const char *server, const char *share, char *out, size_t cap)
{
    Str s = str_from_c(server);
    while (s.len > 0 && s.p[s.len - 1] == '/') s.len--;
    int n = snprintf(out, cap, "%.*s/%s", STR_ARG(s), share);
    return n > 0 && (size_t)n < cap;
}

// ---- servers on the network (F16 step 7b) ------------------------------------------------------
//
// Arch's gvfs has no dnssd backend (network:/// lists nothing) and Windows' NetBIOS browsing
// (smb:///) finds nothing on a modern network, so ofx asks avahi itself:
// `avahi-browse -k -t -r -p _smb._tcp` (a query on the network, about 1 s; -k keeps the type as
// "_smb._tcp", not its database name), or with -c avahi's cache
// alone (25 ms, no traffic; measured on Michael's LAN, 2026-11-10). A resolved service a line:
// "=;eth0;IPv4;YOGA;_smb._tcp;local;yoga.local;192.168.1.165;445;\"txt\"", its name escaped as
// avahi does (a byte as \DDD, its decimal value: \059 for ';', \032 a space; or \ and the
// character), each server once for IPv4 and once for IPv6.

enum { NET_SERVERS_MAX = 32 };
typedef struct {
    char name[64];     // as it announces itself ("YOGA")
    char addr[300];    // smb://host[:port]: what Connect takes
} NetServer;

// An avahi name field unescaped into out (cut at cap).
static void avahi_unescape(Str f, char *out, size_t cap)
{
    size_t o = 0;
    for (isize i = 0; i < f.len && o + 1 < cap; i++) {
        if (f.p[i] != '\\' || i + 1 >= f.len) { out[o++] = f.p[i]; continue; }
        if (i + 3 < f.len && isdigit((unsigned char)f.p[i + 1]) && isdigit((unsigned char)f.p[i + 2]) && isdigit((unsigned char)f.p[i + 3])) {
            int v = (f.p[i + 1] - '0') * 100 + (f.p[i + 2] - '0') * 10 + (f.p[i + 3] - '0');
            if (v > 0 && v < 256) out[o++] = (char)v;
            i += 3;
        } else out[o++] = f.p[++i];
    }
    out[o] = 0;
}

static int net_server_cmp(const void *a, const void *b)
{
    return strcasecmp(((const NetServer *)a)->name, ((const NetServer *)b)->name);
}

// The SMB servers in `avahi-browse -r -p`'s output out, into s (max at most), each once (by its
// address), sorted by name; their count. A line not of a resolved service, a host name with other
// than letters, digits, '.' and '-', a port that isn't a number, or a name with a control
// character, left out.
static int avahi_smb_servers(Str out, NetServer *s, int max)
{
    int n = 0;
    isize at = 0;
    for (Str line; n < max && next_line(out, &at, &line);) {
        Str f[10];
        int k = 0;
        for (isize i = 0, start = 0; i <= line.len && k < 10; i++)
            if (i == line.len || line.p[i] == ';') f[k++] = str_cut(line, start, i), start = i + 1;
        // (a resolved service's line has ten fields; the others, "+" and "-", six)
        if (k < 9 || !str_eq(f[4], S("_smb._tcp")) || f[6].len == 0 || f[8].len == 0) continue;
        bool ok = true;
        for (isize i = 0; i < f[6].len; i++) ok = ok && (isalnum((unsigned char)f[6].p[i]) || f[6].p[i] == '.' || f[6].p[i] == '-');
        for (isize i = 0; i < f[8].len; i++) ok = ok && isdigit((unsigned char)f[8].p[i]);
        if (!ok) continue;
        NetServer v;
        avahi_unescape(f[3], v.name, sizeof v.name);
        for (const char *c = v.name; *c && ok; c++) ok = (unsigned char)*c >= 0x20 && *c != 0x7F;
        if (!ok || !v.name[0]) continue;
        char ps[8];
        snprintf(ps, sizeof ps, "%.*s", STR_ARG(f[8]));
        int port = atoi(ps);
        if (port <= 0 || port > 65535) continue;
        int w = port == 445 ? snprintf(v.addr, sizeof v.addr, "smb://%.*s", STR_ARG(f[6])) : snprintf(v.addr, sizeof v.addr, "smb://%.*s:%d", STR_ARG(f[6]), port);
        if (w <= 0 || (size_t)w >= sizeof v.addr) continue;
        bool seen = false;
        for (int j = 0; j < n && !seen; j++) seen = strcmp(s[j].addr, v.addr) == 0;
        if (!seen) s[n++] = v;
    }
    qsort(s, (size_t)n, sizeof *s, net_server_cmp);
    return n;
}

// The share's folder from `gio info`'s output into path; false when it names none (a server's
// list of shares, which isn't a folder).
static bool gio_local_path(Str info, char *path, size_t cap)
{
    static const Str KEY = S("local path: ");
    for (isize i = 0; i + KEY.len <= info.len; i++) {
        if ((i > 0 && info.p[i - 1] != '\n') || memcmp(info.p + i, KEY.p, (size_t)KEY.len) != 0) continue;
        Str p = str_cut(info, i + KEY.len, info.len);
        isize nl = str_find(p, '\n');
        if (nl >= 0) p = str_cut(p, 0, nl);
        if (p.len == 0 || p.p[0] != '/' || (size_t)p.len >= cap) return false;
        snprintf(path, cap, "%.*s", STR_ARG(p));
        return true;
    }
    return false;
}

// ---- gvfs's share folders (F16 step 5) ---------------------------------------------------------
//
// gvfsd-fuse shows each mounted share as a folder of its FUSE mount named by the share's mount spec:
// "smb-share:port=4445,server=nas,share=pub", "sftp:host=h,user=u", "afp-volume:host=h,volume=v",
// "dav:host=h,ssl=true", keys sorted, values escaped as in a URI (%xx).

// The value of key in a gvfs folder name (the part after its "type:"), unescaped into out; false
// when it has none.
static bool gvfs_key(Str name, const char *key, char *out, size_t cap)
{
    isize colon = str_find(name, ':');
    if (colon < 0 || cap == 0) return false;
    Str rest = str_cut(name, colon + 1, name.len), k = str_from_c(key);
    isize at = 0;
    while (at <= rest.len) {
        isize end = at;
        while (end < rest.len && rest.p[end] != ',') end++;
        Str part = str_cut(rest, at, end);
        if (part.len > k.len && memcmp(part.p, k.p, (size_t)k.len) == 0 && part.p[k.len] == '=') {
            Str v = str_cut(part, k.len + 1, part.len);
            size_t o = 0;
            for (isize i = 0; i < v.len && o + 1 < cap; i++) {
                int hi, lo;
                if (v.p[i] == '%' && i + 2 < v.len && (hi = hex_digit(v.p[i + 1])) >= 0 && (lo = hex_digit(v.p[i + 2])) >= 0)
                    out[o++] = (char)(hi << 4 | lo), i += 2;
                else out[o++] = v.p[i];
            }
            out[o] = 0;
            return o > 0;
        }
        at = end + 1;
    }
    return false;
}

// A share's name as gvfs (and Nautilus, Nemo) give it, from its folder's name: "pub on nas" for an
// SMB share or an AFP volume, "nfs on h" for an NFS export (its path's last name: /srv/nfs), "u@h"
// or "h" for others; the folder's name when it isn't one of gvfs's.
static void gvfs_share_label(Str name, char *out, size_t cap)
{
    char what[256], host[256], user[128], prefix[PATH_MAX];
    bool share = gvfs_key(name, "share", what, sizeof what) || gvfs_key(name, "volume", what, sizeof what);
    if (!share && gvfs_key(name, "prefix", prefix, sizeof prefix)) {   // (an NFS export's path)
        Str pre = str_from_c(prefix);
        while (pre.len > 1 && pre.p[pre.len - 1] == '/') pre.len--;
        Str base = path_base(pre);
        share = base.len > 0 && (size_t)base.len < sizeof what;
        if (share) snprintf(what, sizeof what, "%.*s", STR_ARG(base));
    }
    bool server = gvfs_key(name, "server", host, sizeof host) || gvfs_key(name, "host", host, sizeof host);
    if (share && server) snprintf(out, cap, "%s on %s", what, host);
    else if (server && gvfs_key(name, "user", user, sizeof user)) snprintf(out, cap, "%s@%s", user, host);
    else if (server) snprintf(out, cap, "%s", host);
    else snprintf(out, cap, "%.*s", STR_ARG(name));
}

// The address of a folder in an SMB share, NFS export or SFTP server gvfs mounted (Add Bookmark there writes it,
// as Nautilus does, not the FUSE folder's path): name its share's folder's name, rest the path below
// that ("" or "/sub/x"). smb://[dom;user@]server[:port]/share/rest or nfs://server/prefix/rest, the
// path escaped as a URI's; false for another kind of share or one that doesn't fit.
static bool gvfs_share_uri(Str name, const char *rest, char *out, size_t cap)
{
    char server[256], share[256], port[16], user[128], domain[128], path[PATH_MAX], esc[PATH_MAX * 3 + 8];
    if (str_starts_with(name, S("sftp:"))) {   // (sftp://[user@]host[:port]/path: the folder's path from the server's root)
        if (!gvfs_key(name, "host", server, sizeof server)) return false;
        bool hp = gvfs_key(name, "port", port, sizeof port), hu = gvfs_key(name, "user", user, sizeof user);
        if (snprintf(path, sizeof path, "%s", rest[0] ? rest : "/") >= (int)sizeof path || !percent_encode(str_from_c(path), "!$&'()*+,=@:/", esc, sizeof esc)) return false;
        int n = snprintf(out, cap, "sftp://%s%s%s%s%s%s", hu ? user : "", hu ? "@" : "", server, hp ? ":" : "", hp ? port : "", esc);
        return n > 0 && (size_t)n < cap;
    }
    if (str_starts_with(name, S("nfs:"))) {
        if (!gvfs_key(name, "host", server, sizeof server) || !gvfs_key(name, "prefix", path, sizeof path) || path[0] != '/') return false;
        size_t pl = strlen(path);
        if (pl > 1 && path[pl - 1] == '/') path[--pl] = 0;
        if (snprintf(path + pl, sizeof path - pl, "%s", pl == 1 && rest[0] == '/' ? rest + 1 : rest) >= (int)(sizeof path - pl) || !percent_encode(str_from_c(path), "!$&'()*+,=@:/", esc, sizeof esc)) return false;
        int n = snprintf(out, cap, "nfs://%s%s", server, esc);
        return n > 0 && (size_t)n < cap;
    }
    if (!str_starts_with(name, S("smb-share:")) || !gvfs_key(name, "server", server, sizeof server) || !gvfs_key(name, "share", share, sizeof share)) return false;
    bool hp = gvfs_key(name, "port", port, sizeof port), hu = gvfs_key(name, "user", user, sizeof user), hd = gvfs_key(name, "domain", domain, sizeof domain);
    if (snprintf(path, sizeof path, "/%s%s", share, rest) >= (int)sizeof path || !percent_encode(str_from_c(path), "!$&'()*+,=@:/", esc, sizeof esc)) return false;
    int n = snprintf(out, cap, "smb://%s%s%s%s%s%s%s%s", hd && hu ? domain : "", hd && hu ? ";" : "", hu ? user : "", hu ? "@" : "", server, hp ? ":" : "", hp ? port : "", esc);
    return n > 0 && (size_t)n < cap;
}

// Whether a folder name is one gvfs gives a share: "type:key=value…" (a scratch stand-in's own
// folders aren't).
static bool gvfs_is_share(const char *n)
{
    const char *colon = strchr(n, ':');
    if (!colon || colon == n) return false;
    for (const char *c = n; c < colon; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')) return false;
    return strchr(colon, '=') != NULL;
}

static int gvfs_cmp(const void *x, const void *y)
{
    return strcmp(*(char *const *)x, *(char *const *)y);
}

enum { GVFS_SHARES_MAX = 64 };   // (as many as the sidebar has places)

// `ofx-list --shares` (F16 step 5): the shares gvfs has mounted, a folder a line, sorted: every
// fuse.gvfsd-fuse mount of the mount list mi, its folders named as gvfs names a share. Then a line
// "." (a cut-off answer, or one from a lister that failed, isn't taken for every share gone). False,
// writing nothing, when mi is empty (a read that failed).
static bool gvfs_shares(FILE *out, Arena *a, Str mi)
{
    if (mi.len == 0) return false;
    char *line_of[GVFS_SHARES_MAX];
    int k = 0;
    isize at = 0;
    for (Str line; next_line(mi, &at, &line);) {
        char mp[PATH_MAX];
        Str type, src;
        if (!mountinfo_line(line, mp, sizeof mp, &type, &src) || !str_eq(type, S("fuse.gvfsd-fuse"))) continue;
        DIR *d = opendir(mp);
        for (struct dirent *e; d && k < GVFS_SHARES_MAX && (e = readdir(d));)
            if (gvfs_is_share(e->d_name)) line_of[k++] = arena_sprintf(a, "%s/%s", mp, e->d_name);
        if (d) closedir(d);
    }
    qsort(line_of, (size_t)k, sizeof *line_of, gvfs_cmp);   // (by name, whatever order the folder gives)
    for (int i = 0; i < k; i++) fprintf(out, "%s\n", line_of[i]);
    fprintf(out, ".\n");
    return true;
}

// The wait before the next listing of gvfs's shares (F16 step 7d, Michael 2026-11-10), after one that
// waited prev ms (0: the first): base again when reset (a share is mounted, the list changed, or ofx
// itself connected or unmounted), else twice prev, up to most.
static int share_poll_next(int prev, int base, int most, bool reset)
{
    if (reset || prev <= 0) return base;
    return prev > most / 2 ? most : prev * 2;
}
