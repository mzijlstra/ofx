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
    snprintf(cmd, sizeof cmd, "pgrep -x -f 'sleep %s' > /dev/null", arg);   // (the whole command line: not the sh -c one)
    for (int k = 0; k < 20; k++) {
        if (system(cmd) != 0) return false;
        nanosleep(&(struct timespec){ 0, 50000000 }, NULL);
    }
    return true;
}

// A preview made by a thumbnailer (thumb_get with TK_EXEC): its PNG into the cache as ofx's own
// and read from there after; one that makes none or leaves a link fails, and is known as failed;
// one killed for its time writes nothing (it may make one next time); it gets only PATH, LANG,
// LC_ALL, HOME and GIO_USE_VFS; in bubblewrap it sees only its file, no network, no terminal; and
// the folder it ran in goes.
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
    char clip[12][PATH_MAX];
    for (int k = 0; k < 12; k++) {
        snprintf(clip[k], sizeof clip[k], "%s", at("f12/videos/clip %d.mp4", k));
        put_bytes(clip[k], png.p, (size_t)png.len);
        set_mtime(clip[k], 1700000000 + k);
    }
    char secret[PATH_MAX];
    snprintf(secret, sizeof secret, "%s", at("f12/secret.png"));
    put_bytes(secret, png.p, (size_t)png.len);
    // The hung fakes' sleeps are this run's own (pgrep sees the whole machine: the unit mutants run
    // many of these at once).
    char hang[32], slow[32];
    snprintf(hang, sizeof hang, "3.%06d1", (int)getpid());
    snprintf(slow, sizeof slow, "3.%06d2", (int)getpid());
    char text[PATH_MAX * 2];
    put_exec(at("f12/bin/copy"), "#!/bin/sh\n[ \"$3\" = 128 ] || exit 1\ncp \"$1\" \"$2\"\n");   // (48 px: normal's 128)
    put_exec(at("f12/bin/large"), "#!/bin/sh\n[ \"$3\" = 256 ] || exit 1\ncp \"$1\" \"$2\"\n");   // (256 px: large's)
    put_exec(at("f12/bin/none"), "#!/bin/sh\nexit 0\n");
    put_exec(at("f12/bin/env"), "#!/bin/sh\n[ -z \"$OFX_TEST_SECRET\" ] && [ \"$GIO_USE_VFS\" = local ] && [ -n \"$HOME\" ] || exit 1\ncp \"$1\" \"$2\"\n");
    // In the sandbox: lo alone (/proc/net/dev's two heads and its line), and a session of its own
    // (outside it, the session's leader isn't in its pid namespace: 0).
    put_exec(at("f12/bin/ns"), "#!/bin/sh\n[ \"$(wc -l < /proc/net/dev)\" -le 3 ] && [ \"$(cut -d' ' -f6 /proc/self/stat)\" != 0 ] || exit 1\ncp \"$1\" \"$2\"\n");
    snprintf(text, sizeof text, "#!/bin/sh\nsleep %s\n", hang);
    put_exec(at("f12/bin/hang"), text);
    snprintf(text, sizeof text, "#!/bin/sh\ncp \"$1\" \"$2\"\nsleep %s\n", slow);
    put_exec(at("f12/bin/slow"), text);
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
    CHECK(thumb_get(&a, &q, &out) == TG_NONE && now_ms() - t0 < 2500 && !sleeping(hang));
    snprintf(exec, sizeof exec, "%s/none %%i %%o", bin);   // (no fail entry: a run that makes none is new)
    CHECK(thumb_get(&a, &q, &out) == TG_FAILED);
    // Without bubblewrap too (its group); and one that wrote its PNG first doesn't count.
    snprintf(exec, sizeof exec, "%s/hang %%i %%o", bin);
    box_sandbox = false;
    q = ask(clip[6], TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_NONE && !sleeping(hang));
    box_sandbox = true;
    snprintf(exec, sizeof exec, "%s/slow %%i %%o", bin);
    q = ask(clip[7], TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_NONE && !sleeping(slow));
    thumb_run_ms = THUMB_RUN_MS;
    // Of ofx's environment only the few, in the sandbox and out of it.
    env_set("OFX_TEST_SECRET", "a token");
    snprintf(exec, sizeof exec, "%s/env %%i %%o", bin);
    q = ask(clip[9], TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_MADE);
    box_sandbox = false;
    q = ask(clip[10], TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_MADE);
    box_sandbox = true;
    env_restore();
    // In bubblewrap it sees only the file it was given; without, everything (OFX_TEST_SANDBOX=off).
    snprintf(exec, sizeof exec, "%s/peek %%i %%o", bin);
    q = ask(clip[4], TK_EXEC, 48, cache), q.exec = exec;
    char bw[PATH_MAX];
    if (path_find("bwrap", bw, sizeof bw)) {
        CHECK(thumb_get(&a, &q, &out) == TG_FAILED);
        snprintf(exec, sizeof exec, "%s/ns %%i %%o", bin);
        q = ask(clip[11], TK_EXEC, 48, cache), q.exec = exec;
        CHECK(thumb_get(&a, &q, &out) == TG_MADE);
        snprintf(exec, sizeof exec, "%s/peek %%i %%o", bin);
    } else {
        fprintf(stderr, "test_thumb_exec: no bwrap here, the sandbox untested\n");
    }
    box_sandbox = false;
    q = ask(clip[5], TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_MADE);
    box_sandbox = true;
    // Without bubblewrap on PATH: not run at all (nothing made, nothing written).
    snprintf(exec, sizeof exec, "%s/copy %%i %%o %%s", bin);
    set_mtime(clip[3], 1700000300);
    q = ask(clip[3], TK_EXEC, 48, cache), q.exec = exec;
    env_set("PATH", bin);
    CHECK(thumb_get(&a, &q, &out) == TG_NONE);
    env_restore();
    CHECK(thumb_get(&a, &q, &out) == TG_MADE);
    // A program outside the test's folder isn't run under the guard (nor counted as failed).
    q = ask(clip[2], TK_EXEC, 96, cache), q.exec = "cp %i %o";
    set_mtime(clip[2], 1700000200), q.mtime = 1700000200;
    CHECK(thumb_get(&a, &q, &out) == TG_NONE && thumb_get(&a, &q, &out) == TG_NONE);
    // A folder with a video's name: nothing run.
    char fold[PATH_MAX];
    snprintf(fold, sizeof fold, "%s", at("f12/videos/folder.mp4"));
    mkdir(fold, 0755);
    q = ask(fold, TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_NONE);
    // Its run folder gone while it ran (Clear Previews): nothing, and no failure written.
    put_exec(at("f12/bin/gone"), "#!/bin/sh\nrm -rf \"$(dirname \"$2\")\"\n");
    snprintf(exec, sizeof exec, "%s/gone %%i %%o", bin);
    set_mtime(clip[4], 1700000400);
    box_sandbox = false;   // (in bubblewrap the folder is a mount point there)
    q = ask(clip[4], TK_EXEC, 48, cache), q.exec = exec;
    CHECK(thumb_get(&a, &q, &out) == TG_NONE);
    snprintf(exec, sizeof exec, "%s/none %%i %%o", bin);
    CHECK(thumb_get(&a, &q, &out) == TG_FAILED);   // (no fail entry from the first)
    box_sandbox = true;
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

// ofx-clean: a thumbnailer run folder a crash left goes after a day, what is in it too (a link in
// it removed, not followed); a newer one, another name, or a file of that name stays; a dry run
// only counts.
static void test_clean_runs(void)
{
    Arena a;
    arena_init(&a, "clean runs test", thumbers_test_mem, sizeof thumbers_test_mem);
    ops_guard(root);
    i64 now = (i64)time(NULL), old = now - 2 * 86400;
    char cache[PATH_MAX], keep[PATH_MAX];
    snprintf(cache, sizeof cache, "%s", at("f12/cleancache/thumbnails"));
    snprintf(keep, sizeof keep, "%s", at("f12/keep.txt"));
    put(keep, "mine");
    put(at("f12/cleancache/thumbnails/.ofx-run-12-3/out.png"), "part");
    symlink(keep, at("f12/cleancache/thumbnails/.ofx-run-12-3/link"));
    set_mtime(at("f12/cleancache/thumbnails/.ofx-run-12-3"), old);
    put(at("f12/cleancache/thumbnails/.ofx-run-12-4/out.png"), "new");
    put(at("f12/cleancache/thumbnails/.ofx-run-x/out.png"), "x");
    set_mtime(at("f12/cleancache/thumbnails/.ofx-run-x"), old);
    put(at("f12/cleancache/thumbnails/.ofx-run-5-6"), "a file");
    set_mtime(at("f12/cleancache/thumbnails/.ofx-run-5-6"), old);
    mkdirs(at("f12/cleancache/thumbnails/.ofx-run-7-8/sub"));
    set_mtime(at("f12/cleancache/thumbnails/.ofx-run-7-8"), old);
    CleanStats st;
    CleanOpts o = { .now = now, .cap = 0, .dry = true };
    clean_run(&a, cache, S("1 0 0:1 / / rw - ext4 a rw\n"), &o, &st);
    CHECK(st.n[CR_TEMP] == 2 && access(at("f12/cleancache/thumbnails/.ofx-run-12-3/out.png"), F_OK) == 0);
    o.dry = false;
    clean_run(&a, cache, S("1 0 0:1 / / rw - ext4 a rw\n"), &o, &st);
    CHECK(st.n[CR_TEMP] == 1 && st.failed == 1 && access(at("f12/cleancache/thumbnails/.ofx-run-12-3"), F_OK) != 0);   // (7-8: its folder stays)
    CHECK(strcmp(slurp(keep), "mine") == 0 && access(at("f12/cleancache/thumbnails/.ofx-run-12-4/out.png"), F_OK) == 0);
    CHECK(access(at("f12/cleancache/thumbnails/.ofx-run-x"), F_OK) == 0 && access(at("f12/cleancache/thumbnails/.ofx-run-5-6"), F_OK) == 0);
    // all (ofx-clean --all, F12 step 4): every file of the cache's, a new run folder too, however new;
    // a file of another name stays.
    put(at("f12/cleancache/thumbnails/normal/0123456789abcdef0123456789abcdef.png"), "new");
    put(at("f12/cleancache/thumbnails/fail/ofx/0123456789abcdef0123456789abcdee.png"), "f");
    put(at("f12/cleancache/thumbnails/normal/.ofx-9-9.png"), "t");
    put(at("f12/cleancache/thumbnails/normal/notes.txt"), "mine");
    o.all = true, o.dry = true;
    clean_run(&a, cache, S("1 0 0:1 / / rw - ext4 a rw\n"), &o, &st);
    CHECK(st.n[CR_ALL] == 3 && st.n[CR_TEMP] == 2 && st.n[CR_KEEP] == 0 && access(at("f12/cleancache/thumbnails/normal/.ofx-9-9.png"), F_OK) == 0);   // (temp: 12-4's and 7-8's folders)
    o.dry = false;
    clean_run(&a, cache, S("1 0 0:1 / / rw - ext4 a rw\n"), &o, &st);
    CHECK(st.n[CR_ALL] == 3 && access(at("f12/cleancache/thumbnails/normal/0123456789abcdef0123456789abcdef.png"), F_OK) != 0 &&
          access(at("f12/cleancache/thumbnails/fail/ofx/0123456789abcdef0123456789abcdee.png"), F_OK) != 0 &&
          access(at("f12/cleancache/thumbnails/.ofx-run-12-4"), F_OK) != 0 && access(at("f12/cleancache/thumbnails/normal/notes.txt"), F_OK) == 0);
    CHECK(clean_run_name(".ofx-run-1-2") && !clean_run_name(".ofx-run-1-") && !clean_run_name(".ofx-run--2") && !clean_run_name(".ofx-run-1-2x") &&
          !clean_run_name(".ofx-run-12345678901-2") && !clean_run_name(".ofx-1-2"));
}

// View → Clear Previews… (F12 step 4): the question opens, on top of the panels and under the other
// questions; Enter, Shift+Enter, y and its Delete button say yes (UA_CLEAR_PREVIEWS), Esc, n and
// Cancel no; while gui/ says one runs, the entry is greyed.
static int view_at(UiMenuItem it)
{
    for (int i = 0; i < UI_MENU[2].n; i++)
        if (UI_MENU[2].items[i] == it) return i;
    return -1;
}

static void test_clear_question(void)
{
    UiNames names = { name_at, NULL, NULL };
    Ui u = menu_ui(3);
    CHECK(view_at(UM_CLEAR_PREVIEWS) == UI_MENU[2].n - 1 && UI_MENU[2].items[UI_MENU[2].n - 2] == UM_SEP);   // (View's last, after a line)
    UiInput in = ui_input_none();
    u.menu_on = true, u.menu = 2, in.menu_item = view_at(UM_CLEAR_PREVIEWS);
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.clear_asking && !u.menu_on && ui_on_top(&u) == UI_ON_CLEAR);
    const int no[] = { UK_ESC, 0x100 + 'n', 0x100 + 'N' }, yes[] = { UK_ENTER, UK_SHIFT_ENTER, 0x100 + 'y' };
    for (int k = 0; k < 3; k++) {
        u.clear_asking = true;
        CHECK(menu_keys(&u, 1, no[k]).kind == UA_NONE && !u.clear_asking);
        u.clear_asking = true;
        CHECK(menu_keys(&u, 1, yes[k]).kind == UA_CLEAR_PREVIEWS && !u.clear_asking);
    }
    // Another key leaves it up and does nothing (no terminal behind it).
    u.clear_asking = true;
    CHECK(menu_keys(&u, 2, 0x100 + 't', UK_DOWN).kind == UA_NONE && u.clear_asking && u.list.sel == 0);
    // The buttons.
    in = ui_input_none(), in.modal = UI_MODAL_NO;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.clear_asking);
    u.clear_asking = true;
    in = ui_input_none(), in.modal = UI_MODAL_YES;
    CHECK(ui_handle(&u, names, &in).kind == UA_CLEAR_PREVIEWS && !u.clear_asking);
    // Its place: over a panel, under Empty Trash's question.
    u.clear_asking = true, u.panel = UI_PANEL_KEYS;
    CHECK(ui_on_top(&u) == UI_ON_CLEAR);
    u.empty_asking = true;
    CHECK(ui_on_top(&u) == UI_ON_EMPTY);
    u.empty_asking = u.clear_asking = false, u.panel = UI_PANEL_NONE;
    // While one runs, greyed: picking it does nothing.
    u.clearing = true;
    CHECK(!ui_menu_enabled(&u, UM_CLEAR_PREVIEWS));
    in = ui_input_none(), u.menu_on = true, u.menu = 2, in.menu_item = view_at(UM_CLEAR_PREVIEWS);
    ui_handle(&u, names, &in);
    CHECK(!u.clear_asking);
}

// ---- git status per file (F12 step 5) ---------------------------------------------------------

// git status --porcelain=v2 -z --ignored --branch at a repository's top, as a scratch repo gave it
// (2026-11-02): a.txt modified, src/b.txt renamed to src/b2.txt (staged), src/new.txt added,
// newdir/ and src/deep/untracked.txt untracked, debug.log and ign/ ignored; plus a conflict and a
// modified file in the index only.
static const char GIT_OUT[] =
    "# branch.oid 23aeba1e8e7abd522ec35165879b1f056a482afa\0# branch.head master\0# branch.upstream origin/master\0# branch.ab +2 -1\0"
    "1 .M N... 100644 100644 100644 7898192261 7898192261 a.txt\0"
    "2 R. N... 100644 100644 100644 6178079822 6178079822 R100 src/b2.txt\0src/b.txt\0"
    "1 A. N... 000000 100644 100644 0000000000 3e75765 src/new.txt\0"
    "1 M. N... 100644 100644 100644 1111111111 2222222222 src/deep/staged mod.txt\0"
    "u UU N... 100644 100644 100644 100644 aaaa bbbb cccc lib/both.c\0"
    "2 R. N... 100644 100644 100644 1111 1111 R100 renamed.txt\0? odd old name\0"   // (an old path that looks like a record)
    "? newdir/\0? src/deep/untracked.txt\0! debug.log\0! ign/\0! docs/old/x.log\0";

static void test_git_parse(void)
{
    Arena a;
    arena_init(&a, "git test", thumbers_test_mem, sizeof thumbers_test_mem);
    static GitStat g;
    CHECK(git_parse(&a, (Str){ GIT_OUT, sizeof GIT_OUT - 1 }, &g));
    CHECK(strcmp(g.head, "master") == 0 && strncmp(g.oid, "23aeba1e", 8) == 0 && g.has_ab && g.ahead == 2 && g.behind == 1);
    CHECK(g.changed == 8 && !g.cut);   // (the ignored three aside)
    CHECK(git_state(&g, S(""), S("renamed.txt"), false) == GS_RENAMED && git_state(&g, S(""), S("odd old name"), false) == GS_NONE);
    // Files at the top, and the letters.
    CHECK(git_state(&g, S(""), S("a.txt"), false) == GS_MODIFIED && git_letter(GS_MODIFIED) == 'M');
    CHECK(git_state(&g, S(""), S("debug.log"), false) == GS_IGNORED && git_letter(GS_IGNORED) == 0);
    CHECK(git_state(&g, S(""), S("README"), false) == GS_NONE && git_letter(GS_NONE) == 0);
    CHECK(git_state(&g, S(""), S("newdir"), true) == GS_UNTRACKED && git_letter(GS_UNTRACKED) == 'U');
    CHECK(git_state(&g, S(""), S("ign"), true) == GS_IGNORED);
    // A folder: the most urgent under it (src: renamed, added, modified in the index, untracked).
    CHECK(git_state(&g, S(""), S("src"), true) == GS_MODIFIED && git_state(&g, S("src"), S("deep"), true) == GS_MODIFIED);
    CHECK(git_state(&g, S(""), S("lib"), true) == GS_CONFLICT && git_letter(GS_CONFLICT) == '!');
    CHECK(git_state(&g, S(""), S("src"), false) == GS_NONE);   // (as a file: nothing of its own)
    // In src: the rename by its new name (the old one isn't listed), the added file.
    CHECK(git_state(&g, S("src"), S("b2.txt"), false) == GS_RENAMED && git_letter(GS_RENAMED) == 'R');
    CHECK(git_state(&g, S("src"), S("b.txt"), false) == GS_NONE);
    CHECK(git_state(&g, S("src"), S("new.txt"), false) == GS_STAGED && git_letter(GS_STAGED) == 'A');
    CHECK(git_state(&g, S("src/deep"), S("staged mod.txt"), false) == GS_MODIFIED && git_state(&g, S("src/deep"), S("untracked.txt"), false) == GS_UNTRACKED);
    // Ignored: in ign/ and below, everything; not elsewhere.
    CHECK(git_ignored_at(&g, S("ign")) && git_ignored_at(&g, S("ign/x/y")) && !git_ignored_at(&g, S("")) && !git_ignored_at(&g, S("src")));
    CHECK(!git_ignored_at(&g, S("igno")) && !git_ignored_at(&g, S("newdir")));   // (a folder git names whole, untracked)
    // A folder holding only an ignored file isn't ignored itself, nor marked.
    CHECK(git_state(&g, S(""), S("docs"), true) == GS_NONE && git_state(&g, S("docs"), S("old"), true) == GS_NONE && !git_ignored_at(&g, S("docs/old")));
    CHECK(git_state(&g, S("docs/old"), S("x.log"), false) == GS_IGNORED);
    // Detached, no upstream; a record of another kind, a short one, a cut one: skipped.
    static const char OUT2[] = "# branch.oid abc\0# branch.head (detached)\0x what\0? \0" "1 .M N... 100644\0";
    CHECK(git_parse(&a, (Str){ OUT2, sizeof OUT2 - 1 }, &g));
    CHECK(g.head[0] == 0 && strcmp(g.oid, "abc") == 0 && !g.has_ab && g.changed == 0);
}

// The repository's top: the nearest folder with a .git (a folder, or a worktree's file), itself
// too; none outside one, nor inside .git.
static void test_git_top(void)
{
    char top[PATH_MAX], want[PATH_MAX];
    mkdirs(at("f12/repo/.git/objects"));
    mkdirs(at("f12/repo/src/deep"));
    put(at("f12/wt/.git"), "gitdir: ../repo/.git/worktrees/wt\n");
    mkdirs(at("f12/wt/sub"));
    mkdirs(at("f12/plain/sub"));
    CHECK(realpath(at("f12/repo"), want) && git_top(at("f12/repo/src/deep"), top, sizeof top) && strcmp(top, want) == 0);
    CHECK(git_top(at("f12/repo"), top, sizeof top) && strcmp(top, want) == 0);
    CHECK(realpath(at("f12/wt"), want) && git_top(at("f12/wt/sub"), top, sizeof top) && strcmp(top, want) == 0);
    CHECK(!git_top(at("f12/repo/.git/objects"), top, sizeof top) && !git_top(at("f12/repo/.git"), top, sizeof top));
    CHECK(!git_top(at("f12/nope"), top, sizeof top));
    CHECK(!git_top(at("f12/plain/sub"), top, sizeof top) || strncmp(top, root, strlen(root)) != 0);   // (the test root isn't in a repo; a /tmp above it might be)
}

// spawn_piped: the child's output on the pipe, to its end; the guard runs only a test's own.
static void test_spawn_piped(void)
{
    ops_guard(root);
    char prog[PATH_MAX];
    snprintf(prog, sizeof prog, "%s", at("f12/bin/say"));
    put_exec(prog, "#!/bin/sh\necho one\necho two \"$1\"\n");
    char *argv[] = { prog, "x", NULL };
    int fd = -1;
    Err err;
    CHECK(spawn_piped(argv, &fd, &err) && fd >= 0);
    char buf[64] = "";
    isize n = 0;
    for (ssize_t k; fd >= 0 && (k = read(fd, buf + n, sizeof buf - 1 - (size_t)n)) > 0;) n += k;
    if (fd >= 0) close(fd);
    buf[n] = 0;
    CHECK(strcmp(buf, "one\ntwo x\n") == 0);
    char *out[] = { "true", NULL };
    CHECK(!spawn_piped(out, &fd, &err) && strstr(err.msg, "not run"));
}

// A shell line in dir, with git's own config files out of it (only the repository's counts).
static int sh_in(const char *dir, const char *line)
{
    char cmd[4 * PATH_MAX];
    snprintf(cmd, sizeof cmd, "cd '%s' && GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null HOME='%s' %s > /dev/null 2>&1", dir, root, line);
    return system(cmd);
}

// git_status on scratch repositories, the real git behind a test's own wrapper (the guard), in
// bubblewrap: the states as git gives them; a repository's fsmonitor doesn't run; its clean filter
// runs but can't write outside (it can without the sandbox: the filter does run); a worktree's
// repository is reached; out of time, none.
// git_status into a fresh arena each time (its output takes GIT_OUT_MAX of it, as gui/git.c's
// slot does).
static bool gs_run(Arena *a, const char *git, const char *dir, const char *top, GitStat *g)
{
    arena_reset(a);
    return git_status(a, git, dir, top, g);
}

static void test_git_status(void)
{
    Arena a;
    arena_init(&a, "git status test", thumbers_test_mem, sizeof thumbers_test_mem);
    ops_guard(root);
    env_set("HOME", root), env_set("XDG_CONFIG_HOME", at("f12/xdg"));
    char repo[PATH_MAX], git[PATH_MAX], top[PATH_MAX], sub[PATH_MAX];
    snprintf(repo, sizeof repo, "%s", at("f12/gs/repo"));
    snprintf(git, sizeof git, "%s", at("f12/bin/git"));
    put_exec(git, "#!/bin/sh\nexec /usr/bin/git \"$@\"\n");
    mkdirs(repo);
    CHECK(sh_in(repo, "git init -q -b main . && git -c user.name=t -c user.email=t@t commit -q --allow-empty -m 0") == 0);
    put(at("f12/gs/repo/kept.txt"), "k\n"), put(at("f12/gs/repo/src/b.txt"), "b\n"), put(at("f12/gs/repo/.gitignore"), "*.log\n");
    CHECK(sh_in(repo, "git add -A && git -c user.name=t -c user.email=t@t commit -q -m 1") == 0);
    put(at("f12/gs/repo/kept.txt"), "k2\n"), put(at("f12/gs/repo/src/new.txt"), "n\n"), put(at("f12/gs/repo/x.log"), "l\n");
    CHECK(sh_in(repo, "git add src/new.txt") == 0);
    // A repository config that would run programs: an fsmonitor, a clean filter writing outside.
    char pwned[PATH_MAX], cfg[3 * PATH_MAX];
    snprintf(pwned, sizeof pwned, "%s", at("f12/gs/pwned"));
    snprintf(cfg, sizeof cfg, "git config core.fsmonitor 'touch %s.fsmonitor' && git config filter.evil.clean 'sh -c \"echo x > %s; cat\"'", pwned, pwned);
    CHECK(sh_in(repo, cfg) == 0);
    put(at("f12/gs/repo/.gitattributes"), "*.txt filter=evil\n");
    static GitStat g;
    CHECK(git_top(repo, top, sizeof top));
    CHECK(realpath(at("f12/gs/repo/src"), sub) && gs_run(&a, git, sub, top, &g));
    CHECK(strcmp(g.head, "main") == 0 && git_state(&g, S("src"), S("new.txt"), false) == GS_STAGED && git_state(&g, S("src"), S("b.txt"), false) == GS_NONE);
    CHECK(gs_run(&a, git, top, top, &g));
    CHECK(git_state(&g, S(""), S("kept.txt"), false) == GS_MODIFIED && git_state(&g, S(""), S("x.log"), false) == GS_IGNORED && git_state(&g, S(""), S("src"), true) == GS_STAGED);
    CHECK(git_state(&g, S(""), S(".gitattributes"), false) == GS_UNTRACKED);
    char fsm[PATH_MAX + 16];
    snprintf(fsm, sizeof fsm, "%s.fsmonitor", pwned);
    CHECK(access(fsm, F_OK) != 0 && access(pwned, F_OK) != 0);
    // The filter runs: without the sandbox it writes there (kept.txt touched so git reads it again);
    // the fsmonitor doesn't, and the index isn't written (--no-optional-locks).
    box_sandbox = false;
    set_mtime(at("f12/gs/repo/kept.txt"), 1700000500);
    struct stat ix0, ix1;
    CHECK(stat(at("f12/gs/repo/.git/index"), &ix0) == 0);
    CHECK(gs_run(&a, git, top, top, &g) && access(pwned, F_OK) == 0 && access(fsm, F_OK) != 0);
    CHECK(stat(at("f12/gs/repo/.git/index"), &ix1) == 0 && ix1.st_mtim.tv_sec == ix0.st_mtim.tv_sec && ix1.st_mtim.tv_nsec == ix0.st_mtim.tv_nsec && ix1.st_ino == ix0.st_ino);
    box_sandbox = true;
    unlink(pwned);
    set_mtime(at("f12/gs/repo/kept.txt"), 1700000600);
    CHECK(gs_run(&a, git, top, top, &g) && access(pwned, F_OK) != 0);
    // A worktree: its .git a file; the repository's .git found and read.
    char wt[PATH_MAX], common[PATH_MAX], want[PATH_MAX];
    snprintf(wt, sizeof wt, "%s", at("f12/gs/wt"));
    CHECK(sh_in(repo, "git config --unset core.fsmonitor; git config --unset filter.evil.clean; rm .gitattributes; git worktree add -q ../wt -b other") == 0);
    put(at("f12/gs/wt/w.txt"), "w\n");
    CHECK(git_top(wt, top, sizeof top));
    git_common(top, common, sizeof common);
    CHECK(realpath(at("f12/gs/repo/.git"), want) && strcmp(common, want) == 0);
    CHECK(gs_run(&a, git, top, top, &g) && strcmp(g.head, "other") == 0 && git_state(&g, S(""), S("w.txt"), false) == GS_UNTRACKED);
    CHECK(git_top(repo, top, sizeof top) && (git_common(top, common, sizeof common), common[0] == 0));
    // Out of time: none.
    put_exec(git, "#!/bin/sh\nsleep 2.5\n");
    git_run_ms = 100;
    CHECK(!gs_run(&a, git, top, top, &g));
    git_run_ms = GIT_RUN_MS;
    // Says nothing (not a repository, git failed): none.
    put_exec(git, "#!/bin/sh\nexit 128\n");
    CHECK(!gs_run(&a, git, top, top, &g));
    env_restore(), env_restore();
}

// The branch menu's list (F12 step 5b): the one checked out first, then the reflog's checkouts
// newest first (each move's target, then where it came from), then the rest by commit date; only
// local branches, each once, 8 at most; detached: none marked.
static void test_git_branches_merge(void)
{
    static GitBranches b;
    Str refs = S("main\nnewest\nfeat-b\nfeat-a\nold\nr1\nr2\nr3\nr4\nr5\nearly\n");
    // (a deleted branch and a detached commit skipped; a message that isn't a checkout's, though
    // its text past the prefix's length reads as one, skipped)
    Str log = S("0 1 t <t@t> 1 +0000\tcommit (initial): one\n"
                "1 2 t <t@t> 2 +0000\tcheckout: moving from early to gone\n"
                "2 3 t <t@t> 3 +0000\tcheckout: moving from gone to 1a2b3c4\n"
                "3 4 t <t@t> 4 +0000\tcheckout: moving from feat-a to feat-b\n"
                "4 5 t <t@t> 5 +0000\trebase -i: moving from r4 to r5");
    git_branches_merge(&b, "main", log, refs);
    CHECK(b.n == 8 && b.cur == 0);
    const char *want[] = { "main", "feat-b", "feat-a", "early", "newest", "old", "r1", "r2" };
    for (int i = 0; i < 8 && i < b.n; i++) CHECK(strcmp(b.name[i], want[i]) == 0);
    CHECK(!git_branch_add(&b, S("r3"), refs) && b.n == 8);   // (full: nothing more)
    // Detached, no reflog: by date only, none marked.
    git_branches_merge(&b, "", STR_EMPTY, S("x\ny\n"));
    CHECK(b.n == 2 && b.cur == -1 && strcmp(b.name[0], "x") == 0 && strcmp(b.name[1], "y") == 0);
    // HEAD on a branch git doesn't list (an unborn one): none marked; a name that only begins as one
    // of refs' isn't it.
    git_branches_merge(&b, "fresh", S("1 2 t <t@t> 2 +0000\tcheckout: moving from ma to x"), S("main\nx"));
    CHECK(b.n == 2 && b.cur == -1 && strcmp(b.name[0], "x") == 0 && strcmp(b.name[1], "main") == 0);
}

// HEAD's branch read from the gitdir (F12 step 5c: the menu's current one, after a switch): a
// branch; detached; a link or a FIFO there, none (and at once).
static void test_git_head_name(void)
{
    char n[GIT_HEAD_MAX];
    mkdirs(at("f12/hd"));
    put(at("f12/hd/HEAD"), "ref: refs/heads/feat/x\n");
    git_head_name(at("f12/hd"), n, sizeof n);
    CHECK(strcmp(n, "feat/x") == 0 && git_head_is(at("f12/hd"), "feat/x") && !git_head_is(at("f12/hd"), "feat"));
    put(at("f12/hd/HEAD"), "1319d8b0000000000000000000000000000000\n");
    git_head_name(at("f12/hd"), n, sizeof n);
    CHECK(n[0] == 0 && !git_head_is(at("f12/hd"), ""));
    unlink(at("f12/hd/HEAD"));
    put(at("f12/hd/real"), "ref: refs/heads/main\n");
    CHECK(symlink(at("f12/hd/real"), at("f12/hd/HEAD")) == 0);
    git_head_name(at("f12/hd"), n, sizeof n);
    CHECK(n[0] == 0);
    unlink(at("f12/hd/HEAD"));
    CHECK(mkfifo(at("f12/hd/HEAD"), 0600) == 0);
    i64 t0 = now_ms();
    git_head_name(at("f12/hd"), n, sizeof n);
    CHECK(n[0] == 0 && now_ms() - t0 < 1000);
}

// HEAD's reflog read from its end: whole lines only once it is longer than GIT_REFLOG_TAIL; empty
// without one.
static void test_git_reflog_tail(void)
{
    static char buf[GIT_REFLOG_TAIL], big[GIT_REFLOG_TAIL + 100];
    mkdirs(at("f12/rl/logs"));
    put(at("f12/rl/logs/HEAD"), "a\nb\n");
    Str t = git_reflog_tail(at("f12/rl"), buf, sizeof buf);
    CHECK(str_eq(t, S("a\nb\n")));
    memset(big, 'x', sizeof big - 1);
    big[200] = '\n', big[sizeof big - 2] = '\n';   // (the cut line ends at 200, past the tail's start)
    put(at("f12/rl/logs/HEAD"), big);
    t = git_reflog_tail(at("f12/rl"), buf, sizeof buf);
    CHECK(t.len == (isize)sizeof big - 1 - 201 && t.p[t.len - 1] == '\n' && str_find(t, '\n') == t.len - 1);
    CHECK(git_reflog_tail(at("f12/none"), buf, sizeof buf).len == 0);
    // A FIFO or a link there: none, at once (a FIFO's open would wait for a writer).
    unlink(at("f12/rl/logs/HEAD"));
    CHECK(mkfifo(at("f12/rl/logs/HEAD"), 0600) == 0);
    i64 t0 = now_ms();
    CHECK(git_reflog_tail(at("f12/rl"), buf, sizeof buf).len == 0 && now_ms() - t0 < 1000);
    unlink(at("f12/rl/logs/HEAD"));
    put(at("f12/rl/real"), "a\n");
    CHECK(symlink(at("f12/rl/real"), at("f12/rl/logs/HEAD")) == 0 && git_reflog_tail(at("f12/rl"), buf, sizeof buf).len == 0);
}

// git_branches on a scratch repository (the real git behind the wrapper, in bubblewrap): its
// branches by commit date, the checkouts first; a worktree's own reflog; git not answering, none.
static void test_git_branches(void)
{
    ops_guard(root);
    env_set("HOME", root), env_set("XDG_CONFIG_HOME", at("f12/xdg"));
    char repo[PATH_MAX], git[PATH_MAX], top[PATH_MAX];
    snprintf(repo, sizeof repo, "%s", at("f12/br/repo"));
    snprintf(git, sizeof git, "%s", at("f12/bin/git"));
    put_exec(git, "#!/bin/sh\nexec /usr/bin/git \"$@\"\n");
    mkdirs(repo);
    put_exec(at("f12/br/mk.sh"), "c() { GIT_COMMITTER_DATE=\"$1 +0000\" git -c user.name=t -c user.email=t@t commit -q --allow-empty -m \"$1\"; }\n"
                                 "git init -q -b main . && c 1700000000 && git branch old && c 1700000100 && git branch mid &&\n"
                                 "git switch -q -c feat && c 1700000200 && git switch -q main && git switch -q old && git switch -q main &&\n"
                                 "git branch aaa old\n");   // (by date after mid, by name first)
    CHECK(sh_in(repo, "sh ../mk.sh") == 0);
    static GitBranches b;
    CHECK(git_top(repo, top, sizeof top) && git_branches(git, top, top, &b));
    CHECK(b.n == 5 && b.cur == 0 && strcmp(b.name[0], "main") == 0 && strcmp(b.name[1], "old") == 0 && strcmp(b.name[2], "feat") == 0 && strcmp(b.name[3], "mid") == 0 && strcmp(b.name[4], "aaa") == 0);
    // A worktree's checkouts are its own reflog's (in the repository's .git/worktrees/wt).
    CHECK(sh_in(repo, "git worktree add -q ../wt -b wtb") == 0);
    CHECK(sh_in(at("f12/br/wt"), "git switch -q mid && git switch -q wtb") == 0);
    CHECK(git_top(at("f12/br/wt"), top, sizeof top) && git_branches(git, top, top, &b));
    CHECK(b.n == 6 && b.cur == 0 && strcmp(b.name[0], "wtb") == 0 && strcmp(b.name[1], "mid") == 0 && strcmp(b.name[2], "feat") == 0);
    put_exec(git, "#!/bin/sh\nexit 128\n");
    CHECK(!git_branches(git, top, top, &b));
    env_restore(), env_restore();
}

// The branch button's menu (F12 step 5b): a press opens it while the button is there; its note
// while there are no branches, else the branches, the one checked out marked; a pick gives its
// index; another press (or Esc) closes it.
static void test_branch_menu(void)
{
    UiNames names = { name_at, NULL, NULL };
    Ui u = menu_ui(3);
    UiInput in = ui_input_none();
    in.bar = UB_BRANCH;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.menu_on);   // (no button: nothing)
    u.branch_on = true, u.br_note = true, u.br_n = 0, u.br_here = -1;
    in = ui_input_none(), in.bar = UB_BRANCH;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.menu_on && u.menu == UI_CTX_BRANCH && u.menu_at == -1);
    CHECK(ui_menu_shown(&u, UM_BR_NOTE) && !ui_menu_enabled(&u, UM_BR_NOTE) && !ui_menu_shown(&u, UM_BR_0));
    u.br_note = false, u.br_n = 3, u.br_here = 1;
    CHECK(!ui_menu_shown(&u, UM_BR_NOTE) && ui_menu_enabled(&u, UM_BR_2) && !ui_menu_shown(&u, UM_BR_3));
    CHECK(ui_menu_mark(&u, UM_BR_1) == 1 && ui_menu_mark(&u, UM_BR_0) == 0 && UI_MENU_ITEM[UM_BR_0].mark == 'r');
    in = ui_input_none(), in.menu_item = 3;   // (br-2: after the note)
    UiAction a = ui_handle(&u, names, &in);
    CHECK(a.kind == UA_BRANCH && a.index == 2 && !u.menu_on);
    // Esc closes it; a press elsewhere while it is up only closes it.
    in = ui_input_none(), in.bar = UB_BRANCH;
    ui_handle(&u, names, &in);
    CHECK(menu_keys(&u, 1, UK_ESC).kind == UA_NONE && !u.menu_on);
    in = ui_input_none(), in.bar = UB_BRANCH;
    ui_handle(&u, names, &in);
    in = ui_input_none(), in.menu_away = true;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && !u.menu_on);
}

// The branch button by keyboard (F12 step 5c): Ctrl+K's path buttons, Right past the last onto it
// (only when it is there), Enter opens its menu with its first entry marked, the keys back to the
// list as it closes; the menu key too; Shift+Enter there does nothing.
static void test_branch_keys(void)
{
    UiNames names = { name_at, NULL, NULL };
    Ui u = menu_ui(3);
    u.br_n = 2, u.br_here = 0;
    UiInput in = keys(3, UK_CRUMBS, UK_RIGHT, UK_RIGHT);
    in.crumb_n = 4, in.crumb_last = 3;
    ui_handle(&u, names, &in);
    CHECK(u.focus == UI_FOCUS_CRUMBS && u.crumb_at == 3);   // (no button: the last path button)
    u.branch_on = true;
    in = keys(1, UK_RIGHT), in.crumb_n = 4, in.crumb_last = 3;
    ui_handle(&u, names, &in);
    CHECK(u.crumb_at == 4);
    in = keys(1, UK_SHIFT_ENTER), in.crumb_n = 4, in.crumb_last = 3;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.crumb_at == 4 && !u.menu_on);
    in = keys(1, UK_ENTER), in.crumb_n = 4, in.crumb_last = 3;
    CHECK(ui_handle(&u, names, &in).kind == UA_NONE && u.menu_on && u.menu == UI_CTX_BRANCH && u.menu_at == 1 && u.focus != UI_FOCUS_CRUMBS);
    in = keys(1, UK_ESC), in.crumb_n = 4, in.crumb_last = 3;
    ui_handle(&u, names, &in);
    CHECK(!u.menu_on && u.focus == UI_FOCUS_LIST);
    in = keys(3, UK_CRUMBS, UK_RIGHT, UK_CONTEXT), in.crumb_n = 4, in.crumb_last = 3;
    ui_handle(&u, names, &in);
    CHECK(u.menu_on && u.menu == UI_CTX_BRANCH);
    // The button gone (another folder): the mark comes back to the last path button.
    u.menu_on = false, u.focus = UI_FOCUS_CRUMBS, u.crumb_at = 4, u.branch_on = false;
    in = ui_input_none(), in.crumb_n = 4, in.crumb_last = 3;
    ui_handle(&u, names, &in);
    CHECK(u.crumb_at == 3);
}

// git_switch (F12 step 5c) on a scratch repository, the real git behind the wrapper, in bubblewrap
// with the repository writable: a switch; git's refusal said (local changes, a branch that isn't);
// a hook runs, writing inside the repository and not outside (it can without the sandbox: it does
// run); a worktree's switch (its real .git writable); the guard; an option for a name.
static void test_git_switch(void)
{
    ops_guard(root);
    env_set("HOME", root), env_set("XDG_CONFIG_HOME", at("f12/xdg"));
    char repo[PATH_MAX], git[PATH_MAX], top[PATH_MAX], gitdir[PATH_MAX], msg[256], cmd[3 * PATH_MAX];
    snprintf(repo, sizeof repo, "%s", at("f12/sw/repo"));
    snprintf(git, sizeof git, "%s", at("f12/bin/git"));
    put_exec(git, "#!/bin/sh\nexec /usr/bin/git \"$@\"\n");
    mkdirs(repo);
    put_exec(at("f12/sw/mk.sh"), "git init -q -b main . && echo a > a.txt && git add a.txt && git -c user.name=t -c user.email=t@t commit -qm 1 &&\n"
                                 "git switch -q -c feat && echo f > f.txt && echo a2 > a.txt && git add -A && git -c user.name=t -c user.email=t@t commit -qm 2 &&\n"
                                 "git switch -q main && git branch other\n");
    CHECK(sh_in(repo, "sh ../mk.sh") == 0);
    CHECK(git_top(repo, top, sizeof top));
    git_gitdir(top, gitdir, sizeof gitdir);
    CHECK(git_switch(git, top, "feat", msg, sizeof msg) == GSW_DONE && git_head_is(gitdir, "feat") && access(at("f12/sw/repo/f.txt"), F_OK) == 0);
    CHECK(git_switch(git, top, "main", msg, sizeof msg) == GSW_DONE && access(at("f12/sw/repo/f.txt"), F_OK) != 0);
    // Refused: a local change the switch would overwrite; a branch that isn't. HEAD stays.
    put(at("f12/sw/repo/a.txt"), "mine\n");
    CHECK(git_switch(git, top, "feat", msg, sizeof msg) == GSW_REFUSED && strstr(msg, "local changes") && git_head_is(gitdir, "main"));
    CHECK(sh_in(repo, "git checkout -q a.txt") == 0);
    CHECK(git_switch(git, top, "nope", msg, sizeof msg) == GSW_REFUSED && strstr(msg, "nope") && git_head_is(gitdir, "main"));
    // Gone since the menu opened, a remote's branch of its name there: not made from it (--no-guess).
    CHECK(sh_in(repo, "git remote add origin ../nowhere && git update-ref refs/remotes/origin/ghost HEAD") == 0);
    CHECK(git_switch(git, top, "ghost", msg, sizeof msg) == GSW_REFUSED && git_head_is(gitdir, "main") && access(at("f12/sw/repo/.git/refs/heads/ghost"), F_OK) != 0);
    // A hook: it runs, and writes in the repository only.
    char pwned[PATH_MAX];
    snprintf(pwned, sizeof pwned, "%s", at("f12/sw/pwned"));
    snprintf(cmd, sizeof cmd, "#!/bin/sh\ntouch hooked\necho x > '%s' 2>/dev/null\ntouch /tmp/t && echo hook says hi >&2\nexit 0\n", pwned);
    put_exec(at("f12/sw/repo/.git/hooks/post-checkout"), cmd);
    CHECK(git_switch(git, top, "other", msg, sizeof msg) == GSW_DONE && access(at("f12/sw/repo/hooked"), F_OK) == 0 && access(pwned, F_OK) != 0);
    CHECK(strcmp(msg, "hook says hi") == 0);   // (its words, said on success; /tmp its own to write)
    box_sandbox = false;
    CHECK(git_switch(git, top, "main", msg, sizeof msg) == GSW_DONE && access(pwned, F_OK) == 0);
    box_sandbox = true;
    unlink(at("f12/sw/repo/.git/hooks/post-checkout")), unlink(at("f12/sw/repo/hooked"));
    // A worktree: its HEAD and index in the repository's .git (writable too).
    CHECK(sh_in(repo, "git worktree add -q ../wt -b wtb") == 0);
    CHECK(git_top(at("f12/sw/wt"), top, sizeof top));
    git_gitdir(top, gitdir, sizeof gitdir);
    CHECK(git_switch(git, top, "feat", msg, sizeof msg) == GSW_DONE && git_head_is(gitdir, "feat") && access(at("f12/sw/wt/f.txt"), F_OK) == 0);
    // Past its time: not stopped (a half switch), said, and it lands after.
    put_exec(git, "#!/bin/sh\nsleep 0.3\nexec /usr/bin/git \"$@\"\n");
    git_switch_ms = 100;
    CHECK(git_switch(git, top, "wtb", msg, sizeof msg) == GSW_GOING && strstr(msg, "carries on") && git_head_is(gitdir, "feat"));
    git_switch_ms = GIT_SWITCH_MS;
    for (int i = 0; i < 40 && !git_head_is(gitdir, "wtb"); i++) nanosleep(&(struct timespec){ 0, 50 * 1000 * 1000 }, NULL);
    CHECK(git_head_is(gitdir, "wtb"));
    CHECK(git_switch(git, top, "feat", msg, sizeof msg) == GSW_DONE);
    put_exec(git, "#!/bin/sh\nexec /usr/bin/git \"$@\"\n");
    // ofx gone in the middle of a switch: it still finishes (a slow git, the process asking it killed).
    put_exec(git, "#!/bin/sh\nsleep 0.3\nexec /usr/bin/git \"$@\"\n");
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGALRM, SIG_DFL);
        ualarm(100000, 0);
        git_switch(git, top, "wtb", msg, sizeof msg);
        _exit(0);
    }
    int st;
    waitpid(pid, &st, 0);
    for (int i = 0; i < 40 && !git_head_is(gitdir, "wtb"); i++) nanosleep(&(struct timespec){ 0, 50 * 1000 * 1000 }, NULL);
    CHECK(git_head_is(gitdir, "wtb"));
    CHECK(git_switch(git, top, "feat", msg, sizeof msg) == GSW_DONE);
    put_exec(git, "#!/bin/sh\nexec /usr/bin/git \"$@\"\n");
    // The step 5c review's half switch: a smudge filter that writes to stderr for each file, the
    // process asking killed early. The whole switch lands, its index.lock gone, nothing changed.
    char flt[PATH_MAX], ftop[PATH_MAX], fdir[PATH_MAX];
    snprintf(flt, sizeof flt, "%s", at("f12/sw/flt"));
    mkdirs(flt);
    put_exec(at("f12/sw/mkflt.sh"), "git init -q -b main . && git config filter.slow.smudge 'sh -c \"sleep 0.02; echo smudged >&2; cat\"' && git config filter.slow.clean cat &&\n"
                                    "echo '*.txt filter=slow' > .gitattributes && for i in 0 1 2 3 4 5 6 7 8 9; do echo m$i > f$i.txt; done && git add -A && git -c user.name=t -c user.email=t@t commit -qm m &&\n"
                                    "git switch -q -c b && for i in 0 1 2 3 4 5 6 7 8 9; do echo b$i > f$i.txt; done && git -c user.name=t -c user.email=t@t commit -qam b && git switch -q main\n");
    CHECK(sh_in(flt, "sh ../mkflt.sh") == 0);
    CHECK(git_top(flt, ftop, sizeof ftop));
    git_gitdir(ftop, fdir, sizeof fdir);
    pid = fork();
    if (pid == 0) {
        signal(SIGALRM, SIG_DFL);
        ualarm(50000, 0);
        git_switch(git, ftop, "b", msg, sizeof msg);
        _exit(0);
    }
    waitpid(pid, &st, 0);
    for (int i = 0; i < 60 && !git_head_is(fdir, "b"); i++) nanosleep(&(struct timespec){ 0, 50 * 1000 * 1000 }, NULL);
    char lock[PATH_MAX + 16];
    snprintf(lock, sizeof lock, "%s/index.lock", fdir);
    CHECK(git_head_is(fdir, "b") && access(lock, F_OK) != 0 && sh_in(flt, "test -z \"$(git status --porcelain)\" && grep -qx b9 f9.txt") == 0);
    // Not a name: an option; outside the guard's root: refused before git runs.
    CHECK(git_switch(git, top, "-f", msg, sizeof msg) == GSW_REFUSED && strstr(msg, "not a branch"));
    // (git itself inside the guard's root there: only the repository's place refuses it)
    char git2[PATH_MAX];
    snprintf(git2, sizeof git2, "%s", at("f12/sw/elsewhere/git"));
    put_exec(git2, "#!/bin/sh\nexec /usr/bin/git \"$@\"\n");
    ops_guard(at("f12/sw/elsewhere"));
    CHECK(git_switch(git2, top, "wtb", msg, sizeof msg) == GSW_REFUSED && msg[0] && git_head_is(gitdir, "feat"));
    ops_guard(root);
    env_restore(), env_restore();
}

// A job that outlives ofx (F12 step 5c: a switch): ofx gone in the middle, it still finishes; any
// other is killed with it. Each from a process a timer kills as it waits.
static pid_t outlive_start(const char *sub, bool outlive, char *done, size_t cap)
{
    char dir[PATH_MAX], prog[PATH_MAX + 8], text[3 * PATH_MAX];
    snprintf(dir, sizeof dir, "%s", at(sub));
    mkdirs(dir);
    snprintf(prog, sizeof prog, "%s/late", dir);
    snprintf(done, cap, "%s/done", dir);
    unlink(done);
    snprintf(text, sizeof text, "#!/bin/sh\nsleep 0.3\necho late\necho late >&2\ntouch '%s'\n", done);   // (it writes after its reader went: F12 step 5c review)
    put_exec(prog, text);
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGALRM, SIG_DFL);
        ualarm(100000, 0);
        const char *argv[] = { prog, NULL };
        static char out[64];
        isize len;
        BoxJob j = { .argv = argv, .rw = { dir }, .ms = 5000, .outlive = outlive, .out = out, .cap = sizeof out, .len = &len, .err = true };
        box_run(&j);
        _exit(0);
    }
    return pid;
}

// The four at once: in bubblewrap and as they are (then the death signal is what would end one).
static void test_box_outlive(void)
{
    ops_guard(root);
    char d[4][PATH_MAX + 8];
    static const char *const sub[4] = { "f12/ol/yes", "f12/ol/no", "f12/ol/yes-plain", "f12/ol/no-plain" };
    pid_t k[4];
    int st;
    for (int i = 0; i < 4; i++) {
        box_sandbox = i < 2;   // (the child's copy, at its fork)
        k[i] = outlive_start(sub[i], i % 2 == 0, d[i], sizeof d[i]);
    }
    box_sandbox = true;
    for (int i = 0; i < 4; i++) waitpid(k[i], &st, 0);
    for (int i = 0; i < 40 && (access(d[0], F_OK) != 0 || access(d[2], F_OK) != 0); i++) nanosleep(&(struct timespec){ 0, 50 * 1000 * 1000 }, NULL);   // (2 s at most)
    nanosleep(&(struct timespec){ 0, 150 * 1000 * 1000 }, NULL);   // (the others had as long, and more)
    CHECK(access(d[0], F_OK) == 0 && access(d[1], F_OK) != 0);
    CHECK(access(d[2], F_OK) == 0 && access(d[3], F_OK) != 0);
}

// An outlive job's output, read from its memfd as it ends (both streams); past its time, left
// running (BOX_GOING) and not killed.
static void test_box_outlive_out(void)
{
    ops_guard(root);
    char prog[PATH_MAX], done[PATH_MAX], text[3 * PATH_MAX], out[64];
    isize len = 0;
    mkdirs(at("f12/olo"));
    snprintf(prog, sizeof prog, "%s", at("f12/olo/say"));
    put_exec(prog, "#!/bin/sh\necho one\necho two >&2\n");
    const char *argv[] = { prog, NULL };
    BoxJob j = { .argv = argv, .ms = 3000, .outlive = true, .out = out, .cap = sizeof out, .len = &len, .err = true };
    CHECK(box_run(&j) == BOX_ENDED && str_eq((Str){ out, len }, S("one\ntwo\n")));
    snprintf(done, sizeof done, "%s", at("f12/olo/done"));
    snprintf(text, sizeof text, "#!/bin/sh\nsleep 0.3\ntouch '%s'\n", done);
    put_exec(prog, text);
    BoxJob k = { .argv = argv, .rw = { at("f12/olo") }, .ms = 100, .outlive = true, .out = out, .cap = sizeof out, .len = &len };
    CHECK(box_run(&k) == BOX_GOING && access(done, F_OK) != 0);
    for (int i = 0; i < 40 && access(done, F_OK) != 0; i++) nanosleep(&(struct timespec){ 0, 50 * 1000 * 1000 }, NULL);
    CHECK(access(done, F_OK) == 0);
}

// What git said (F12 step 5c): its last error or fatal line, without the word; else its last line.
static void test_git_said(void)
{
    char m[128];
    git_said(S("error: Your local changes would be overwritten:\n\ta.txt\nAborting\n"), m, sizeof m);
    CHECK(strcmp(m, "Your local changes would be overwritten:") == 0);
    git_said(S("hint: x\nfatal: invalid reference: nope\n"), m, sizeof m);
    CHECK(strcmp(m, "invalid reference: nope") == 0);
    git_said(S("something\n  last  \n\n"), m, sizeof m);
    CHECK(strcmp(m, "last") == 0);
    git_said(STR_EMPTY, m, sizeof m);
    CHECK(m[0] == 0);
}

// The window's timer (F12 step 5a review): the soonest due time, whatever its place; 0s are none.
static void test_due_min(void)
{
    CHECK(ui_due_min((i64[]){ 0, 0 }, 2) == 0);
    CHECK(ui_due_min((i64[]){ 0, 500, 0, 300 }, 4) == 300);
    CHECK(ui_due_min((i64[]){ 900, 0, 200, 700 }, 4) == 200);
    CHECK(ui_due_min((i64[]){ 100, 400 }, 2) == 100);
    CHECK(ui_due_min((i64[]){ 300, 0 }, 2) == 300);   // (a none after the soonest)
}

// The footer in a repository: the changes after the free space (F12 step 5); none said without.
static void test_git_footer(void)
{
    char buf[128];
    UiInfo in = { .n = 5, .avail = 38LL << 30, .one_count = -1, .one_size = -1, .changed = 6 };
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "5 items, 38 GiB free \xC2\xB7 6 changed") == 0);
    in.avail = -1;
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "5 items \xC2\xB7 6 changed") == 0);
    in.changed = 0;
    ui_status_info(buf, sizeof buf, &in);
    CHECK(strcmp(buf, "5 items") == 0);
}

// box_run's end is the program's and its output's: one that exits leaving something it started
// holding its output open past the time is killed, with its group (as run, not in bubblewrap, whose
// namespace ends with it).
static void test_box_output_end(void)
{
    ops_guard(root);
    char prog[PATH_MAX], tail[32], text[256];
    snprintf(prog, sizeof prog, "%s", at("f12/bin/leave"));
    snprintf(tail, sizeof tail, "3.%06d3", (int)getpid());
    snprintf(text, sizeof text, "#!/bin/sh\necho started\nsleep %s &\nexit 0\n", tail);
    put_exec(prog, text);
    const char *argv[] = { prog, NULL };
    char out[64];
    isize len = 0;
    BoxJob j = { .argv = argv, .ms = 200, .out = out, .cap = sizeof out, .len = &len };
    box_sandbox = false;
    CHECK(box_run(&j) == BOX_KILLED && !sleeping(tail));
    CHECK(len == 0 || (len == 8 && memcmp(out, "started\n", 8) == 0));   // (under load the shell may not have said it within the 200 ms)
    box_sandbox = true;
}
