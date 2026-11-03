// A program run in bubblewrap (F12): the thumbnailers (thumbers.c) and git status (git.c) parse
// what is on the drive, a download or a repository someone else made, and git runs what a
// repository's own config names (core.fsmonitor, a filter). So each runs with /usr read only, the
// paths it needs read only (one or two folders writable when it writes), no network, no other processes,
// nothing else of the home folder, no terminal (a session of its own), and of ofx's environment only
// PATH, LANG, LC_ALL and HOME (a token in it could leave through what it writes; F12 step 3
// review). Without bubblewrap installed nothing runs (Michael, 2026-11-02, for both). It has a time
// limit; then it is killed with whatever it started.

#include "../base/base.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

enum { BOX_BINDS_MAX = 8, BOX_ARGS_MAX = 64 };

typedef enum { BOX_NOT_RUN, BOX_ENDED, BOX_KILLED } BoxRun;

typedef struct {
    const char *const *argv;          // argv[0]: a name on PATH, or a path
    const char *ro[BOX_BINDS_MAX];    // read only inside, each at its own path (one not there: skipped)
    const char *rw[2];                // writable inside (each NULL: none)
    const char *cwd;                  // where it starts, inside (NULL: /)
    int         ms;                   // its time
    char       *out;                  // its output read into out, cap bytes at most (the rest
    isize       cap, *len;            // read and dropped); out NULL: /dev/null
    bool        err;                  // its error output into out too (git's refusal: F12 step 5c)
    bool        outlive;              // it finishes if ofx ends meanwhile (a switch cut short leaves the work tree half switched)
} BoxJob;

static bool box_sandbox = true;   // (false: run as it is; a test's only, OFX_TEST_SANDBOX=off under the guard)

// What it wrote while it runs, into j's buffer; false at its end (EOF) or an error.
static bool box_read(const BoxJob *j, int fd)
{
    char sink[4096];
    isize room = j->cap - *j->len;
    ssize_t k = room > 0 ? read(fd, j->out + *j->len, (size_t)room) : read(fd, sink, sizeof sink);
    if (k > 0 && room > 0) *j->len += k;
    return k > 0 || (k < 0 && (errno == EINTR || errno == EAGAIN));
}

// j run: in bubblewrap (BOX_NOT_RUN without it), or as it is when a test turned box_sandbox off;
// with /dev/null for its input and error, no blocked signals, the environment above, in a process
// group of its own, and killed if the thread that started it goes (ofx's end; not when j->outlive). BOX_ENDED when it
// ended within j->ms; else BOX_KILLED, with its group, and what it wrote doesn't count.
static BoxRun box_run(const BoxJob *j)
{
    Err err;
    char prog[PATH_MAX], bw[PATH_MAX];
    const char *const *argv = j->argv;
    if (!ops_program_allowed(argv[0], &err)) return BOX_NOT_RUN;
    if (!(argv[0][0] == '/' ? snprintf(prog, sizeof prog, "%s", argv[0]) < (int)sizeof prog : path_find(argv[0], prog, sizeof prog))) return BOX_NOT_RUN;
    // The environment, bubblewrap's and so the program's: these of ofx's, and GIO_USE_VFS=local (a
    // thumbnailer's %u URI read without gvfs).
    static const char *const keep[] = { "PATH", "LANG", "LC_ALL", "HOME", "GIO_USE_VFS" };
    char ebuf[COUNT(keep)][PATH_MAX];
    const char *envp[COUNT(keep) + 1];
    int ne = 0;
    for (int i = 0; i < COUNT(keep); i++) {
        const char *v = i == COUNT(keep) - 1 ? "local" : getenv(keep[i]);
        if (v && snprintf(ebuf[ne], sizeof ebuf[ne], "%s=%s", keep[i], v) < (int)sizeof ebuf[ne]) envp[ne] = ebuf[ne], ne++;
    }
    envp[ne] = NULL;
    const char *av[BOX_ARGS_MAX + 3 * BOX_BINDS_MAX + 64];
    int n = 0;
    if (box_sandbox) {
        if (!path_find("bwrap", bw, sizeof bw)) return BOX_NOT_RUN;
        static const char *const box[] = {
            "--ro-bind", "/usr", "/usr", "--ro-bind-try", "/etc/ld.so.cache", "/etc/ld.so.cache",
            "--ro-bind-try", "/etc/fonts", "/etc/fonts", "--ro-bind-try", "/var/cache/fontconfig", "/var/cache/fontconfig",   // (without them a PDF's fonts are scanned each run: 570 ms against 63)
            "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib", "/lib64", "--symlink", "usr/bin", "/bin", "--symlink", "usr/bin", "/sbin",
            "--proc", "/proc", "--dev", "/dev", "--unshare-all", "--new-session",
        };
        av[n++] = bw;
        for (int i = 0; i < COUNT(box); i++) av[n++] = box[i];
        if (!j->outlive) av[n++] = "--die-with-parent";
        for (int i = 0; i < BOX_BINDS_MAX && j->ro[i]; i++) av[n++] = "--ro-bind-try", av[n++] = j->ro[i], av[n++] = j->ro[i];
        for (int i = 0; i < 2; i++)
            if (j->rw[i]) av[n++] = "--bind", av[n++] = j->rw[i], av[n++] = j->rw[i];
        if (!path_below(prog, "/usr")) av[n++] = "--ro-bind", av[n++] = prog, av[n++] = prog;   // (a program of its own: ~/.local/bin, a test's fake)
        av[n++] = "--chdir", av[n++] = j->cwd ? j->cwd : "/";
        av[n++] = "--";
    }
    av[n++] = prog;
    for (int k = 1; argv[k] && k < BOX_ARGS_MAX; k++) av[n++] = argv[k];
    av[n] = NULL;
    int pipe_fd[2] = { -1, -1 };
    if (j->out && pipe2(pipe_fd, O_CLOEXEC) != 0) return BOX_NOT_RUN;
    if (j->out) *j->len = 0;
    const char *cwd = !box_sandbox && j->cwd ? j->cwd : NULL;
    sigset_t none;
    sigemptyset(&none);
    pid_t pid = fork();
    if (pid < 0) {
        if (j->out) close(pipe_fd[0]), close(pipe_fd[1]);
        return BOX_NOT_RUN;
    }
    if (pid == 0) {   // (only what is safe after a fork in threads, up to the exec)
        sigprocmask(SIG_SETMASK, &none, NULL);
        if (!j->outlive) prctl(PR_SET_PDEATHSIG, SIGKILL);
        setpgid(0, 0);
        int nul = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (nul >= 0) dup2(nul, 0), dup2(nul, 1), dup2(nul, 2);
        if (j->out) dup2(pipe_fd[1], 1);
        if (j->out && j->err) dup2(pipe_fd[1], 2);
        if (cwd && chdir(cwd) != 0) _exit(127);
        execve(av[0], (char *const *)av, (char *const *)envp);
        _exit(127);
    }
    if (j->out) close(pipe_fd[1]);
    // ofx's children are reaped by the kernel (spawn_init): the pidfd says when it is gone.
    int pfd = (int)syscall(SYS_pidfd_open, pid, 0), fd = j->out ? pipe_fd[0] : -1;
    if (pfd < 0 && errno != ESRCH) {   // it can't be watched (no fd left): killed, nothing kept
        kill(-pid, SIGKILL);
        if (fd >= 0) close(fd);
        return BOX_KILLED;
    }
    bool gone = pfd < 0, open_out = fd >= 0;   // (pfd < 0: gone already)
    i64 until = now_ms() + j->ms;
    while (!gone || open_out) {
        struct pollfd p[2] = { { .fd = gone ? -1 : pfd, .events = POLLIN }, { .fd = open_out ? fd : -1, .events = POLLIN } };
        i64 left = until - now_ms();
        int r = left > 0 ? poll(p, 2, (int)left) : 0;
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        if (p[0].revents) gone = true;
        if (p[1].revents && !box_read(j, fd)) open_out = false;
    }
    bool ended = gone && !open_out;
    if (!ended) {
        kill(-pid, SIGKILL);   // (its group: bubblewrap and all in its sandbox, or what the program started)
        if (!gone) poll(&(struct pollfd){ .fd = pfd, .events = POLLIN }, 1, 1000);
    }
    if (pfd >= 0) close(pfd);
    if (fd >= 0) close(fd);
    return ended ? BOX_ENDED : BOX_KILLED;
}
