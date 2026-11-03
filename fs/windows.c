// The ofx windows open (F7.1, docs/plan.md "F7 design"): each is its own process (New Window), so
// Close All Windows finds the others here. Each window writes <dir>/<pid> at start, holding its
// start time as /proc/<pid>/stat has it, and removes it when it ends; one left by a crash is
// removed when found dead, and a pid used again since is told apart by that start time. The
// folder is $XDG_RUNTIME_DIR/ofx, or OFX_TEST_WINDOWS for the GUI tests (run-script.sh), and with
// the test guard on (OFX_GUI_ROOT) nothing outside it is signalled: a test can never close
// Michael's windows.

#include "../base/base.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

// OFX_TEST_WINDOWS, else $XDG_RUNTIME_DIR/ofx; false when neither is set.
static bool win_dir(char *out, size_t cap)
{
    const char *t = getenv("OFX_TEST_WINDOWS"), *r = getenv("XDG_RUNTIME_DIR");
    int n;
    if (t && t[0] == '/') n = snprintf(out, cap, "%s", t);
    else if (r && r[0] == '/') n = snprintf(out, cap, "%s/ofx", r);
    else return false;
    return n >= 0 && (size_t)n < cap;
}

// Process pid's start time (/proc/<pid>/stat's 22nd field, in clock ticks since boot); -1 when
// there is no such process.
static i64 win_started(pid_t pid)
{
    char p[64], buf[1024];
    snprintf(p, sizeof p, "/proc/%d/stat", (int)pid);
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    // The name, the 2nd field, is in parentheses and may hold spaces and ')': count from the last.
    char *s = strrchr(buf, ')');
    if (!s) return -1;
    s++;
    for (int field = 3; field < 22; field++) {
        s = strchr(s + 1, ' ');
        if (!s) return -1;
    }
    char *end;
    long long v = strtoll(s + 1, &end, 10);
    return end == s + 1 ? -1 : (i64)v;
}

// This window, pid, into the folder dir (made when missing).
static bool win_register(const char *dir, pid_t pid, Err *err)
{
    char p[PATH_MAX], body[32];
    i64 t = win_started(pid);
    if (t < 0) return err_set(err, "no start time for process %d", (int)pid), false;
    if (snprintf(p, sizeof p, "%s/%d", dir, (int)pid) >= (int)sizeof p) return err_set(err, "%s: too long", dir), false;
    if (!ops_allowed(p, err) || !mkdir_private(dir, err)) return false;
    int n = snprintf(body, sizeof body, "%lld\n", (long long)t);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return err_set(err, "%s: %s", p, strerror(errno)), false;
    bool ok = write(fd, body, (size_t)n) == n;
    ok &= close(fd) == 0;
    if (!ok) return unlink(p), err_set(err, "%s: cannot write it", p), false;
    return true;
}

static void win_unregister(const char *dir, pid_t pid)
{
    char p[PATH_MAX];
    Err err;
    if (snprintf(p, sizeof p, "%s/%d", dir, (int)pid) < (int)sizeof p && ops_allowed(p, &err)) unlink(p);
}

// Whether name is an entry's: a pid as win_register writes it, digits without a leading 0.
static bool win_name(const char *name)
{
    if (name[0] < '1' || name[0] > '9') return false;
    size_t i = 1;
    while (name[i] >= '0' && name[i] <= '9') i++;
    return name[i] == 0 && i <= 10;
}

// An entry's body as win_register writes it, digits then a newline, into *t.
static bool win_body(const char *buf, ssize_t n, i64 *t)
{
    if (n < 2 || buf[n - 1] != '\n') return false;
    i64 v = 0;
    for (ssize_t i = 0; i < n - 1; i++) {
        if (buf[i] < '0' || buf[i] > '9' || v > INT64_MAX / 10 - 1) return false;
        v = v * 10 + (buf[i] - '0');
    }
    return *t = v, true;
}

// Sends sig to every window listed in dir but self that is still the process it was; an entry
// whose process is gone, or is another one now, is removed. Only names and bodies as win_register
// writes them, and regular files (a FIFO would block the read: F7.1 review). The
// signal goes through a pidfd taken before the start time is checked, so a pid freed and used
// again in between can't get it. The number signalled.
static int win_signal_others(const char *dir, pid_t self, int sig)
{
    Err err;
    if (!ops_allowed(dir, &err)) return 0;   // the test guard: never a folder outside the run's
    DIR *d = opendir(dir);
    if (!d) return 0;
    int told = 0;
    for (struct dirent *e; (e = readdir(d));) {
        if (!win_name(e->d_name)) continue;
        long pid = strtol(e->d_name, NULL, 10);
        if (pid > INT32_MAX || (pid_t)pid == self) continue;
        char p[PATH_MAX], buf[32];
        if (!path_put(p, sizeof p, dir, str_from_c(e->d_name))) continue;
        isize n = file_read_buf(p, buf, sizeof buf);
        if (n < 0) continue;   // not a regular file: not ofx's
        i64 want;
        bool whole = win_body(buf, n, &want);
        int pfd = (int)syscall(SYS_pidfd_open, (pid_t)pid, 0);
        if (pfd < 0) {
            if (errno == ESRCH) unlink(p);   // gone
            continue;
        }
        i64 now = win_started((pid_t)pid);   // the pidfd's process, or one that took the pid since
        if (now < 0 || (whole && now != want)) {
            unlink(p);   // gone, or the pid is another process's now
        } else if (whole && syscall(SYS_pidfd_send_signal, pfd, sig, NULL, 0) == 0) {
            told++;
        }   // not whole: still being written
        close(pfd);
    }
    closedir(d);
    return told;
}
