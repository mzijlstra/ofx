// Launching what fx hands off: a file to xdg-open, a folder to the terminal (docs/plan.md,
// "Launching"). Each child gets its own session (it outlives fx, and a Ctrl+C in a terminal that
// started fx doesn't reach it), /dev/null for stdin and stdout, and fx's stderr. Children are
// reaped by the kernel: spawn_init sets SA_NOCLDWAIT on SIGCHLD.

#include "../base/base.h"

#include <errno.h>
#include <fcntl.h>
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
    // The child must not inherit fx's blocked SIGTERM and SIGINT (gui/main.c blocks them in every
    // thread): a terminal started from fx would ignore Ctrl+C.
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
static bool spawn_app(const char *const *cmd, isize n, Err *err)
{
    const char *argv[16];
    isize k = 0;
    if (on_path("uwsm-app")) argv[k++] = "uwsm-app", argv[k++] = "--";
    for (isize i = 0; i < n && k < COUNT(argv) - 1; i++) argv[k++] = cmd[i];
    argv[k] = NULL;
    if (!on_path(argv[0])) return err_set(err, "%s is not installed", argv[0]), false;
    return spawn_detached((char *const *)argv, err);
}

static bool spawn_open(const char *path, Err *err)
{
    const char *cmd[] = { "xdg-open", path };
    return spawn_app(cmd, COUNT(cmd), err);
}

static bool spawn_terminal(const char *dir, Err *err)
{
    char arg[4200];
    snprintf(arg, sizeof arg, "--dir=%s", dir);
    const char *cmd[] = { "xdg-terminal-exec", arg };
    return spawn_app(cmd, COUNT(cmd), err);
}
