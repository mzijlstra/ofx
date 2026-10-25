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

// Sends sig to every window listed in dir but self that is still the process it was; an entry
// whose process is gone, or is another one now, is removed. The number signalled.
static int win_signal_others(const char *dir, pid_t self, int sig)
{
    Err err;
    if (!ops_allowed(dir, &err)) return 0;   // the test guard: never a folder outside the run's
    DIR *d = opendir(dir);
    if (!d) return 0;
    int told = 0;
    for (struct dirent *e; (e = readdir(d));) {
        char *end;
        long pid = strtol(e->d_name, &end, 10);
        if (*end || end == e->d_name || pid <= 0 || pid > INT32_MAX || (pid_t)pid == self) continue;
        char p[PATH_MAX], buf[32];
        if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
        int fd = open(p, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) continue;
        ssize_t n = read(fd, buf, sizeof buf - 1);
        close(fd);
        buf[n > 0 ? n : 0] = 0;
        long long want = strtoll(buf, &end, 10);
        i64 now = win_started((pid_t)pid);
        if (now < 0 || (n > 0 && end != buf && now != (i64)want)) {
            unlink(p);   // gone, or the pid is another process's now
            continue;
        }
        if (n <= 0 || end == buf) continue;   // still being written
        if (kill((pid_t)pid, sig) == 0) told++;
    }
    closedir(d);
    return told;
}
