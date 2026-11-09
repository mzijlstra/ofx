// Launching what ofx hands off: a file to `gio open`, a folder to the terminal, an app (Open With),
// a new window (docs/plan.md, "Launching"). Each child gets its own session (it outlives ofx, and a
// Ctrl+C in a terminal that started ofx doesn't reach it), /dev/null for stdin and stdout, and
// ofx's stderr. Children are reaped by the kernel: spawn_init sets SA_NOCLDWAIT on SIGCHLD.
// Then the short runs whose output ofx reads (spawn_capture, under a time limit): `gio info`'s
// type, and udisksctl's mount, unmount and eject with their results. Every program goes through
// the test guard's check (ops_program_allowed) as resolved on PATH.

#include "../base/base.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

static void spawn_init(void)
{
    struct sigaction sa = { .sa_handler = SIG_DFL, .sa_flags = SA_NOCLDWAIT };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);
}

static int spawn_started;   // programs started, for the GUI scripts' state line (spawned=)

// argv started (argv[0] looked up on PATH once, and that file run: the one the guard checked, not
// another PATH search's; a review, 2026-11-03), in a session of its own, its signals unblocked
// (gui/main.c blocks SIGTERM and SIGINT in every thread: a terminal started from ofx would ignore
// Ctrl+C), /dev/null for its input, out for its output (-1: /dev/null) and its errors too when
// err_too (else ofx's), and none of ofx's other fds (a folder's fd a copy job holds would keep a
// USB drive busy for as long as a terminal ran). False, with the reason, when it can't start or the
// test guard refuses check (the program that runs in the end: udisksctl under eject's sh -c, given
// as resolved; NULL: argv[0]'s file). spawn_fds: in for its input instead of /dev/null (-1: that).
static bool spawn_fds(const char *check, char *const *argv, int in, int out, bool err_too, pid_t *pid, Err *err)
{
    char full[PATH_MAX];
    if (!prog_resolve(argv[0], full, sizeof full)) return err_set(err, "%s is not installed", argv[0]), false;
    if (!ops_program_allowed(check ? check : full, err)) return false;   // (the file found, not a second search)
    posix_spawnattr_t attr;
    posix_spawn_file_actions_t fa;
    posix_spawnattr_init(&attr);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK);
    posix_spawn_file_actions_init(&fa);
    if (in >= 0) posix_spawn_file_actions_adddup2(&fa, in, 0);
    else posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (out >= 0) posix_spawn_file_actions_adddup2(&fa, out, 1);   // (dup2 drops O_CLOEXEC on 1)
    else posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    if (out >= 0 && err_too) posix_spawn_file_actions_adddup2(&fa, out, 2);
    posix_spawn_file_actions_addclosefrom_np(&fa, 3);
    int rc = posix_spawn(pid, full, &fa, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) return err_set(err, "%s: %s", argv[0], strerror(rc)), false;
    spawn_started++;
    return true;
}

static bool spawn_with(const char *check, char *const *argv, int out, bool err_too, pid_t *pid, Err *err)
{
    return spawn_fds(check, argv, -1, out, err_too, pid, err);
}

// Runs argv detached. False, with the reason, when it can't start, or when the test guard refuses
// it: with the guard on only a test's own fake runs (uwsm-app, which every launch goes through; F10
// review, 2026-11-01: without it on PATH an editor would start).
static bool spawn_detached(char *const *argv, Err *err)
{
    pid_t pid;
    return spawn_with(NULL, argv, -1, false, &pid, err);
}

// As spawn_detached, with the child's output on a pipe: its read end into *out (the caller reads it
// to its end, which is the child's, and closes it). For ofx-clean --all (View → Clear Previews…,
// F12): the kernel reaps the child (spawn_init), so its end and its summary come this way.
static bool spawn_piped(char *const *argv, int *out, Err *err)
{
    int p[2];
    pid_t pid;
    if (pipe2(p, O_CLOEXEC) != 0) return err_set(err, "a pipe: %s", strerror(errno)), false;
    bool ok = spawn_with(NULL, argv, p[1], false, &pid, err);
    close(p[1]);
    if (!ok) return close(p[0]), false;
    *out = p[0];
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

// ---- a program's output; eject, mount, unmount (F6.1) --------------------------------------------

// Runs argv (argv[0] on PATH) and reads what it writes, stdout and stderr together, until it ends
// or timeout_ms pass (then it and what it started are killed, *timed_out set): into out (cut at
// cap). Blocking: gui/ runs it off the main thread. The exit status can't be had (SA_NOCLDWAIT
// reaps the child), so a caller that needs it has the command say so (eject_argv).
static bool spawn_capture(const char *prog, char *const *argv, char *out, size_t cap, int timeout_ms, bool *timed_out, Err *err)
{
    *timed_out = false;
    // The test guard asks for prog (F11 review, 2026-11-02): what runs in the end (udisksctl under
    // eject's sh -c), argv[0] without one.
    int fd[2];
    pid_t pid;
    if (pipe2(fd, O_CLOEXEC) != 0) return err_set(err, "pipe: %s", strerror(errno)), false;
    bool ok = spawn_with(prog, argv, fd[1], true, &pid, err);
    close(fd[1]);
    if (!ok) return close(fd[0]), false;
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
    if (!spawn_capture(NULL, argv, buf, sizeof buf, timeout_ms, &late, &err) || late) return false;
    const char *key = "\n  standard::content-type: ";
    char *at = strstr(buf, key);
    if (!at) return false;
    at += strlen(key);
    size_t n = strcspn(at, "\n");
    if (n == 0 || n >= cap) return false;
    memcpy(out, at, n), out[n] = 0;
    return true;
}

// The command that ejects the drive dev with udisksctl, the program as resolved (prog_resolve: the
// one the guard checks, not the shell's own PATH search; health step 1 review, 2026-11-03): unmount,
// then power off, each step saying it went (an exit status can't be read, spawn_capture).
// script run as sh -c with dev as $1 and udisks as $2 (eject_argv's, unmount_argv's).
static void udisks_sh_argv(const char *script, const char *dev, const char *udisks, const char **argv)
{
    argv[0] = "sh", argv[1] = "-c", argv[2] = script, argv[3] = "sh", argv[4] = dev, argv[5] = udisks, argv[6] = NULL;
}

static void eject_argv(const char *dev, const char *udisks, const char **argv)
{
    udisks_sh_argv("\"$2\" unmount -b \"$1\" && echo OFX-UNMOUNTED && \"$2\" power-off -b \"$1\" && echo OFX-EJECTED", dev, udisks, argv);
}

// Mount (2026-10-30): an unmounted drive's row runs `udisksctl mount -b <dev>`, which mounts it under
// /run/media/$USER and says where. polkit lets the session's user mount a removable drive, or
// asks through the desktop's agent.
static void mount_argv(const char *dev, const char **argv)
{
    argv[0] = "udisksctl", argv[1] = "mount", argv[2] = "-b", argv[3] = dev, argv[4] = NULL;
}

// What mount_argv's output says: true with the folder in at, from "Mounted /dev/sdb1 at
// /run/media/m/STICK"; else false with the first line in why ("" when there is none).
static bool mount_result(Str out, char *at, size_t atcap, char *why, size_t whycap)
{
    at[0] = why[0] = 0;
    isize k = 0;
    for (Str line; next_line(out, &k, &line);) {
        line = str_trim(line);
        const char *m = line.len ? memmem(line.p, (size_t)line.len, " at ", 4) : NULL;
        isize a = m ? m - line.p : -1;
        if (str_starts_with(line, S("Mounted ")) && a > 0) {
            Str p = str_cut(line, a + 4, line.len);
            if (p.len && p.p[0] == '/' && (size_t)p.len < atcap) {
                snprintf(at, atcap, "%.*s", STR_ARG(p));
                // Older udisks ended the line with '.'; 2.11 doesn't, and a label may end in one
                // ("BACKUP."): dropped only when the folder is there without it, not with it.
                struct stat st;
                size_t n = strlen(at);
                if (n > 1 && at[n - 1] == '.' && stat(at, &st) != 0) {
                    at[n - 1] = 0;
                    if (stat(at, &st) != 0 || !S_ISDIR(st.st_mode)) at[n - 1] = '.';
                }
                return true;
            }
        }
        if (line.len && !why[0]) snprintf(why, whycap, "%.*s", STR_ARG(line));
    }
    return false;
}

// Unmount (2026-10-30): eject without the power-off, the drive left there to mount again; its
// output read as eject's (EJECT_UNMOUNTED is all it can reach). udisks is the udisksctl resolved, as
// eject's: sh's own PATH lookup would find another than the one the test guard checked (code-health
// review, 2026-11-06).
static void unmount_argv(const char *dev, const char *udisks, const char **argv)
{
    udisks_sh_argv("\"$2\" unmount -b \"$1\" && echo OFX-UNMOUNTED", dev, udisks, argv);
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
