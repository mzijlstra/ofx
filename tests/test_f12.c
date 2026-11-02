// F12 (docs/plan.md "F12 design"): shift-select's range mode (step 2) and its ofx.conf line.
// Included by test.c after test_f11.c.

// shift-select (F12): range, the default, or add; anything else is a bad line.
static void test_conf_shift(void)
{
    static Conf c;
    conf_defaults(&c);
    CHECK(!c.shift_add);
    conf_parse(S("shift-select = add\n"), &c);
    CHECK(c.shift_add && !c.bad);
    conf_parse(S("shift-select = range\n"), &c);
    CHECK(!c.shift_add && !c.bad);
    conf_parse(S("shift-select = Add\n"), &c);
    CHECK(!c.shift_add && c.bad == 1 && strcmp(c.why, "shift-select wants range or add") == 0);
}

// shift-select = range (F12, the default; Nemo's and GTK's): Shift marks exactly the anchor (where
// the last plain move or click left the cursor) to the cursor, in place of the other marks.
static void test_shift_range(void)
{
    UiNames names = { name_at, NULL, NULL };
    static u8 mark[20];
    Ui u = { 0 };
    ui_resize(&u.list, 10);
    ui_mark_set(&u, mark);
    ui_set(&u.list, 20, 2);
    CHECK(u.anchor == -1 && !u.shift_add);
    // No anchor yet: the first Shift starts at the cursor. Back again, the range shrinks.
    UiInput in = keys(2, UK_SEL_DOWN, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4 && u.marked == 3 && mark[2] && mark[3] && mark[4] && u.anchor == 2);
    in = keys(1, UK_SEL_UP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 3 && u.marked == 2 && !mark[4]);
    in = chars("KK");   // past the anchor: the other way
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.marked == 2 && mark[1] && mark[2] && !mark[3]);
    // A plain move keeps the marks and drops the anchor; the next Shift's range, from there,
    // replaces them.
    in = keys(3, UK_DOWN, UK_DOWN, UK_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 4 && u.marked == 2 && u.anchor == -1);
    in = chars("J");
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 5 && u.marked == 2 && mark[4] && mark[5] && !mark[1]);
    in = keys(1, UK_SEL_END);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 19 && u.marked == 16 && mark[4] && !mark[3]);
    in = keys(1, UK_SEL_HOME);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 0 && u.marked == 5 && mark[0] && mark[4] && !mark[5]);
    // Keys that don't move the cursor leave the anchor (a copy between two Shift moves).
    in = keys(1, UK_COPY);
    CHECK(ui_handle(&u, names, &in).kind == UA_COPY);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.marked == 4 && mark[1] && !mark[0] && u.anchor == 4);
    // A key that moves nothing, in the frame of a Shift move, keeps the anchor for the next.
    in = keys(1, UK_SEL_DOWN);
    in.ev[in.nev++] = (UiEvent){ UK_NONE, 'x' };
    ui_handle(&u, names, &in);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 3 && u.marked == 2 && mark[3] && mark[4] && u.anchor == 4);
    in = keys(2, UK_SEL_UP, UK_SEL_UP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.marked == 4);
    // Clicks: a plain one moves the anchor, keeping the marks; Shift+click the anchor's range in
    // place of them; Ctrl+Shift+click adds it; Ctrl+click toggles and moves the anchor.
    in = ui_input_none(), in.click = 3;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 3 && u.marked == 4 && u.anchor == -1);
    in = ui_input_none(), in.click = 5, in.click_mods = UI_MOD_SHIFT;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 5 && u.marked == 3 && mark[3] && mark[5] && !mark[1] && u.anchor == 3);
    in = ui_input_none(), in.click = 1, in.click_mods = UI_MOD_SHIFT;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.marked == 3 && mark[1] && mark[3] && !mark[4]);
    in = ui_input_none(), in.click = 8, in.click_mods = UI_MOD_SHIFT | UI_MOD_CTRL;
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 8 && u.marked == 8 && mark[1] && mark[8] && !mark[9] && u.anchor == 3);
    in = ui_input_none(), in.click = 9, in.click_mods = UI_MOD_CTRL;
    ui_handle(&u, names, &in);
    CHECK(u.marked == 9 && mark[9] && u.anchor == -1);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 10 && u.marked == 2 && mark[9] && mark[10]);
    // A plain move back onto where the range ended still drops the anchor.
    in = keys(2, UK_DOWN, UK_UP);
    ui_handle(&u, names, &in);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 11 && u.marked == 2 && mark[10] && mark[11] && !mark[9]);
    // The cursor moved by something other than the keys (a find, the pointer, gui/): the next
    // Shift starts there.
    u.list.sel = 14;
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 15 && u.marked == 2 && mark[14] && mark[15] && u.anchor == 14);
    u.list.sel = 12;
    in = ui_input_none(), in.click = (int)(13 - u.list.top), in.click_mods = UI_MOD_SHIFT;   // (a row on screen)
    ui_handle(&u, names, &in);
    CHECK(u.marked == 2 && mark[12] && mark[13] && u.anchor == 12);
    // A new listing forgets the anchor, and one past the list counts as none.
    ui_mark_set(&u, mark);
    memset(mark, 0, sizeof mark);
    ui_set(&u.list, 20, 6);
    in = keys(1, UK_SEL_UP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 5 && u.marked == 2 && mark[5] && mark[6]);
    u.anchor = 10, u.range_end = 2;   // (just past the list, from where the last range ended)
    ui_set(&u.list, 10, 2);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 3 && u.marked == 2 && mark[2] && mark[3] && u.anchor == 2);
    in = ui_input_none(), in.click = 7, in.click_mods = UI_MOD_SHIFT;   // (from the cursor, 3)
    u.anchor = 15;
    ui_handle(&u, names, &in);
    CHECK(u.marked == 5 && mark[3] && mark[7] && !mark[2] && u.anchor == 3);
    // The icon view: a row down marks the cells between in the list's order, and back up only the
    // anchor's.
    ui_mark_set(&u, mark);
    memset(mark, 0, sizeof mark);
    u.grid = true;
    ui_set_cols(&u.list, 4);
    ui_set(&u.list, 20, 1);
    in = keys(1, UK_SEL_DOWN);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 5 && u.marked == 5 && mark[1] && mark[5]);
    in = keys(1, UK_SEL_UP);
    ui_handle(&u, names, &in);
    CHECK(u.list.sel == 1 && u.marked == 1 && mark[1]);
}

// ---- the system's thumbnailers (F12 step 3) ----------------------------------------------------

static u8 thumbers_test_mem[MiB(24)];

// text at path, executable: a test's own fake thumbnailer.
static void put_exec(const char *path, const char *text)
{
    put(path, text);
    chmod(path, 0755);
}

// A .thumbnailer file at path from its lines.
static void put_thumber(const char *path, const char *lines)
{
    char text[2048];
    snprintf(text, sizeof text, "%s", lines);
    put(path, text);
}

// The .thumbnailer files read: a type's first one wins (by file name, the user's data dir first);
// one whose program isn't installed, without a type, or in another group is skipped; with the test
// guard on, only OFX_TEST_THUMBNAILERS's.
static void test_thumbers(void)
{
    static Thumbers t;
    Arena a;
    arena_init(&a, "thumbers test", thumbers_test_mem, sizeof thumbers_test_mem);
    ops_guard(root);
    char fake[PATH_MAX], dir[PATH_MAX], text[3 * PATH_MAX];
    snprintf(fake, sizeof fake, "%s", at("f12/bin/fake"));
    snprintf(dir, sizeof dir, "%s", at("f12/tn"));
    put_exec(fake, "#!/bin/sh\n");
    snprintf(text, sizeof text, "[Thumbnailer Entry]\nTryExec=%s\nExec=%s -a %%i %%o %%s\nMimeType=video/mp4;;application/pdf;\n", fake, fake);
    put_thumber(at("f12/tn/a.thumbnailer"), text);
    snprintf(text, sizeof text, "[Thumbnailer Entry]\nTryExec=ofx-no-such-thumbnailer\nExec=%s %%i %%o\nMimeType=video/webm;\n", fake);   // (TryExec, not Exec)
    put_thumber(at("f12/tn/b.thumbnailer"), text);
    snprintf(text, sizeof text, "[Thumbnailer Entry]\nExec=%s -c %%u %%o\nMimeType=application/pdf;text/x-foo;\n", fake);
    put_thumber(at("f12/tn/c.thumbnailer"), text);
    snprintf(text, sizeof text, "[Thumbnailer Entry]\nExec=%s %%i %%o\n", fake);
    put_thumber(at("f12/tn/d.thumbnailer"), text);
    snprintf(text, sizeof text, "[Other]\nExec=%s %%i %%o\nMimeType=video/ogg;\n", fake);
    put_thumber(at("f12/tn/e.thumbnailer"), text);
    put_thumber(at("f12/tn/f.thumbnailer"), "[Thumbnailer Entry]\nExec=/nonexistent/prog %i %o\nMimeType=video/x-f;\n");
    snprintf(text, sizeof text, "[Thumbnailer Entry]\nTryExec=%s\nMimeType=video/x-noexec;\n", fake);
    put_thumber(at("f12/tn/g.thumbnailer"), text);
    snprintf(text, sizeof text, "[Thumbnailer Entry]\nExec=%s %%i %%o\nMimeType=image/x-notes;\n", fake);
    put_thumber(at("f12/tn/notes.txt"), text);
    env_set("OFX_TEST_THUMBNAILERS", dir);
    thumbers_load(&a, &t);
    const char *mp4 = thumbers_find(&t, S("video/mp4")), *pdf = thumbers_find(&t, S("application/pdf")), *foo = thumbers_find(&t, S("text/x-foo"));
    CHECK(t.n == 2 && t.ntypes == 3 && mp4 && strstr(mp4, " -a %i %o %s") && pdf == mp4 && foo && strstr(foo, " -c %u %o"));
    CHECK(!thumbers_find(&t, S("video/x-noexec")));
    CHECK(!thumbers_find(&t, S("video/webm")) && !thumbers_find(&t, S("video/ogg")) && !thumbers_find(&t, S("video/x-f")) && !thumbers_find(&t, S("image/x-notes")));
    CHECK(!thumbers_find(&t, S("video/mp")) && !thumbers_find(&t, S("")));
    env_set("OFX_TEST_THUMBNAILERS", NULL);   // (the guard on: none)
    thumbers_load(&a, &t);
    CHECK(t.n == 0 && !thumbers_find(&t, S("video/mp4")));
    env_restore(), env_restore();
    // Without the guard: every data dir's thumbnailers/, the user's first. (Only read.)
    ops_guard("");
    env_set("XDG_DATA_HOME", at("f12/user")), env_set("XDG_DATA_DIRS", at("f12/sys"));
    snprintf(text, sizeof text, "[Thumbnailer Entry]\nExec=%s user %%i %%o\nMimeType=video/mp4;\n", fake);
    put_thumber(at("f12/user/thumbnailers/z.thumbnailer"), text);
    snprintf(text, sizeof text, "[Thumbnailer Entry]\nExec=%s sys %%i %%o\nMimeType=video/mp4;video/ogg;\n", fake);
    put_thumber(at("f12/sys/thumbnailers/a.thumbnailer"), text);
    thumbers_load(&a, &t);
    mp4 = thumbers_find(&t, S("video/mp4"));
    const char *ogg = thumbers_find(&t, S("video/ogg"));
    CHECK(t.n == 2 && mp4 && strstr(mp4, " user ") && ogg && strstr(ogg, " sys "));
    env_restore(), env_restore();
    ops_guard(root);
}

// An Exec line's fields: %i the path, %u the URI, %o the output, %s the size, %% a %; a quoted word
// stays one; an unknown field, an open quote or too many words: none.
static void test_thumbers_argv(void)
{
    Arena a;
    arena_init(&a, "thumbers argv test", thumbers_test_mem, sizeof thumbers_test_mem);
    const char *argv[THUMB_ARGS_MAX + 1];
    int n = thumbers_argv(&a, "prog -s %s \"%u\" -o=%o %i 100%% \"a b\"", "/x/a b.mp4", "file:///x/a%20b.mp4", "/c/out.png", 256, argv, COUNT(argv));
    CHECK(n == 8 && strcmp(argv[0], "prog") == 0 && strcmp(argv[2], "256") == 0 && strcmp(argv[3], "file:///x/a%20b.mp4") == 0);
    CHECK(n == 8 && strcmp(argv[4], "-o=/c/out.png") == 0 && strcmp(argv[5], "/x/a b.mp4") == 0 && strcmp(argv[6], "100%") == 0 && strcmp(argv[7], "a b") == 0 && !argv[8]);
    CHECK(thumbers_argv(&a, "prog %x %o", "/a", "file:///a", "/o", 128, argv, COUNT(argv)) == -1);
    CHECK(thumbers_argv(&a, "prog %o %", "/a", "file:///a", "/o", 128, argv, COUNT(argv)) == -1);
    CHECK(thumbers_argv(&a, "prog -s%s", "/a", "file:///a", "/o", 1024, argv, COUNT(argv)) == 2 && strcmp(argv[1], "-s1024") == 0);
    CHECK(thumbers_argv(&a, "prog \"%i %o", "/a", "file:///a", "/o", 128, argv, COUNT(argv)) == -1);
    CHECK(thumbers_argv(&a, "prog %i %o", "/a", "file:///a", "/o", 128, argv, 3) == -1);
    CHECK(thumbers_argv(&a, "prog %i %o", "/a", "file:///a", "/o", 128, argv, 4) == 3);
    CHECK(thumbers_argv(&a, "", "/a", "file:///a", "/o", 128, argv, COUNT(argv)) == -1);
}

// Whether a process with "sleep <arg>" in its command line is still running a second on (a SIGKILL
// takes effect as the process next runs).
static bool sleeping(const char *arg)
{
    char cmd[128];
    snprintf(cmd, sizeof cmd, "pgrep -f '[s]leep %s' > /dev/null", arg);   // ([s]: not the sh -c line itself)
    for (int k = 0; k < 20; k++) {
        if (system(cmd) != 0) return false;
        nanosleep(&(struct timespec){ 0, 50000000 }, NULL);
    }
    return true;
}

// A preview made by a thumbnailer (thumb_get with TK_EXEC): its PNG into the cache as ofx's own
// and read from there after; one that makes none, a link, or no PNG in time (killed) fails, and
// is known as failed; in bubblewrap it sees only its file; and the folder it ran in goes.
static void test_thumb_exec(void)
{
    Arena a;
    arena_init(&a, "thumb exec test", thumbers_test_mem, sizeof thumbers_test_mem);
    ops_guard(root);
    char cache[PATH_MAX], bin[PATH_MAX], exec[PATH_MAX * 2];
    snprintf(cache, sizeof cache, "%s", at("f12/cache/thumbnails"));
    snprintf(bin, sizeof bin, "%s", at("f12/bin"));
    Img im = halves(&a, 300, 200), out;
    Str png;
    CHECK(png_encode(&a, im, NULL, 0, 0, &png));
    // "Videos" that are PNGs, so a fake can make the preview with cp.
    char clip[9][PATH_MAX];
    for (int k = 0; k < 9; k++) {
        snprintf(clip[k], sizeof clip[k], "%s", at("f12/videos/clip %d.mp4", k));
        put_bytes(clip[k], png.p, (size_t)png.len);
        set_mtime(clip[k], 1700000000 + k);
    }
    char secret[PATH_MAX];
    snprintf(secret, sizeof secret, "%s", at("f12/secret.png"));
    put_bytes(secret, png.p, (size_t)png.len);
    char text[PATH_MAX * 2];
    put_exec(at("f12/bin/copy"), "#!/bin/sh\n[ \"$3\" = 128 ] || exit 1\ncp \"$1\" \"$2\"\n");   // (48 px: normal's 128)
    put_exec(at("f12/bin/large"), "#!/bin/sh\n[ \"$3\" = 256 ] || exit 1\ncp \"$1\" \"$2\"\n");   // (256 px: large's)
    put_exec(at("f12/bin/none"), "#!/bin/sh\nexit 0\n");
    put_exec(at("f12/bin/hang"), "#!/bin/sh\nsleep 3.125\n");
    put_exec(at("f12/bin/slow"), "#!/bin/sh\ncp \"$1\" \"$2\"\nsleep 3.25\n");
    put_exec(at("f12/bin/link"), "#!/bin/sh\nln -s \"$1\" \"$2\"\n");
    snprintf(text, sizeof text, "#!/bin/sh\ncp '%s' \"$2\"\n", secret);
    put_exec(at("f12/bin/peek"), text);
    int w, h;
    // Made: at 48 px from normal, the spec's text in it; then read from the cache, not run again.
    snprintf(exec, sizeof exec, "%s/copy %%i %%o %%s", bin);
    ThumbAsk q = ask(clip[0], TK_EXEC, 48, cache);
    q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_MADE && out.w == 48 && out.h == 32 && reddish(&out, 2, 16) && blueish(&out, 45, 16));
    CHECK(cached_fresh(&a, cache, "normal", clip[0], &w, &h) && w == 128 && h == 85);
    snprintf(exec, sizeof exec, "%s/none %%i %%o", bin);
    CHECK(thumb_get(&a, &q, &out) == TG_CACHED && out.w == 48);
    // At 256: asked of it at large's size, and in large.
    snprintf(exec, sizeof exec, "%s/large %%i %%o %%s", bin);
    q.px = 256;
    CHECK(thumb_get(&a, &q, &out) == TG_MADE && out.w == 256 && cached_fresh(&a, cache, "large", clip[0], &w, &h) && w == 256);
    snprintf(exec, sizeof exec, "%s/none %%i %%o", bin);
    // The listing older than the file: nothing run.
    q = ask(clip[1], TK_EXEC, 48, cache), q.exec = exec, q.mtime--;
    CHECK(thumb_get(&a, &q, &out) == TG_CHANGED);
    // None made: failed, and known so after, until the file changes.
    q.mtime++;
    CHECK(thumb_get(&a, &q, &out) == TG_FAILED && thumb_get(&a, &q, &out) == TG_FAIL_KNOWN);
    set_mtime(clip[1], 1700000100);
    q = ask(clip[1], TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_FAILED);
    // A link where the PNG should be isn't followed.
    snprintf(exec, sizeof exec, "%s/link %%i %%o", bin);
    q = ask(clip[2], TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_FAILED);
    // Past its time: killed, with what it started, and failed.
    thumb_run_ms = 100;   // (each mutant runs this: 100 ms, not 300, per hang; 2026-11-02)
    snprintf(exec, sizeof exec, "%s/hang %%i %%o", bin);
    q = ask(clip[3], TK_EXEC, 48, cache), q.exec = exec;
    i64 t0 = now_ms();
    CHECK(thumb_get(&a, &q, &out) == TG_FAILED && now_ms() - t0 < 2500 && !sleeping("3.125"));
    // Without bubblewrap too (its group); and one that wrote its PNG first doesn't count.
    thumb_sandbox = false;
    q = ask(clip[6], TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_FAILED && !sleeping("3.125"));
    thumb_sandbox = true;
    snprintf(exec, sizeof exec, "%s/slow %%i %%o", bin);
    q = ask(clip[7], TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_FAILED && !sleeping("3.25"));
    thumb_run_ms = THUMB_RUN_MS;
    // In bubblewrap it sees only the file it was given; without, everything (OFX_TEST_SANDBOX=off).
    snprintf(exec, sizeof exec, "%s/peek %%i %%o", bin);
    q = ask(clip[4], TK_EXEC, 48, cache), q.exec = exec;
    char bw[PATH_MAX];
    if (path_find("bwrap", bw, sizeof bw)) CHECK(thumb_get(&a, &q, &out) == TG_FAILED);
    else fprintf(stderr, "test_thumb_exec: no bwrap here, the sandbox untested\n");
    thumb_sandbox = false;
    q = ask(clip[5], TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_MADE);
    thumb_sandbox = true;
    // A program outside the test's folder isn't run under the guard.
    q = ask(clip[2], TK_EXEC, 96, cache), q.exec = "cp %i %o";
    set_mtime(clip[2], 1700000200), q.mtime = 1700000200;
    CHECK(thumb_get(&a, &q, &out) == TG_FAILED);
    // A folder with a video's name: nothing run.
    char fold[PATH_MAX];
    snprintf(fold, sizeof fold, "%s", at("f12/videos/folder.mp4"));
    mkdir(fold, 0755);
    q = ask(fold, TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_NONE);
    // A file that is gone: nothing. A cache outside the test's folder: refused, nothing made there.
    snprintf(exec, sizeof exec, "%s/copy %%i %%o %%s", bin);
    q = ask(clip[8], TK_EXEC, 48, cache), q.exec = exec;
    unlink(clip[8]);
    CHECK(thumb_get(&a, &q, &out) == TG_NONE);
    char far[PATH_MAX];
    snprintf(far, sizeof far, "%s", out_at("f12-cache"));
    q = ask(clip[0], TK_EXEC, 48, far), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) != TG_MADE && access(far, F_OK) != 0);
    // Every folder they ran in is gone.
    char cmd[PATH_MAX + 64];
    snprintf(cmd, sizeof cmd, "! ls -A '%s' | grep -q ofx-run", cache);
    CHECK(system(cmd) == 0);
}
