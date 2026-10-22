// F5.3's unit tests: which waiting job starts, the throughput line and speed, and the job rows'
// keys and panels. Included by test.c.

static void test_jobq(void)
{
    CHECK(jobq_nested(S("/a/b"), S("/a/b")));
    CHECK(jobq_nested(S("/a/b"), S("/a/b/c")) && jobq_nested(S("/a/b/c"), S("/a/b")));
    CHECK(!jobq_nested(S("/a/b"), S("/a/bc")) && !jobq_nested(S("/a/bc"), S("/a/b")));
    CHECK(jobq_nested(S("/a/b/"), S("/a/b")) && jobq_nested(S("/"), S("/x")));
    CHECK(jobq_nested(S("/a/b/"), S("/a/b/c")) && jobq_nested(S("/a/b/c"), S("/a/b/")));
    CHECK(!jobq_nested(S("/a"), S("/b")));

    const char *pa[] = { "/s/one", "/d" }, *pb[] = { "/s/two", "/e" }, *pc[] = { "/s/one/in", "/f" }, *pd[] = { "/g", "/d/sub" };
    JobUses a = { pa, 2 }, b = { pb, 2 }, c = { pc, 2 }, d = { pd, 2 };
    CHECK(!jobq_overlap(&a, &b) && jobq_overlap(&a, &c) && jobq_overlap(&c, &a) && jobq_overlap(&a, &d) && !jobq_overlap(&b, &d));

    // Free workers, nothing in the way: the first waiting.
    const JobUses *w[4], *r[4];
    w[0] = &b, w[1] = &c;
    CHECK(jobq_next(w, 2, r, 0) == 0);
    // The first overlaps a running job: the next that doesn't, and doesn't overlap the first.
    r[0] = &a;
    w[0] = &c, w[1] = &b;
    CHECK(jobq_next(w, 2, r, 1) == 1);
    // A later one that overlaps an earlier waiting one waits behind it.
    w[0] = &c, w[1] = &d;   // both overlap a
    CHECK(jobq_next(w, 2, r, 1) == -1);
    const char *pe[] = { "/f/x" };
    JobUses e = { pe, 1 };   // overlaps c (waiting, held by a), not a
    w[0] = &c, w[1] = &e;
    CHECK(jobq_next(w, 2, r, 1) == -1);
    w[0] = &c, w[1] = &e, w[2] = &b;
    CHECK(jobq_next(w, 3, r, 1) == 2);
    // All workers busy: none.
    r[0] = &a, r[1] = &a, r[2] = &a, r[3] = &a;
    w[0] = &b;
    CHECK(jobq_next(w, 1, r, JOBQ_WORKERS) == -1);
    CHECK(jobq_next(w, 1, r, JOBQ_WORKERS - 1) == 0);   // one worker free
    CHECK(jobq_next(w, 0, r, 0) == -1);
}

static void test_jobline(void)
{
    static JobLine l;
    memset(&l, 0, sizeof l);
    // 1 MB a second for 2 s, 10 MB in all: the speed, the points reached so far.
    for (int t = 0; t <= 2000; t += 100) jobline_sample(&l, t, (i64)t * 1000, 10000000);
    CHECK(l.now > 999000 && l.now < 1001000);
    CHECK(l.reached == 60);   // 2 MB of 10: a fifth of 300
    CHECK(l.speed[0] > 999000 && l.speed[59] > 999000);
    CHECK(l.max == l.now);
    // Then faster: 4 MB a second; the window is 2 s, so it climbs over it.
    for (int t = 2100; t <= 3000; t += 100) jobline_sample(&l, t, 2000000 + (i64)(t - 2000) * 4000, 10000000);
    CHECK(l.now > 2400000 && l.now < 2600000);   // (6 MB - 1 MB) / 2 s
    for (int t = 3100; t <= 4000; t += 100) jobline_sample(&l, t, 2000000 + (i64)(t - 2000) * 4000, 10000000);
    CHECK(l.now > 3999000 && l.now < 4001000);
    CHECK(l.max == l.now && l.reached == 300);   // 10 MB: all of it
    // Never past the end; never backwards.
    jobline_sample(&l, 4100, 20000000, 10000000);
    CHECK(l.reached == 300);
    float before = l.now;
    jobline_sample(&l, 4200, 5, 10000000);
    CHECK(l.now > 0 && l.max >= before);   // a count that went back reads as the last one
    // Samples closer than 50 ms keep the ring; no total: no points. No speed over less than
    // half a second (a first block from the page cache), then the points reached take it.
    memset(&l, 0, sizeof l);
    jobline_sample(&l, 0, 0, 100000000);
    jobline_sample(&l, 10, 5000000, 100000000);   // 5 MB in 10 ms: not a speed
    jobline_sample(&l, 20, 5000000, 100000000);
    CHECK(l.ns == 1 && l.now == 0 && l.reached == 15 && l.speed[14] == 0);
    for (int t = 100; t <= 500; t += 100) jobline_sample(&l, t, 5000000 + (i64)t * 1000, 100000000);
    CHECK(l.now > 10900000 && l.now < 11100000 && l.speed[0] == l.now && l.speed[14] == l.now);   // 5.5 MB in 0.5 s
    memset(&l, 0, sizeof l);
    jobline_sample(&l, 0, 0, 0);
    jobline_sample(&l, 600, 6000, 0);
    CHECK(l.reached == 0 && l.now > 9999 && l.now < 10001);
    // The ring wraps (64 samples) and the speed still reads the last 2 s.
    memset(&l, 0, sizeof l);
    for (int t = 0; t <= 10000; t += 50) jobline_sample(&l, t, (i64)t * 500, 0);
    CHECK(l.ns == JOBQ_SAMPLES && l.now > 499000 && l.now < 501000);

    char b[32];
    jobq_speed(b, sizeof b, 0);
    CHECK(strcmp(b, "") == 0);
    jobq_speed(b, sizeof b, 512);
    CHECK(strcmp(b, "512 B/s") == 0);
    jobq_speed(b, sizeof b, 12400000);
    CHECK(strcmp(b, "12.4 MB/s") == 0);
    jobq_speed(b, sizeof b, 250000000);
    CHECK(strcmp(b, "250 MB/s") == 0);
    jobq_speed(b, sizeof b, 3.2e12f);
    CHECK(strcmp(b, "3200 GB/s") == 0);
    jobq_speed(b, sizeof b, 1500);
    CHECK(strcmp(b, "1.5 kB/s") == 0);
}

// An input with n job rows (ids 10, 11, ...), all jobs counted as all.
static UiInput job_input(int n, int all)
{
    UiInput in = ui_input_none();
    in.job_n = n, in.jobs_all = all;
    for (int i = 0; i < n; i++) in.job_id[i] = 10 + i;
    return in;
}

static void job_keys(Ui *u, UiNames names, UiInput in, int n, ...)
{
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) in.ev[in.nev++] = (UiEvent){ (UiKey)va_arg(ap, int), 0 };
    va_end(ap);
    ui_handle(u, names, &in);
}

static void test_job_keys(void)
{
    UiNames names = { name_at, NULL };
    static Ui u;
    memset(&u, 0, sizeof u);
    ui_resize(&u.list, 5);
    ui_set(&u.list, 20, 3);
    ui_resize(&u.side, 5);
    ui_set(&u.side, 3, 0);
    // No rows: Ctrl+J does nothing.
    job_keys(&u, names, job_input(0, 0), 1, UK_JOBS);
    CHECK(!u.jobs_focus);
    // Ctrl+J: the top row marked; Down, Up, j, k move it, the ends kept; the list doesn't move.
    job_keys(&u, names, job_input(3, 3), 1, UK_JOBS);
    CHECK(u.jobs_focus && u.job_at == 0);
    job_keys(&u, names, job_input(3, 3), 1, UK_DOWN);
    CHECK(u.job_at == 1);
    UiInput in = job_input(3, 3);
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 'j' }, in.ev[in.nev++] = (UiEvent){ UK_NONE, 'j' };
    ui_handle(&u, names, &in);
    CHECK(u.job_at == 2);
    in = job_input(3, 3), in.ev[in.nev++] = (UiEvent){ UK_NONE, 'k' };
    ui_handle(&u, names, &in);
    CHECK(u.job_at == 1);
    job_keys(&u, names, job_input(3, 3), 2, UK_UP, UK_JOBS);   // Ctrl+J again keeps the mark
    CHECK(u.job_at == 0 && u.jobs_focus && u.list.sel == 3);
    job_keys(&u, names, job_input(3, 3), 2, UK_DOWN, UK_JOBS);
    CHECK(u.job_at == 1 && u.jobs_focus);
    job_keys(&u, names, job_input(3, 3), 2, UK_ESC, UK_JOBS);   // given back and taken again: the top row
    CHECK(u.job_at == 0 && u.jobs_focus);
    job_keys(&u, names, job_input(3, 3), 2, UK_UP, UK_ENTER);   // at the top already: the top row's job
    CHECK(u.job_at == 0 && u.cancel_asking && u.cancel_id == 10);
    u.cancel_asking = false;
    // Enter asks about the marked job by its id; Esc keeps it going, Enter or y cancels it.
    job_keys(&u, names, job_input(3, 3), 2, UK_DOWN, UK_ENTER);
    CHECK(u.cancel_asking && u.cancel_id == 11);
    job_keys(&u, names, job_input(3, 3), 1, UK_ESC);
    CHECK(!u.cancel_asking && u.jobs_focus);
    job_keys(&u, names, job_input(3, 3), 1, UK_ENTER);
    in = job_input(3, 3), in.ev[in.nev++] = (UiEvent){ UK_DOWN, 0 };   // other keys: ignored while asked
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.cancel_asking && u.job_at == 1);
    in = job_input(3, 3), in.ev[in.nev++] = (UiEvent){ UK_NONE, 'y' };
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_CANCEL_JOB && a.index == 11 && !u.cancel_asking);
    job_keys(&u, names, job_input(3, 3), 1, UK_ENTER);
    in = job_input(3, 3), in.ev[in.nev++] = (UiEvent){ UK_NONE, 'n' };
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.cancel_asking);
    job_keys(&u, names, job_input(3, 3), 1, UK_ENTER);
    in = job_input(3, 3), in.ev[in.nev++] = (UiEvent){ UK_ENTER, 0 };
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_CANCEL_JOB && a.index == 11);
    // The panel's buttons.
    job_keys(&u, names, job_input(3, 3), 1, UK_ENTER);
    in = job_input(3, 3), in.modal = UI_MODAL_NO;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.cancel_asking);
    job_keys(&u, names, job_input(3, 3), 1, UK_ENTER);
    in = job_input(3, 3), in.modal = UI_MODAL_YES;
    a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_CANCEL_JOB && a.index == 11);
    // Fewer rows: the mark moves up; none: the keyboard goes back.
    job_keys(&u, names, job_input(3, 3), 1, UK_DOWN);
    CHECK(u.job_at == 2);
    in = job_input(1, 1);
    ui_handle(&u, names, &in);
    CHECK(u.jobs_focus && u.job_at == 0);
    in = job_input(0, 0);
    ui_handle(&u, names, &in);
    CHECK(!u.jobs_focus);
    // Esc and Tab give the keyboard back and do nothing else; another key is the list's.
    job_keys(&u, names, job_input(2, 2), 2, UK_JOBS, UK_ESC);
    CHECK(!u.jobs_focus);
    job_keys(&u, names, job_input(2, 2), 2, UK_JOBS, UK_TAB);
    CHECK(!u.jobs_focus && !u.side_focus);
    job_keys(&u, names, job_input(2, 2), 2, UK_JOBS, UK_PAGEDOWN);
    CHECK(!u.jobs_focus && u.list.sel == 7);
    // A click gives it back.
    job_keys(&u, names, job_input(2, 2), 1, UK_JOBS);
    in = job_input(2, 2), in.click = 0;
    ui_handle(&u, names, &in);
    CHECK(!u.jobs_focus);
    // Ctrl+K and an edit line take it; Ctrl+J takes it from the path buttons and the sidebar.
    job_keys(&u, names, job_input(2, 2), 1, UK_JOBS);
    in = job_input(2, 2), in.ev[in.nev++] = (UiEvent){ UK_CRUMBS, 0 }, in.crumb_n = 3, in.crumb_last = 2;
    ui_handle(&u, names, &in);
    CHECK(!u.jobs_focus && u.crumb_focus);
    in = job_input(2, 2), in.ev[in.nev++] = (UiEvent){ UK_JOBS, 0 }, in.crumb_n = 3;
    ui_handle(&u, names, &in);
    CHECK(u.jobs_focus && !u.crumb_focus);
    ui_edit_open(&u, S("x"));
    CHECK(!u.jobs_focus);
    ui_edit_close(&u);
    u.side_focus = true;
    job_keys(&u, names, job_input(2, 2), 1, UK_JOBS);
    CHECK(u.jobs_focus && !u.side_focus);
    job_keys(&u, names, job_input(2, 2), 1, UK_ESC);
    // A row's × clicked asks about that job; not while another panel is up, not past the rows.
    in = job_input(2, 2), in.job_x = 1;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.cancel_asking && u.cancel_id == 11);
    u.cancel_asking = false;
    in = job_input(2, 2), in.job_x = 2;
    ui_handle(&u, names, &in);
    CHECK(!u.cancel_asking);
    u.confirming = true;
    in = job_input(2, 2), in.job_x = 0;
    ui_handle(&u, names, &in);
    CHECK(!u.cancel_asking && u.confirming);
    u.confirming = false;
    // Esc in the list never cancels a job.
    in = job_input(2, 2), in.ev[in.nev++] = (UiEvent){ UK_ESC, 0 };
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.cancel_asking);
    // A clash panel up: the job keys wait, with the rows' keyboard or without.
    u.asking = true;
    job_keys(&u, names, job_input(2, 2), 1, UK_JOBS);
    CHECK(!u.jobs_focus && u.asking);
    u.asking = false;
    job_keys(&u, names, job_input(2, 2), 1, UK_JOBS);
    u.asking = true;
    job_keys(&u, names, job_input(2, 2), 2, UK_DOWN, UK_ENTER);
    CHECK(u.job_at == 0 && !u.cancel_asking && u.asking);
    u.asking = false, u.jobs_focus = false;
}

static void test_quit_full(void)
{
    UiNames names = { name_at, NULL };
    static Ui u;
    memset(&u, 0, sizeof u);
    ui_resize(&u.list, 5);
    ui_set(&u.list, 20, 0);
    // No jobs: Ctrl+Q quits at once.
    UiInput in = job_input(0, 0);
    in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 };
    CHECK(ui_handle(&u, names, &in).kind == UA_QUIT);
    // Jobs running or waiting: asked first. n and Esc stay, Enter and y quit, the buttons too.
    in = job_input(1, 3), in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 };
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.quit_asking);
    in = job_input(1, 3), in.ev[in.nev++] = (UiEvent){ UK_NONE, 'x' };
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.quit_asking);
    in = job_input(1, 3), in.ev[in.nev++] = (UiEvent){ UK_ESC, 0 };
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.quit_asking);
    in = job_input(0, 2), in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 };   // only waiting ones: asked too
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.quit_asking);
    u.quit_asking = false;
    in = job_input(0, 2), in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 }, in.ev[in.nev++] = (UiEvent){ UK_NONE, 'y' };
    CHECK(ui_handle(&u, names, &in).kind == UA_QUIT && !u.quit_asking);
    in = job_input(1, 1), in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 }, in.ev[in.nev++] = (UiEvent){ UK_ENTER, 0 };
    CHECK(ui_handle(&u, names, &in).kind == UA_QUIT);
    in = job_input(1, 1), in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 }, in.ev[in.nev++] = (UiEvent){ UK_NONE, 'n' };
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.quit_asking);
    in = job_input(1, 1), in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 };
    ui_handle(&u, names, &in);
    in = job_input(1, 1), in.modal = UI_MODAL_NO;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.quit_asking);
    in = job_input(1, 1), in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 };
    ui_handle(&u, names, &in);
    in = job_input(1, 1), in.modal = UI_MODAL_YES;
    CHECK(ui_handle(&u, names, &in).kind == UA_QUIT);
    // Ctrl+Q over the cancel question or a clash: the quit question takes over.
    u.cancel_asking = true, u.cancel_id = 10;
    in = job_input(1, 1), in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 };
    ui_handle(&u, names, &in);
    CHECK(u.quit_asking && !u.cancel_asking);
    in = job_input(1, 1), in.ev[in.nev++] = (UiEvent){ UK_ESC, 0 };
    ui_handle(&u, names, &in);
    u.asking = true;
    in = job_input(1, 1), in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 }, in.ev[in.nev++] = (UiEvent){ UK_NONE, 'n' };
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.asking && !u.quit_asking);
    u.asking = false;
    // The queue full: any key closes the note and does nothing else; so does OK.
    u.full_told = true;
    in = job_input(1, 1), in.ev[in.nev++] = (UiEvent){ UK_DOWN, 0 };
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.full_told && u.list.sel == 0);
    u.full_told = true;
    in = job_input(1, 1), in.modal = UI_MODAL_OK;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.full_told);
    u.full_told = true;
    in = job_input(1, 1), in.ev[in.nev++] = (UiEvent){ UK_QUIT, 0 };
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.full_told && !u.quit_asking);
    u.full_told = true;
    in = job_input(1, 1), in.click = 2;   // a click doesn't reach the list under it
    ui_handle(&u, names, &in);
    CHECK(u.full_told && u.list.sel == 0);
    u.full_told = false;
}

static void test_ui_f53(void)
{
    test_jobq();
    test_jobline();
    test_job_keys();
    test_quit_full();
}
