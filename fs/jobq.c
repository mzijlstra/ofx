// The jobs' queue and their progress rows' numbers (docs/plan.md, F5.3): which waiting job may
// start, and the throughput line and speed a row draws. raylib-free and unit-tested; gui/jobs.c
// runs the workers.

#include "../base/base.h"

#include <stdio.h>

enum { JOBQ_WORKERS = 4, JOBQ_WAITING = 32, JOBQ_POINTS = 300, JOBQ_SAMPLES = 64, JOBQ_SPEED_MS = 2000, JOBQ_SPAN_MS = 500 };

// The paths a job uses: its sources and its targets' folders.
typedef struct {
    const char *const *path;
    isize              n;
} JobUses;

// a and b are the same path or one is under the other.
static bool jobq_nested(Str a, Str b)
{
    while (a.len > 1 && a.p[a.len - 1] == '/') a.len--;
    while (b.len > 1 && b.p[b.len - 1] == '/') b.len--;
    Str s = a.len <= b.len ? a : b, l = a.len <= b.len ? b : a;
    if (!str_starts_with(l, s)) return false;
    return l.len == s.len || str_eq(s, S("/")) || l.p[s.len] == '/';
}

static bool jobq_overlap(const JobUses *a, const JobUses *b)
{
    for (isize i = 0; i < a->n; i++)
        for (isize k = 0; k < b->n; k++)
            if (jobq_nested(str_from_c(a->path[i]), str_from_c(b->path[k]))) return true;
    return false;
}

// Which of the waiting jobs (in the order they came) starts now: the first that overlaps no
// running job and no waiting job before it, while a worker is free; -1 for none. A job held back
// keeps the ones after it that touch the same files behind it, so they run in the order asked.
static int jobq_next(const JobUses *const *wait, int nwait, const JobUses *const *run, int nrun)
{
    if (nrun >= JOBQ_WORKERS) return -1;
    for (int w = 0; w < nwait; w++) {
        bool free = true;
        for (int r = 0; free && r < nrun; r++) free = !jobq_overlap(wait[w], run[r]);
        for (int e = 0; free && e < w; e++) free = !jobq_overlap(wait[w], wait[e]);
        if (free) return w;
    }
    return -1;
}

// A row's throughput line: x the progress (JOBQ_POINTS steps), y the speed when the job got
// there; the speed smoothed over the last JOBQ_SPEED_MS.
typedef struct {
    float speed[JOBQ_POINTS];   // bytes a second; 0 where the job hasn't been
    int   reached;              // points filled, 0..JOBQ_POINTS
    float max;                  // the fastest so far, for the line's scale
    float now;                  // the current speed
    struct { i64 ms, bytes; } s[JOBQ_SAMPLES];   // a ring of recent samples
    int   ns, at;
} JobLine;

// The job has done `done` of `total` bytes at time ms.
// Samples closer than 50 ms apart move the line only, so the ring spans the whole window.
static void jobline_sample(JobLine *l, i64 ms, i64 done, i64 total)
{
    int last = (l->at + JOBQ_SAMPLES - 1) % JOBQ_SAMPLES;
    if (l->ns == 0 || ms - l->s[last].ms >= 50) {
        l->s[l->at].ms = ms, l->s[l->at].bytes = done;
        l->at = (l->at + 1) % JOBQ_SAMPLES;
        if (l->ns < JOBQ_SAMPLES) l->ns++;
    }
    // The oldest sample within the window (or the oldest kept) against this one.
    int newest = (l->at + JOBQ_SAMPLES - 1) % JOBQ_SAMPLES, from = newest;
    done = done > l->s[newest].bytes ? done : l->s[newest].bytes;   // never back
    for (int k = 1; k < l->ns; k++) {
        int i = (newest - k + JOBQ_SAMPLES) % JOBQ_SAMPLES;
        if (ms - l->s[i].ms > JOBQ_SPEED_MS) break;
        from = i;
    }
    // Not over less than JOBQ_SPAN_MS: a job's first block lands at once (the page cache), and a
    // speed read off it would set the line's scale for good.
    i64 dt = ms - l->s[from].ms;
    bool first = l->now <= 0;
    if (dt >= JOBQ_SPAN_MS) l->now = (float)(done - l->s[from].bytes) * 1000.0f / (float)dt;
    if (l->now < 0) l->now = 0;
    if (l->now > l->max) l->max = l->now;
    // The points reached before there was a speed take the first one.
    if (first && l->now > 0)
        for (int k = 0; k < l->reached; k++) l->speed[k] = l->now;
    int to = total > 0 ? (int)((double)done / (double)total * JOBQ_POINTS) : 0;
    if (to > JOBQ_POINTS) to = JOBQ_POINTS;
    while (l->reached < to) l->speed[l->reached++] = l->now;
}

// A speed for the row's right end: "12.4 MB/s"; "" before there is one.
static void jobq_speed(char *buf, size_t cap, float bps)
{
    static const char *const U[] = { "B/s", "kB/s", "MB/s", "GB/s" };
    int u = 0;
    double v = bps;
    while (v >= 1000 && u < 3) v /= 1000, u++;
    if (bps <= 0) snprintf(buf, cap, "%s", "");
    else if (u == 0 || v >= 100) snprintf(buf, cap, "%.0f %s", v, U[u]);
    else snprintf(buf, cap, "%.1f %s", v, U[u]);
}
