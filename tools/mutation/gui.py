#!/usr/bin/env python3
"""GUI mutants: each applied to a scratch copy of the repo, built, and run through
tests/gui/run.sh, each script in its own headless weston (CLAUDE.md, "Testing the GUI").

    tools/mutation/gui.py <scratch-dir> [index,...] [--full]

The whole GUI suite takes about 86 s, so each mutant runs only the script groups that killed it
before (its tags, in gui-tags.txt): a grid mutant runs f62, not the copy jobs' f53. A mutant with
no tags (new, or its text changed) runs the whole suite, and the groups that fail become its
tags. --full runs every mutant on the whole suite and writes the tags again: before an install,
since a narrow run can't see a mutant break something its tags don't cover (Michael, 2026-10-24).
The unmutated build always runs the whole suite first.

Everything runs from the scratch copy, scripts included, so the repo can be edited meanwhile.
Each run's trees (about 50 MB: the copy tests' big files) are removed once it is judged, and the
run stops while there is still room: the scratch dir is usually /tmp, a tmpfs in RAM, and keeping
them filled it on 2026-10-24.
"""
import hashlib, os, re, shutil, subprocess, sys

G, W, K, J = 'gui/main.c', 'gui/watch.c', 'gui/counts.c', 'gui/jobs.c'
MUTANTS = [
    (G, "    go_to(up, str_from_c(child_buf), 0);", "    go_to(up, STR_EMPTY, 0);"),
    (G, "    if (go_to(app.cwd, str_from_c(keep), app.ui.list.sel)) return;", "    if (go_to(app.cwd, STR_EMPTY, app.ui.list.sel)) return;"),
    (G, "        memmove(up, d.p, (size_t)d.len);\n        up[d.len] = 0;", "        snprintf(up, sizeof up, \"%.*s\", STR_ARG(d));"),
    (G, "        atomic_store(&watch.list_ready, true);\n        glfwPostEmptyEvent();\n        SetWindowTitle", "        SetWindowTitle"),
    (W, "static const u32 WATCH_DIR_MASK = IN_CREATE | ", "static const u32 WATCH_DIR_MASK = "),
    (W, "    if (rewatch) watch.theme_wd = inotify_add_watch(watch.fd, watch.theme, WATCH_THEME_MASK);", ""),
    (W, "        if (theme) atomic_store(&watch.theme_ready, true);", ""),
    # F2
    (G, "    if (!same && app.have_list && !stepping) hist_leave(&hist, app.cwd, sel_name());\n", ""),
    (G, "    isize sel = listing_find(&app.list, str_from_c(keep));\n    ui_set(&app.ui.list, app.list.n, sel >= 0 ? sel : 0);", "    ui_set(&app.ui.list, app.list.n, 0);"),
    (G, "        go_to(app.cwd, str_from_c(keep), 0);\n        break;", "        go_to(app.cwd, STR_EMPTY, 0);\n        break;"),
    (G, "        if (in_box(CLAY_IDI(\"side-row\", r), x, y)) h.side = r;", "        if (0) h.side = r;"),
    (G, "        if (in_box(Clay_GetElementId(cz(IDS[k])), x, y)) h.header = k;", ""),
    (G, "(button == GLFW_MOUSE_BUTTON_5 || button == GLFW_MOUSE_BUTTON_6)) mouse_forwards++;", "(button == GLFW_MOUSE_BUTTON_6)) mouse_forwards++;"),
    (G, "        if (p) go_to(p, STR_EMPTY, 0);", ""),
    (G, "        hist_unstep(&hist, dir);\n", ""),
    # F3
    (G, "        if (k >= 0 && !mark[k]) mark[k] = 1, marked++;", ""),
    (G, "        if (app.ui.marked ? !app.ui.mark[i] : i != app.ui.list.sel) continue;\n        char p[PATH_MAX];", "        if (i != app.ui.list.sel) continue;\n        char p[PATH_MAX];"),
    (J, "            if (it->out != OUT_NEW) continue;", ""),
    (J, "    if (r->own_cut && !atomic_load(&j->cancel) && own_gen == r->own_gen) own.n = 0, clip_clear_own();", ""),
    (G, "if (!name_free(app.cwd, base, S_ISDIR(st.st_mode), true, true, name, sizeof name)) continue;", "if (!name_free(app.cwd, base, S_ISDIR(st.st_mode), false, true, name, sizeof name)) continue;"),
    (G, "        if (S_ISDIR(st.st_mode) && into_itself(src, app.cwd)) {", "        if (0) {"),
    (G, "            if (c->cut) {\n                here++;", "            if (0) {\n                here++;"),
    (G, "        undo_begin(&undo, app.edit_new ? UNDO_MKDIR : UNDO_RENAME);", "        undo_begin(&undo, UNDO_RENAME);"),
    (G, "    if (back[0]) go_to(app.cwd, str_from_c(back), app.ui.list.sel);\n    else reload();", "    reload();"),
    (G, "        return m == GLFW_MOD_SHIFT ? UK_SHIFT_DELETE : m == 0 ? UK_DELETE : UK_NONE;", "        return m == 0 ? UK_DELETE : UK_SHIFT_DELETE;"),
    (G, "    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_N) return UK_NEWFOLDER;", ""),
    (J, "        if (jobs.asking >= 0) app.ui.asking = true, app.ui.ask_all = false;", ""),
    (G, "    ops_guard(getenv(\"OFX_GUI_ROOT\"));", ""),
    (G, "        if (!was_editing && app.ui.editing && !app.ui.edit_path) snprintf(app.edit_from, sizeof app.edit_from, \"%.*s\", STR_ARG(sel_name())), app.edit_new = false;", ""),
    # F4
    (G, "    if (i + 1 < c->n) snprintf(below, sizeof below, \"%.*s\", STR_ARG(ui_crumb_label(c, i + 1)));", ""),
    (G, "    // The counts are by row too: asked again in the new order.\n    for (isize i = 0; i < app.list.n; i++) app.count[i] = COUNT_UNKNOWN;", "    // The counts are by row too: asked again in the new order."),
    (G, "        if (app.have_list && count_take(app.count, app.list.n)) dirty = true;", ""),
    (G, "    if (!S_ISDIR(st.st_mode)) {\n        Str d = path_dir(str_from_c(path));", "    if (0) {\n        Str d = path_dir(str_from_c(path));"),
    (G, "        if (in_box(CLAY_IDI(\"crumb\", i), x, y)) h.bar = UB_CRUMB, h.crumb = i;", ""),
    # Not run: the cursor's row drawing an edit box while the location line is open
    # (`&& !app.ui.edit_path` in layout_row) shows only on screen; location.png shows the names.
    (G, "        ui_crumbs_set(&app.crumbs, app.cwd, getenv(\"HOME\"));", ""),
    (G, "    if (m == GLFW_MOD_ALT && key == GLFW_KEY_UP) return UK_PARENT;", ""),
    (G, "    ui_edit_close(&app.ui);\n    if (name[0] == '.'", "    if (name[0] == '.'"),
    # Not run: counts.c drops an old listing's answer twice over (in count_loop and in count_take),
    # so taking out either check alone changes nothing a test can see.
    (G, "    if (app.count[i] >= 0) snprintf(size, cap, \"%td item%s\", app.count[i], app.count[i] == 1 ? \"\" : \"s\");", "    if (app.count[i] >= 0) snprintf(size, cap, \"%td items\", app.count[i]);"),
    # F5
    (G, "    ui_mark_clear(&app.ui);   // the same folder in two tabs: this tab's marks, not the other's", ""),
    (G, "        if (k >= 0) ui_mark(&app.ui, k, true);\n        at += nm.len + 1;", "        at += nm.len + 1;"),
    (G, "    app.ui.list.top = t->top;\n", ""),
    (G, "    t->hist = hist, t->crumbs = app.crumbs;", "    t->crumbs = app.crumbs;"),
    (G, "        go_up_from(t->dir);\n        if (there)", "        if (there)"),
    (G, "    if (tabbar.n == 1) {\n        app.quit = true;\n        return;\n    }", ""),
    (G, "    if (shown) tab_load();", ""),
    (G, "    if (strcmp(path, TRASH_URI) != 0 && (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))) {", "    if (strcmp(path, TRASH_URI) != 0 && stat(path, &st) != 0) {"),
    (G, "    if (h.tab >= 0) in->tab_close = h.tab;\n    else if (h.row >= 0)", "    if (h.row >= 0)"),
    (G, "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_W) return UK_TAB_CLOSE;", ""),
    (G, "    if (action == GLFW_PRESS && m == GLFW_MOD_ALT && key >= GLFW_KEY_1 && key <= GLFW_KEY_9)", "    if (action == GLFW_PRESS && key >= GLFW_KEY_1 && key <= GLFW_KEY_9)"),
    (G, "    return HEADER_H + (tabbar.n >= 2 ? TAB_H : 0);", "    return HEADER_H;"),
    (G, "    case UA_TAB_MOVE: ui_tabs_move(&tabbar, (int)a.index); break;", "    case UA_TAB_MOVE: break;"),
    # F5 review: the delete question's targets taken when it opens
    (G, "    isize k = 0;\n    for (isize i = 0; i < n; i++) {\n        r->item[k++] = (JobItem){ .src = arena_cstr(&r->arena, str_from_c(doomed.path[i])) };", "    isize k = 0;\n    char *late[CLIP_PATHS_MAX];\n    n = targets(&r->arena, late, CLIP_PATHS_MAX);\n    for (isize i = 0; i < n; i++) {\n        r->item[k++] = (JobItem){ .src = late[i] };"),
    (J, "    if (undo.n == 0) undo_clear(&undo);\n    }", "    if (undo.n == 0) undo_clear(&undo);\n    }\n    if (j->kind == JOB_DELETE) ui_mark_clear(&app.ui);"),
    (G, "        if (!was_confirming && (app.ui.confirming || a.kind == UA_DELETE)) delete_ask();", "        if (!was_confirming && app.ui.confirming) delete_ask();"),
    (G, "    doomed.n = 0;\n    if (app.ui.marked > CLIP_PATHS_MAX) {", "    if (app.ui.marked > CLIP_PATHS_MAX) {"),
    # F5.1: the modal panels
    (G, "        for (int k = 0; k < COUNT(MB) && !h.modal; k++)\n            if (in_box(Clay_GetElementId(cz(MB[k].id)), x, y)) h.modal = MB[k].b;\n        return h;", "        return h;"),
    # Not run: hit_at's early return while a panel is up; ui_handle drops every other click during a
    # prompt anyway, so leaving it out changes nothing a test can see.
    (J, "        if (jobs.asking >= 0) app.ui.asking = true, app.ui.ask_all = false;", "        if (jobs.asking >= 0) app.ui.asking = true;"),
    # F5.2: the edit lines' text clipboard, the path buttons by keyboard. Not run: clipboard.c's
    # data-control text (clip_set_text, clip_get_text, clip_get's "our text holds no files"):
    # weston has no data-control, so it is the live check's.
    (G, "        case GLFW_KEY_LEFT: return UK_SEL_LEFT;\n", ""),
    (G, "        case GLFW_KEY_KP_ENTER: return UK_SHIFT_ENTER;\n", ""),
    (G, "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_K) return UK_CRUMBS;\n", ""),
    (G, "    if (app.ui.crumb_focus) ui_crumbs_show(c, wd, room, ARROW_W, app.ui.crumb_at);\n", ""),
    (G, "        in.crumb_n = app.crumbs.n, in.crumb_last = app.crumb_hi - 1;", "        in.crumb_n = app.crumbs.n, in.crumb_last = app.crumb_hi;"),
    (G, "        if (pasting && app.ui.editing) in.paste = text_paste();\n", ""),
    (G, "        if (app.ui.copied) text_copy((Str){ app.ui.copy, app.ui.copy_len }), app.ui.copied = false;", "        app.ui.copied = false;"),
    (G, "    if (!clip_set_text(t)) SetClipboardText(app.text_copied);\n    own.n = 0;", "    if (!clip_set_text(t)) SetClipboardText(app.text_copied);"),
    (G, "    if (own.n > 0) return clip_payload(&pastea, &own, CLIP_FMT_TEXT);\n", ""),
    (G, "    if (!go_to(dir, str_from_c(name), 0)) return;", "    if (!go_to(dir, STR_EMPTY, 0)) return;"),
    (G, "    if (name[0] == '.' && !app.show_hidden) set_status(", "    if (0) set_status("),
    (J, "!atomic_load(&j->cancel) && own_gen == r->own_gen)", "!atomic_load(&j->cancel))"),
    # F5.3: the jobs. Not run: the clash shown only while no other panel is up (no test makes a
    # clash come during the delete question); the earliest job's clash first when two wait at once
    # (which job's worker gets there first is the scheduler's); Ctrl+Q and the window's close
    # asking (a scripted window quits at once, and weston has no close to send).
    (J, "    if (jobs.nwait >= JOBQ_WAITING) {", "    if (jobs.nwait > JOBQ_WAITING) {"),
    (J, "    jobs.wait[jobs.nwait++] = job_index(r);\n    jobs_schedule();", "    jobs.wait[jobs.nwait++] = job_index(r);"),
    (J, "    if (finished) jobs_schedule();", ""),
    (J, "        p[k++] = last_dir;", ""),
    (J, "        atomic_store(&r->j.cancel, true);", ""),
    (J, "    jobs.asking = -1;\n}", "}"),
    (J, "    if (gone) app.ui.cancel_asking = false;   // it ended meanwhile", ""),
    (J, "    for (int i = 0; i < jobs.nrun; i++) job_cancel(job_recs[jobs.run[i]].id);", ""),
    (J, "        jobline_sample(&r->line, now,", "        if (0) jobline_sample(&r->line, now,"),
    (G, "    int room = h - top_h() - FOOTER_H - jobs_h() - (app.ui.in_trash", "    int room = h - top_h() - FOOTER_H - (app.ui.in_trash"),
    (G, "        if (in_box(CLAY_IDI(\"job-x\", i), x, y)) h.job_x = i;", ""),
    (G, "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_J) return UK_JOBS;\n", ""),
    (G, "    if (app.ui.cancel_asking) return MODAL_CANCEL;", ""),
    (G, "        for (int k = 0; k < jobs.nrun; k++) in.job_id[k] = job_recs[jobs.run[k]].id;", ""),
    # F5.3 review fixes. Not run: the refusals in rename, new folder and undo (the same
    # jobs_refuse as trash's), and the timed join at quit (it needs a worker stuck in a hung mount).
    (J, "    if (undo_gen != r->undo_gen) {", "    if (0) {"),
    (G, "    for (isize i = 0; i < n; i++)\n        if (jobs_refuse(paths[i])) return;\n", ""),
    # F5.4: a theme switch as omarchy-theme-set makes it
    (W, "        if (e->wd == watch.current_wd) *theme = true, rewatch = true;", "        if (e->wd == watch.current_wd) rewatch = true;"),
    (W, "        if (e->wd == watch.theme_wd && !(e->mask & IN_IGNORED)) *theme = true;", ""),
    (G, "    app.theme = theme_load(&scratch);\n    icons_reset();", "    app.theme = theme_load(&scratch);"),
    # F6
    (W, "        if (watch.mount_wd >= 0 && e->wd == watch.mount_wd) *mounts = true;\n", ""),
    (G, "        if (atomic_exchange(&watch.mounts_ready, false)) places_reload(), dirty = true;\n", ""),
    (G, "    tree_refresh(!same, app.ui.side.sel);\n", "    tree_refresh(false, app.ui.side.sel);\n"),
    (G, "    if (in_trash) trash_scan();\n", ""),
    (G, "        if (app.ui.in_trash && str_eq(path_base(files), S(\"files\")))\n            r->item[k++]", "        if (0)\n            r->item[k++]"),
    (G, "        job_submit(r, JOB_DELETE, k);\n        r->trash = JOB_TRASH_EMPTY;", "        r->trash = JOB_TRASH_EMPTY;"),
    (G, "    app.ui.in_trash = in_trash;\n", ""),
    (G, "    case UA_RESTORE: trash_restore_targets(); break;\n", ""),
    (G, "        if (ui_input_acts(&in) && ui_status_clears(app.status_bad, app.status_at, now_ms())) app.status[0] = 0;", ""),
    (G, "        if (state_path(path, sizeof path) && !state_save(path, &st, &err)) set_status(true, \"%s\", err.msg);\n        break;\n    }\n    case UA_VIEW", "        break;\n    }\n    case UA_VIEW"),
    (G, "    if (state_path(state_file, sizeof state_file)) {\n        State st", "    if (0) {\n        State st"),
    (G, "    case UA_SIDE_MODE: {\n        side_refresh();", "    case UA_SIDE_MODE: {"),
    (G, "        if (in_box(CLAY_IDI(\"mode\", k), x, y)) h.mode = k;", "        if (0) h.mode = k;"),
    (J, "    if (r->trash == JOB_TRASH_ITEMS) done /= 2, shown_n /= 2;", ""),
    (G, "    if (kind_is_dir(e->kind) && e->trash) {", "    if (0) {"),
    (G, "        if (in_trash && trash_home(tinfo, sizeof tinfo - 8)) strcat(tinfo, \"/info\");", ""),
    (G, "    if (app.ui.side_mode == SIDE_PLACES && app.ui.side.sel < app.places.n) keep = app.places.p[app.ui.side.sel];", "    if (app.places.n) keep = app.places.p[app.ui.side.sel];"),
    # F6 review
    (G, "        if (space_take(&app.places, app.cwd, &app.cwd_avail)) dirty = true;", ""),
    (G, "    space_ask(&app.places, app.cwd);\n    return true;", "    return true;"),
    (G, ".src = arena_cstr(&r->arena, str_from_c(doomed_all[at + i].path)), .after_ok = doomed_all[at + i].after_ok };", ".src = arena_cstr(&r->arena, str_from_c(doomed_all[at + i].path)) };"),
    # F6.1
    (G, "            if (drive) ui_size(avail, sizeof avail, hover ? p->size - p->avail : p->avail);", "            if (drive) ui_size(avail, sizeof avail, p->avail);"),
    (G, "                if (hover && place_ejectable(p)) {", "                if (0) {"),
    (G, "    if (h.side >= 0 && app.ui.side_mode == SIDE_PLACES && in_box(CLAY_IDI(\"side-eject\", h.side), x, y)) h.eject = h.side;", ""),
    (G, "        if (eject_take()) dirty = true;", ""),
    ('gui/eject.c', "    if (jobs_refuse(p->path)) return false;   // a job of ours writes there: it says so", ""),
    (G, "    if (chdir(\"/\") != 0) fprintf(stderr, \"ofx: chdir /: %s\\n\", strerror(errno));", ""),
    # F6.2: the icon view
    (G, "    int c = (w - side_w() - 2 * PAD) / CELL_W;", "    int c = (w - 2 * PAD) / CELL_W;"),
    (G, "    int r = app.ui.grid ? (room - PAD) / CELL_H : (room - COLS_H) / ROW_H;", "    int r = (room - COLS_H) / ROW_H;"),
    (G, "    for (int r = 0; r < list_cells() && h.row < 0; r++)", "    for (int r = 0; r < app.ui.list.rows && h.row < 0; r++)"),
    (G, "    if (!same && app.nslots >= app.icons_max) icons_reset(), app.icon_full_resets++;\n", ""),
    (G, "        if (rows != app.ui.list.rows) ui_resize(&app.ui.list, rows), dirty = true;\n", ""),
    (G, "    if (!app.ui.grid) return 1;\n    int c = (w", "    int c = (w"),
    (G, "        State st = { app.ui.side_mode, app.ui.grid };\n        if (state_path(path, sizeof path) && !state_save(path, &st, &err)) set_status(true, \"%s\", err.msg);\n        break;\n    }\n    case UA_BACK", "        break;\n    }\n    case UA_BACK"),
    (G, "        app.ui.side_mode = st.side, app.ui.grid = st.icons;", "        app.ui.side_mode = st.side;"),
    (G, "    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_1 || key == GLFW_KEY_KP_1)) return UK_VIEW_ICONS;", ""),
    (G, "    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_2 || key == GLFW_KEY_KP_2)) return UK_VIEW_LIST;", ""),
    (G, "    return app.ui.grid ? GRID_ICON_PX : ICON_PX;", "    return ICON_PX;"),
    (G, "            int n = name_lines(\"cell-line\", k, e->name, 2, name_w, fg, &cut);", "            int n = name_lines(\"cell-line\", k, e->name, CELL_LINES_FULL, name_w, fg, &cut);"),
    (G, "        isize first = app.ui.list.top + (isize)r * cols;", "        isize first = app.ui.list.top + r;"),
    (G, "        if (cols != (int)ui_cols(&app.ui.list)) ui_set_cols(&app.ui.list, cols), dirty = true;", ""),
    (G, "{ \"view-list\", UB_VIEW_LIST }, { \"view-icons\", UB_VIEW_ICONS },", "{ \"view-list\", UB_VIEW_LIST },"),
    (G, "            if (app.have_list && app.ui.grid) layout_grid();\n", ""),
    (G, "    if (strmap_get(&app.slot_of, slot_key, &k))", "    if (strmap_get(&app.slot_of, key, &k))"),
    # F6.2 keys (Michael, 2026-10-24): Ctrl and an arrow, Ctrl+S
    (G, "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_LEFT) return UK_SIDE;\n", ""),
    (G, "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_UP) return UK_CRUMBS;\n", ""),
    (G, "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_DOWN) return UK_JOBS;\n", ""),
    (G, "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_RIGHT) return UK_LOCATION;\n", ""),
    (G, "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_S) return UK_F9;\n", ""),
    # tooltips (Michael, 2026-10-24)
    (G, "        timer_at(ui_tip_due(&app.tip));\n", ""),
    (G, "        if (ui_tip_tick(&app.tip, now_ms())) dirty = true;\n", ""),
    (G, "        if (ui_input_acts(&in) && ui_tip_act(&app.tip)) seen = true;   // a click or key: the tooltip goes\n", ""),
    (G, "        pthread_cond_timedwait(&timer.cv, &timer.mu, &ts);", "        pthread_cond_wait(&timer.cv, &timer.mu);"),
    (G, "    if (h.mode >= 0) return TIP_MODE + h.mode;\n", ""),
]
args = [a for a in sys.argv[1:] if a != '--full']
full = len(args) != len(sys.argv) - 1
scratch = args[0]
only = [int(x) for x in args[1].split(',')] if len(args) > 1 else None
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
tags_path = os.path.join(root, 'tools', 'mutation', 'gui-tags.txt')
work = os.path.join(scratch, 'gui-mut-src')
shutil.rmtree(work, ignore_errors=True)
shutil.copytree(root, work, ignore=shutil.ignore_patterns('.git', 'build'))
os.makedirs(os.path.join(work, 'build'))
os.symlink(os.path.join(root, 'build', 'raylib'), os.path.join(work, 'build', 'raylib'))
run_sh = os.path.join(work, 'tests/gui/run.sh')
for f, old, new in MUTANTS:
    n = open(os.path.join(work, f)).read().count(old)
    assert n == 1, (f, old, n)

# A mutant's tag key: its file and texts, so a mutant edited is a new one, untagged.
def key(m):
    return hashlib.sha1('\0'.join(m).encode()).hexdigest()[:12]

tags = {}
if os.path.exists(tags_path):
    for line in open(tags_path):
        w = line.split('#')[0].split()
        if len(w) >= 2:
            tags[w[0]] = w[1:]

# The group of a script that failed: run.sh prints "== <script>: FAILED" or "<group>: FAILED".
GROUP_OF = {'f6b': 'f6', 'f62b': 'f62', 'f62c': 'f62'}
def failed_groups(out):
    g = []
    for l in out.splitlines():
        m = re.match(r'(?:== )?([a-z0-9]+): FAILED', l)
        if m and GROUP_OF.get(m[1], m[1]) not in g:
            g.append(GROUP_OF.get(m[1], m[1]))
    return g

# The unmutated build first, on the whole suite: if it fails, every mutant would count as killed
# (as they did while f6 couldn't start, from fe01f7c to e8c2c57).
b = subprocess.run(['make', '-s', 'build/ofx'], cwd=work, capture_output=True, text=True)
r = subprocess.run([run_sh, os.path.join(scratch, 'gm-base'), os.path.join(work, 'build/ofx')], capture_output=True, text=True)
if b.returncode != 0 or r.returncode != 0:
    print('the unmutated build fails tests/gui/run.sh: fix that first')
    print(b.stderr[-300:], '\n'.join(l for l in r.stdout.splitlines() if 'FAILED' in l or (l.startswith('script:') and not l.startswith('script: 0 of'))))
    sys.exit(2)
shutil.rmtree(os.path.join(scratch, 'gm-base'), ignore_errors=True)
print('base: tests/gui/run.sh passes unmutated', flush=True)
MIN_FREE = 2 << 30   # bytes left in the scratch dir's file system before a mutant starts
survivors, retagged = [], 0
for i, m in enumerate(MUTANTS):
    if only is not None and i not in only:
        continue
    if shutil.disk_usage(scratch).free < MIN_FREE:
        print(f'stopped before #{i}: less than 2 GiB free in {scratch}')
        break
    f, old, new = m
    k = key(m)
    groups = [] if full else tags.get(k, [])
    path = os.path.join(work, f)
    text = open(path).read()
    open(path, 'w').write(text.replace(old, new, 1))
    b = subprocess.run(['make', '-s', 'build/ofx'], cwd=work, capture_output=True, text=True)
    if b.returncode != 0:
        print(f'#{i} build failed: {b.stderr[-300:]}')
        open(path, 'w').write(text)
        continue
    r = subprocess.run([run_sh, os.path.join(scratch, f'gm{i}'), os.path.join(work, 'build/ofx')] + groups, capture_output=True, text=True)
    open(path, 'w').write(text)
    shutil.rmtree(os.path.join(scratch, f'gm{i}'), ignore_errors=True)
    if r.returncode != 0 and not groups:
        tags[k] = failed_groups(r.stdout) or ['all']   # 'all': it failed without naming a group
        retagged += 1
    verdict = 'killed' if r.returncode != 0 else 'SURVIVED'
    fails = [l for l in r.stdout.splitlines() if l.startswith('script:') and not l.startswith('script: 0 of')]
    ran = ' '.join(groups) if groups else 'all'
    print(f'#{i} {verdict} [{ran}] {f}: {old.strip()[:60]!r}  {fails[:1]}', flush=True)
    if r.returncode == 0:
        survivors.append(i)
if retagged:
    live = {key(m) for m in MUTANTS}
    with open(tags_path, 'w') as out:
        out.write('# tools/mutation/gui.py: each GUI mutant (its key) and the run.sh groups that kill it\n')
        for k in sorted(tags):
            if k in live and tags[k] != ['all']:
                out.write(f'{k} {" ".join(tags[k])}\n')
    print(f'{retagged} mutants tagged in {tags_path}')
shutil.rmtree(work, ignore_errors=True)
print(f'{len(MUTANTS)} GUI mutants, {len(survivors)} survived: {survivors}')
