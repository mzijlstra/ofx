// Launching what ofx hands off: a file to `gio open`, a folder to the terminal (docs/plan.md,
// "Launching"). Each child gets its own session (it outlives ofx, and a Ctrl+C in a terminal that
// started ofx doesn't reach it), /dev/null for stdin and stdout, and ofx's stderr. Children are
// reaped by the kernel: spawn_init sets SA_NOCLDWAIT on SIGCHLD.

#include "../base/base.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern char **environ;

static void spawn_init(void)
{
    struct sigaction sa = { .sa_handler = SIG_DFL, .sa_flags = SA_NOCLDWAIT };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);
}

// Whether prog is an executable file in a PATH directory.
static bool on_path(const char *prog)
{
    const char *path = getenv("PATH");
    Str p = str_from_c(path ? path : "");
    for (isize at = 0; at <= p.len;) {
        isize e = at;
        while (e < p.len && p.p[e] != ':') e++;
        char full[4096];
        if (e > at && snprintf(full, sizeof full, "%.*s/%s", (int)(e - at), p.p + at, prog) < (int)sizeof full &&
            access(full, X_OK) == 0)
            return true;
        at = e + 1;
    }
    return false;
}

// Runs argv (argv[0] looked up on PATH) detached. False, with the reason, when it can't start.
static bool spawn_detached(char *const *argv, Err *err)
{
    posix_spawnattr_t attr;
    posix_spawn_file_actions_t fa;
    posix_spawnattr_init(&attr);
    // The child must not inherit ofx's blocked SIGTERM and SIGINT (gui/main.c blocks them in every
    // thread): a terminal started from ofx would ignore Ctrl+C.
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK);
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], &fa, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) return err_set(err, "%s: %s", argv[0], strerror(rc)), false;
    return true;
}

// Through uwsm-app when it is there, as Omarchy launches apps (each in its own systemd scope).
enum { SPAWN_FILES_MAX = 256 };   // files in one launch (F7.3); more go in further launches

static bool spawn_app(const char *const *cmd, isize n, Err *err)
{
    const char *argv[SPAWN_FILES_MAX + 8];
    isize k = 0;
    if (on_path("uwsm-app")) argv[k++] = "uwsm-app", argv[k++] = "--";
    for (isize i = 0; i < n && k < COUNT(argv) - 1; i++) argv[k++] = cmd[i];
    argv[k] = NULL;
    if (strchr(argv[0], '/') ? access(argv[0], X_OK) != 0 : !on_path(argv[0])) return err_set(err, "%s is not installed", argv[0]), false;
    return spawn_detached((char *const *)argv, err);
}

// GIO, as Nemo opens files (Michael, 2026-10-24): the type from the name (a .md is text/markdown,
// Typora), and a Terminal=true app (nvim) in a terminal through xdg-terminal-exec. xdg-open on
// Hyprland is xdg-utils' generic mode: it typed CLAUDE.md by content (text/plain, nvim) and ran
// nvim in the terminal it was called from, which ofx hasn't: nothing opened.
static bool spawn_open(const char *path, Err *err)
{
    const char *cmd[] = { "gio", "open", path };
    return spawn_app(cmd, COUNT(cmd), err);
}

// Another ofx on dir (F7.1, New Window): exe is this one's own binary, so the window that opens is
// the same build.
static bool spawn_window(const char *exe, const char *dir, Err *err)
{
    const char *cmd[] = { exe, dir };
    return spawn_app(cmd, COUNT(cmd), err);
}

// Another ofx on path's folder with path selected, a folder too (FileManager1's ShowItems).
static bool spawn_window_select(const char *exe, const char *path, Err *err)
{
    const char *cmd[] = { exe, "--select", path };
    return spawn_app(cmd, COUNT(cmd), err);
}

// An app's desktop file run on path (F7.1.1: an empty file, whose type gio can't tell): GIO's own
// launch, so a Terminal=true app (nvim) gets its terminal as with `gio open`.
static bool spawn_launch(const char *desktop, const char *path, Err *err)
{
    const char *cmd[] = { "gio", "launch", desktop, path };
    return spawn_app(cmd, COUNT(cmd), err);
}

// The app's desktop file run on n files at once (F7.3, Open With and Enter on several): one
// `gio launch`, which hands them all to an app that takes several (%F, %U) and runs one that takes
// one (%f) once a file. At most SPAWN_FILES_MAX: the caller splits more.
static bool spawn_launch_all(const char *desktop, char *const *paths, isize n, Err *err)
{
    const char *cmd[SPAWN_FILES_MAX + 3] = { "gio", "launch", desktop };
    isize k = 3;
    for (isize i = 0; i < n && i < SPAWN_FILES_MAX; i++) cmd[k++] = paths[i];
    return spawn_app(cmd, k, err);
}

static bool spawn_terminal(const char *dir, Err *err)
{
    char arg[4200];
    snprintf(arg, sizeof arg, "--dir=%s", dir);
    const char *cmd[] = { "xdg-terminal-exec", arg };
    return spawn_app(cmd, COUNT(cmd), err);
}

// ---- eject (F6.1) -----------------------------------------------------------------------------

// Runs argv (argv[0] on PATH) and reads what it writes, stdout and stderr together, until it ends
// or timeout_ms pass (then it and what it started are killed, *timed_out set): into out (cut at
// cap). Blocking: gui/ runs it off the main thread. The exit status can't be had (SA_NOCLDWAIT
// reaps the child), so a caller that needs it has the command say so (eject_argv).
static bool spawn_capture(char *const *argv, char *out, size_t cap, int timeout_ms, bool *timed_out, Err *err)
{
    *timed_out = false;
    int fd[2];
    if (pipe2(fd, O_CLOEXEC) != 0) return err_set(err, "pipe: %s", strerror(errno)), false;
    posix_spawnattr_t attr;
    posix_spawn_file_actions_t fa;
    posix_spawnattr_init(&attr);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK);
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, fd[1], 1);
    posix_spawn_file_actions_adddup2(&fa, fd[1], 2);
    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], &fa, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    close(fd[1]);
    if (rc != 0) {
        close(fd[0]);
        return err_set(err, "%s: %s", argv[0], strerror(rc)), false;
    }
    // Read to the end (the child must not block on a full pipe), keeping what fits.
    size_t n = 0;
    char chunk[512];
    i64 until = now_ms() + timeout_ms;
    for (;;) {
        i64 left = until - now_ms();
        struct pollfd p = { fd[0], POLLIN, 0 };
        int ready = left > 0 ? poll(&p, 1, (int)left) : 0;
        if (ready < 0 && errno == EINTR) continue;
        if (ready == 0) {   // the deadline: the child is a session (and group) leader, its group goes
            kill(-pid, SIGKILL);
            *timed_out = true;
            break;
        }
        ssize_t r = read(fd[0], chunk, sizeof chunk);
        if (r == 0) break;
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) break;
        size_t room = cap ? cap - 1 - n : 0, take = (size_t)r < room ? (size_t)r : room;
        memcpy(out + n, chunk, take);
        n += take;
    }
    close(fd[0]);
    if (cap) out[n] = 0;
    return true;
}

// The type gio gives path (its content read, as `gio open` types it: an empty file is
// application/x-zerosize) into out; false, out empty, when gio doesn't say within timeout_ms. For
// the Properties panel's MIME type (Michael, 2026-10-26).
static bool spawn_gio_type(const char *path, char *out, size_t cap, int timeout_ms)
{
    char buf[4096];
    bool late = false;
    Err err;
    char *argv[] = { "gio", "info", "-a", "standard::content-type", "--", (char *)path, NULL };
    out[0] = 0;
    if (!spawn_capture(argv, buf, sizeof buf, timeout_ms, &late, &err) || late) return false;
    const char *key = "\n  standard::content-type: ";
    char *at = strstr(buf, key);
    if (!at) return false;
    at += strlen(key);
    size_t n = strcspn(at, "\n");
    if (n == 0 || n >= cap) return false;
    memcpy(out, at, n), out[n] = 0;
    return true;
}

// The command that ejects the drive dev: unmount, then power off, each step saying it went (an
// exit status can't be read, spawn_capture).
static void eject_argv(const char *dev, const char **argv)
{
    argv[0] = "sh", argv[1] = "-c";
    argv[2] = "udisksctl unmount -b \"$1\" && echo OFX-UNMOUNTED && udisksctl power-off -b \"$1\" && echo OFX-EJECTED";
    argv[3] = "sh", argv[4] = dev, argv[5] = NULL;
}

typedef enum { EJECT_FAILED, EJECT_UNMOUNTED, EJECT_DONE } EjectResult;

// What eject_argv's output says: ejected, unmounted only, or neither; *why gets the first line
// that isn't ours (udisksctl's error), "" when there is none.
static EjectResult eject_result(Str out, char *why, size_t cap)
{
    bool unmounted = false, done = false;
    why[0] = 0;
    isize at = 0;
    for (Str line; next_line(out, &at, &line);) {
        line = str_trim(line);
        if (str_eq(line, S("OFX-UNMOUNTED"))) unmounted = true;
        else if (str_eq(line, S("OFX-EJECTED"))) done = true;
        else if (line.len && !why[0]) snprintf(why, cap, "%.*s", STR_ARG(line));
    }
    return done && unmounted ? EJECT_DONE : unmounted ? EJECT_UNMOUNTED : EJECT_FAILED;
}
