#!/usr/bin/env python3
"""GUI mutants: each applied to a scratch copy of the repo, built, and run through
tests/gui/run.sh, each script in its own headless weston (CLAUDE.md, "Testing the GUI").

    tools/mutation/gui.py <scratch-dir> [index,...] [--full] [--jobs N]

The whole GUI suite takes about 90 s, so each mutant runs only the script groups that killed it
before (its tags, in gui-tags.txt): a grid mutant runs f62, not the copy jobs' f53. Groups run one
at a time and a mutant stops at the first that kills it (Michael, 2026-10-25). A mutant with no
tags (new, or its text changed) tries its hint first (a fourth field, `'f7'`), then the rest; the
group that kills it becomes its tag. --jobs N (6 by default) runs N mutants at once, each in its
own copy of the repo: the unmutated build runs the whole suite in all N at once first, so a
script that fails under that load stops the run instead of killing mutants it doesn't see (3 of 3
such runs passed at 4, 6 and 8 each, 2026-10-25; 6 is as fast as 8). Mutants that didn't build, or
weren't run for want of disk, are named in the summary and fail the run. --full runs every mutant on the whole suite and writes the tags again: before installing the
last step of a major milestone (F6 to F7), since a narrow run can't see a mutant break something its
tags don't cover; the installs between run the tagged mutants (Michael, 2026-10-24).
The unmutated build always runs the whole suite first.

Everything runs from the scratch copy, scripts included, so the repo can be edited meanwhile.
Each run's trees (about 50 MB: the copy tests' big files) are removed once it is judged, and the
run stops while there is still room: the scratch dir is usually /tmp, a tmpfs in RAM, and keeping
them filled it on 2026-10-24.
"""
import hashlib, os, re, shutil, subprocess, sys

G, W, K, J, TB, WK, OW = 'gui/main.c', 'gui/watch.c', 'gui/counts.c', 'gui/jobs.c', 'gui/thumbs.c', 'gui/walk.c', 'gui/openwith.c'
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
    (G, "        undo_begin(&undo, app.edit_new ? app.edit_new : UNDO_RENAME);", "        undo_begin(&undo, UNDO_RENAME);"),
    (G, "    if (back[0]) go_to(app.cwd, str_from_c(back), app.ui.list.sel);\n    else reload();", "    reload();"),
    (G, "        return m == GLFW_MOD_SHIFT ? UK_SHIFT_DELETE : m == 0 ? UK_DELETE : UK_NONE;", "        return m == 0 ? UK_DELETE : UK_SHIFT_DELETE;"),
    (G, "    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_N) return UK_NEWFOLDER;", ""),
    (J, "        if (jobs.asking >= 0) app.ui.asking = true, app.ui.ask_all = false;", ""),
    (G, "    ops_guard(getenv(\"OFX_GUI_ROOT\"));", ""),
    (G, "        if (!was_editing && app.ui.editing && !app.ui.edit_path) snprintf(app.edit_from, sizeof app.edit_from, \"%.*s\", STR_ARG(sel_name())), app.edit_new = UNDO_NONE;", ""),
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
    (G, "    return MENU_H + HEADER_H + (tabbar.n >= 2 ? TAB_H : 0);", "    return MENU_H + HEADER_H;"),
    (G, "    case UA_TAB_MOVE: ui_tabs_move(&tabbar, (int)a.index); break;", "    case UA_TAB_MOVE: break;"),
    # F5 review: the delete question's targets taken when it opens
    (G, "    isize k = 0;\n    for (isize i = 0; i < n; i++) {\n        r->item[k++] = (JobItem){ .src = arena_cstr(&r->arena, str_from_c(doomed.path[i])) };", "    isize k = 0;\n    char *late[CLIP_PATHS_MAX];\n    n = targets(&r->arena, late, CLIP_PATHS_MAX);\n    for (isize i = 0; i < n; i++) {\n        r->item[k++] = (JobItem){ .src = late[i] };"),
    (J, "    if (undo.n == 0) undo_clear(&undo);\n    }", "    if (undo.n == 0) undo_clear(&undo);\n    }\n    if (j->kind == JOB_DELETE) ui_mark_clear(&app.ui);"),
    (G, "        if (!was_confirming && (app.ui.confirming || a.kind == UA_DELETE)) delete_ask();", "        if (!was_confirming && app.ui.confirming) delete_ask();"),
    (G, "    doomed.n = 0;\n    if (app.ui.marked > CLIP_PATHS_MAX) {", "    if (app.ui.marked > CLIP_PATHS_MAX) {"),
    # F5.1: the modal panels
    (G, "        for (int k = 0; k < COUNT(MB) && !h.modal; k++)\n            if (in_box(Clay_GetElementId(cz(MB[k].id)), x, y)) h.modal = MB[k].b;\n        for (int k = 0; modal_kind()", "        for (int k = 0; modal_kind()"),
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
    (G, "        if (state_path(path, sizeof path) && !state_put(&scratch, path, \"sidebar\", SIDE_MODE_NAMES[app.ui.side_mode], &err)) set_status(true, \"%s\", err.msg);\n", ""),
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
    (G, "    int c = (w - side_w() - 2 * PAD) / cell_w();", "    int c = (w - 2 * PAD) / cell_w();"),
    (G, "    int r = app.ui.grid ? (room - PAD) / cell_h() : (room - COLS_H) / ROW_H;", "    int r = (room - COLS_H) / ROW_H;"),
    (G, "    for (int r = 0; r < list_cells() && h.row < 0; r++)", "    for (int r = 0; r < app.ui.list.rows && h.row < 0; r++)"),
    (G, "    if (!same && app.nslots >= app.icons_max) icons_reset(), app.icon_full_resets++;\n", ""),
    (G, "        if (rows != app.ui.list.rows) ui_resize(&app.ui.list, rows), dirty = true;\n", ""),
    (G, "    if (!app.ui.grid) return 1;\n    int c = (w", "    int c = (w"),
    (G, "        if (state_path(path, sizeof path) &&\n            !(a.kind == UA_ZOOM ?", "        if (false &&\n            !(a.kind == UA_ZOOM ?"),
    (G, "        app.ui.side_mode = st.side, app.ui.grid = st.icons, app.ui.zoom = st.zoom;", "        app.ui.side_mode = st.side, app.ui.zoom = st.zoom;"),
    (G, "    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_1 || key == GLFW_KEY_KP_1)) return UK_VIEW_LIST;", ""),
    (G, "    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_2 || key == GLFW_KEY_KP_2)) return UK_VIEW_ICONS;", ""),
    (G, "    return app.ui.grid ? grid_px() : ICON_PX;", "    return ICON_PX;"),
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
    # F6.3: the icon view's sizes
    (G, "    return w > CELL_W_MIN ? w : CELL_W_MIN;", "    return w;"),
    (G, "    return grid_px() + CELL_H_ROOM;", "    return 48 + CELL_H_ROOM;"),
    (G, "    return app.ui.grid ? grid_px() : ICON_PX;", "    return app.ui.grid ? 48 : ICON_PX;"),
    (G, "        app.ui.side_mode = st.side, app.ui.grid = st.icons, app.ui.zoom = st.zoom;", "        app.ui.side_mode = st.side, app.ui.grid = st.icons;"),
    (G, "state_put(&scratch, path, \"zoom\", zoom, &err) :", "state_put(&scratch, path, \"view\", app.ui.grid ? \"icons\" : \"list\", &err) :"),
    (G, "        if (img.data && big > want) ImageResize(", "        if (0) ImageResize("),
    (G, "        if (app.zoomed) ui_refit(&app.ui.list, cols, rows), app.zoomed = false, dirty = true;\n", ""),
    (G, "        if (a.kind == UA_ZOOM) app.zoomed = true;\n", ""),
    (G, "    int k = (int)lroundf((x - d.boundingBox.x - ZOOM_KNOB_W / 2.0f) / step);", "    int k = (int)((x - d.boundingBox.x - ZOOM_KNOB_W / 2.0f) / step);"),
    (G, "    if (h.zoom) in->zoom_to = zoom_level_at(x), app.zoom_drag = true;", "    if (h.zoom) in->zoom_to = zoom_level_at(x);"),
    (G, "    in->zoom_to = zoom_level_at(x);\n    return false;", "    return false;"),
    (G, "    if (x >= side_w() && ctrl) in->zoom_wheel = notches;", "    if (0) in->zoom_wheel = notches;"),
    (G, "    if ((m == GLFW_MOD_CONTROL || m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT)) && (key == GLFW_KEY_EQUAL", "    if ((m == GLFW_MOD_CONTROL) && (key == GLFW_KEY_EQUAL"),
    (G, " (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT)) return UK_ZOOM_OUT;", " key == GLFW_KEY_MINUS) return UK_ZOOM_OUT;"),
    (G, "            if (app.ui.grid) layout_zoom();\n", ""),
    (G, "    Clay_FloatingAttachPoints at = above && right ?", "    Clay_FloatingAttachPoints at = 0 ?"),
    (G, "    if (h.zoom) return TIP_ZOOM;\n", ""),
    (G, "        { UB_ZOOM_IN, \"zoom-in\", \"Bigger icons (Ctrl+=)\" },", ""),
    # the hover for the pointer where it is (Michael, 2026-10-24)
    (G, "    return on ? hover_at(x, y) : hover_at(-1e6f, -1e6f);", "    return hover_at(x, y);"),
    (G, "            if (scripted ? script.pointing && hover_refresh(true, script.px, script.py) : hover_refresh(IsCursorOnScreen(), m.x, m.y))\n                dirty = true, glfwPostEmptyEvent();\n", ""),
    # a full icon cache at a zoom (reviewer-2, F6.3 review)
    (G, "        if (app.icons_missed && app.icons_reset_armed) icons_reset()", "        if (app.icons_missed) icons_reset()"),
    (G, "        app.icons_reset_armed = true;   // the other", "        ;   // the other"),
    (G, "return app.icons_missed = true, arena_restore(&scratch, mark), NULL;", "return arena_restore(&scratch, mark), NULL;"),
    # F6.4: previews in the icon view
    (TB, "        if (thumbs.e[i].state == TS_ASKED) thumbs.e[i].state = TS_UNKNOWN;\n", ""),
    (TB, "    if ((e->state == TS_UNKNOWN && (draw || kind != TK_NONE)) || (draw && e->state == TS_DROPPED)) {", "    if (e->state == TS_UNKNOWN || e->state == TS_DROPPED) {"),
    (TB, "    if ((e->state == TS_UNKNOWN && (draw || kind != TK_NONE)) || (draw && e->state == TS_DROPPED)) {", "    if (e->state == TS_UNKNOWN || (draw && e->state == TS_DROPPED)) {"),
    (TB, "    if (draw) e->drawn = thumbs.frame, thumbs.drawn_now++;", "    if (draw) thumbs.drawn_now++;"),
    (TB, "thumbs.e[i].state == TS_SHOWN && thumbs.e[i].drawn < thumbs.frame && (old < 0", "thumbs.e[i].state == TS_SHOWN && (old < 0"),
    (TB, "    while (thumbs.held > thumbs.held_max) {", "    while (0) {"),
    (TB, "    if (e->mtime != mtime && e->state != TS_ASKED) {", "    if (0) {"),
    (TB, "        if ((got == TG_CACHED || got == TG_MADE) && out.w <= THUMB_PX_MAX", "        if (got == TG_MADE && out.w <= THUMB_PX_MAX"),
    (TB, "                             .make = local && kind != TK_NONE && size <= THUMB_SRC_MAX, .px = px };", "                             .make = kind != TK_NONE && size <= THUMB_SRC_MAX, .px = px };"),
    (G, "    app.thumb_local = thumb_local(read_file(&scratch, mountinfo_path()), dir);", "    app.thumb_local = true;"),
    (G, "    for (isize i = ahead; i < ahead + (isize)app.ui.list.rows * cols && i < app.list.n; i++) entry_thumb(i, false);\n", ""),
    (G, "        Texture2D *th = entry_thumb(i, true);", "        Texture2D *th = NULL;"),
    (G, "CLAY_SIZING_FIXED(th->width / s), CLAY_SIZING_FIXED(th->height / s)", "CLAY_SIZING_FIXED(px), CLAY_SIZING_FIXED(px)"),
    # ---- F7.1: the menu bar, New Window, Create New Empty File, Close All Windows
    (G, "            if (UI_MENU[app.ui.menu].items[i] != UM_SEP && in_box(CLAY_IDI(\"menu-item\", i), x, y)) h.menu_item = i;\n        return h;", "            if (UI_MENU[app.ui.menu].items[i] != UM_SEP && in_box(CLAY_IDI(\"menu-item\", i), x, y)) h.menu_item = i;", 'f7'),
    (G, "        in->menu_away = h.menu_title < 0 && h.menu_item < 0;", "        in->menu_away = false;", 'f7'),
    (G, "    changed |= ui_menu_hover(&app.ui, h.menu_title, h.menu_item);", "", 'f7'),
    (G, "    app.menu_title_hover = h.menu_title;\n", "", 'f7'),
    (G, "            if (in_box(CLAY_IDI(\"menu-title\", m), x, y)) h.menu_title = m;", "", 'f7'),
    (G, "    app.ui.can_write = !in_trash && access(dir, W_OK) == 0;", "    app.ui.can_write = !in_trash;", 'f7'),
    (G, "    app.edit_new = UNDO_MKFILE;", "    app.edit_new = UNDO_MKDIR;", 'f7'),
    (G, "    if (!op_mkfile(app.cwd, S(\"New file\"), made, sizeof made, &err))", "    if (!op_mkdir(app.cwd, S(\"New file\"), made, sizeof made, &err))", 'f7'),
    (G, "    case UA_NEWFILE: new_file(); break;", "    case UA_NEWFILE: new_folder(); break;", 'f7'),
    (G, "        if (spawn_window(app.exe, app.cwd, &err))", "        if (spawn_window(\"ofx\", app.cwd, &err))", 'f7'),
    (G, "    if (app.windows[0]) win_signal_others(app.windows, getpid(), SIGUSR1);", "", 'f7'),
    (G, "    if (quit && jobs.nrun + jobs.nwait > 0) close_asked = true;", "", 'f7'),
    (G, "        if (sig == SIGUSR1) {", "        if (sig == SIGUSR2) {", 'f7'),
    (G, "        if (atomic_exchange(&close_signal, false)) close_asked = true;", "", 'f7'),
    (G, "    if (win_dir(app.windows, sizeof app.windows) && !win_register(app.windows, getpid(), &werr)) {", "    if (win_dir(app.windows, sizeof app.windows) && false) {", 'f7'),
    (G, "    if (app.windows[0]) win_unregister(app.windows, getpid());", "", 'f7'),
    (G, "    sigaddset(&quit_signals, SIGUSR1);", "", 'f7'),
    (G, "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_N) return UK_NEW_WINDOW;", "", 'f7'),
    (G, "    case GLFW_KEY_F10: return m == 0 ? UK_F10 : UK_NONE;", "", 'f7'),
    (G, "key >= GLFW_KEY_A && key <= GLFW_KEY_Z) ev_push((UiEvent){ UK_MENU, (u32)('a' + key - GLFW_KEY_A) });", "key >= GLFW_KEY_A && key <= GLFW_KEY_Z) ev_push((UiEvent){ UK_MENU, (u32)('A' + key - GLFW_KEY_A) });", 'f7'),
    (G, "        float body_h = (float)(h - MENU_H - HEADER_H - FOOTER_H);", "        float body_h = (float)(h - HEADER_H - FOOTER_H);", 'f7'),
    (G, "    int r = (h - MENU_H - HEADER_H - FOOTER_H - PAD) / SIDE_ROW_H;", "    int r = (h - HEADER_H - FOOTER_H - PAD) / SIDE_ROW_H;", 'f7'),
    (G, "        layout_menubar();\n", "", 'f7'),
    (G, "                if (open) layout_menu(m);", "", 'f7'),
    (G, "    ui_edit_open_all(&app.ui, str_from_c(made));\n    snprintf(app.edit_from, sizeof app.edit_from, \"%s\", made);\n    app.edit_new = UNDO_MKDIR;", "    ui_edit_open(&app.ui, str_from_c(made));\n    snprintf(app.edit_from, sizeof app.edit_from, \"%s\", made);\n    app.edit_new = UNDO_MKDIR;", 'f3'),
    (G, "    ui_edit_open_all(&app.ui, str_from_c(made));\n    snprintf(app.edit_from, sizeof app.edit_from, \"%s\", made);\n    app.edit_new = UNDO_MKFILE;", "    ui_edit_open(&app.ui, str_from_c(made));\n    snprintf(app.edit_from, sizeof app.edit_from, \"%s\", made);\n    app.edit_new = UNDO_MKFILE;", 'f7'),
    (G, "    return kind_is_dir(app.list.e[i].kind);\n}", "    return false;\n}", 'f7'),
    (G, "    if (e->size == 0 && (e->kind == K_FILE || e->kind == K_LINK_FILE) && apps_default(", "    if (e->kind == K_FILE && false && apps_default(", 'f7'),
    (G, "    if (e->size == 0 && (e->kind == K_FILE || e->kind == K_LINK_FILE) && apps_default(", "    if ((e->kind == K_FILE || e->kind == K_LINK_FILE) && apps_default(", 'f7'),
    (G, "        if (spawn_launch(desktop, path, &err)) set_status(false, \"opened %.*s\", STR_ARG(e->name));\n        else set_status(true, \"%s\", err.msg);\n        return;", "        if (spawn_launch(desktop, path, &err)) set_status(false, \"opened %.*s\", STR_ARG(e->name));\n        else set_status(true, \"%s\", err.msg);", 'f7'),
    (G, '    app.ui.prop_can = !app.ui.in_trash && !props.broken && props.st.st_uid == geteuid();', '    app.ui.prop_can = !props.broken && props.st.st_uid == geteuid();', 'f72'),
    (G, '    app.ui.prop_can = !app.ui.in_trash && !props.broken && props.st.st_uid == geteuid();', '    app.ui.prop_can = !app.ui.in_trash && !props.broken;', 'f72'),
    (G, '        props.broken = stat(path, &props.st) != 0;', '        props.broken = false;', 'f72'),
    (G, '        props.link[k > 0 ? k : 0] = 0;', '        props.link[0] = 0;', 'f72'),
    (G, '    if (n == 0 && !app.ui.in_trash) props_path[0] = arena_cstr(&propsa, str_from_c(app.cwd)), n = 1;', '', 'f72'),
    (G, '    if (app.ui.in_trash) snprintf(props.where, sizeof props.where, "Trash");', '', 'f72'),
    # Not mutants: `n >= bn` in props_drive (mountinfo lists a drive before those mounted inside it,
    # so the last above is the longest); prop_can in props_chmod (ui/ sends no UA_CHMOD without it);
    # walk_tick's sums (the final ones are the same; progress can't be timed in a script); `n > 0`
    # in walk_ask (an empty walk ends at once).
    (G, '    props.walking = props.local;\n    if (props.local) walk_ask(props_path, n);', '    props.walking = false;\n    if (false) {}', 'f72'),
    (G, '    if (props.walking && walk_take(&props.sum, &props.done)) dirty = true;', '', 'f72'),
    (G, '    mode_t old, perms = (props.st.st_mode & 0777) ^ perm_bit(k);', '    mode_t old, perms = (props.st.st_mode & 0777) | perm_bit(k);', 'f72'),
    (G, '    undo_gen++, undo_begin(&undo, UNDO_CHMOD), undo_add(&undo, NULL, props_path[0], was);', '', 'f72'),
    (G, '    snprintf(was, sizeof was, "%o %llu %llu", (unsigned)old,', '    snprintf(was, sizeof was, "%u %llu %llu", (unsigned)old,', 'f72'),
    (G, '    props_read();\n    set_status(false,', '    set_status(false,', 'f72'),
    (G, '            if (in_box(CLAY_IDI("prop-box", k), x, y)) h.prop_box = k;', '', 'f72'),
    (G, '    in->prop_box = h.prop_box;\n', '', 'f72'),
    (G, '    if (app.ui.props) return MODAL_PROPS;   // under any question (F7.2)', '', 'f72'),
    (G, '    case UA_CHMOD: props_chmod((int)a.index); break;', '', 'f72'),
    (G, '    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_I) return UK_PROPERTIES;', '', 'f72'),
    (G, '    if (m == GLFW_MOD_ALT && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) return UK_TERMINAL;', '', 'f72'),
    (G, '    if (statx(AT_FDCWD, path, props.broken ? AT_SYMLINK_NOFOLLOW : 0, STATX_BTIME, &sx) == 0 && (sx.stx_mask & STATX_BTIME))', '    if (false)', 'f72'),
    (G, '            if (app.ui.marked ? app.ui.mark[i] : i == app.ui.list.sel) props.idx = i;', '            {}', 'f72'),
    (WK, '        if (whole && gen == atomic_load(&walk.gen)) walk.sum = s, walk.sum_gen = gen, walk.done = walk.fresh = true;', '', 'f72'),
    # ---- F7.2 follow-ups: hard links, the MIME type (Michael, 2026-10-26)
    (WK, '                    .inodes = walk_inodes };', '                    .inodes = NULL };', 'f72'),
    (G, '        props.type_by_name = !app.thumb_local || !spawn_gio_type(props_path[0], props.type, sizeof props.type, 1000);', '        props.type_by_name = true;', 'f72'),
    (G, '        mime_comment(&scratch, str_from_c(props.type), props.type_name, sizeof props.type_name);', '', 'f72'),
    # ---- F7.3: Open With, Enter on several, find's toggles
    (OW, '    if (!app.thumb_local || !spawn_gio_type(ow.paths[0], ow.type, sizeof ow.type, 1000)) {', '    if (true) {', 'f73'),
    (OW, '    ow.shown = apps_rank(&owa, &ow.apps, str_from_c(ow.type));', '    ow.shown = ow.apps.n;', 'f73'),
    (OW, '    for (isize i = 0; i < ow.shown && i < UI_APPS_MAX; i++) ow.names[i] = ow.apps.a[i].name;', '    for (isize i = 0; i < ow.shown && i < UI_APPS_MAX; i++) ow.names[i] = ow.apps.a[i].id;', 'f73'),
    # Not a mutant: ow_launch's `i >= ow.shown` is inert, ui/ hands only the listed apps' places.
    (OW, '        isize k = ow.n - at < SPAWN_FILES_MAX ? ow.n - at : SPAWN_FILES_MAX;', '        isize k = 1;', 'f73'),
    (OW, '        if (kind_is_dir(e->kind) || e->kind == K_LINK_BROKEN || !entry_path(i, p, sizeof p)) {', '        if (!entry_path(i, p, sizeof p)) {', 'f73'),
    (OW, '                if (strcmp(desk[k], d) == 0) g = k;', '', 'f73'),
    (OW, '        if (group[i] < 0) bad |= !spawn_open(paths[i], &err), opened++;', '', 'f73'),
    (OW, '    if (n == 0) {   // only folders marked: the cursor\'s, as before\n        open_entry(app.ui.list.sel);\n        return;\n    }', '', 'f73'),
    (G, '    case UA_OPEN: a.all ? open_marked() : open_entry(a.index); break;', '    case UA_OPEN: open_entry(a.index); break;', 'f73'),
    (G, '    case UA_OPEN_WITH: ow_open(); break;', '', 'f73'),
    (G, '    case UA_LAUNCH: ow_launch(a.index, a.all); break;', '', 'f73'),
    (G, '    if (app.ui.ow) return MODAL_OW;         // likewise (F7.3)', '', 'f73'),
    (G, '            if (in_box(CLAY_IDI("ow-row", r), x, y)) h.ow_row = r;', '', 'f73'),
    (G, '        h.ow_field = modal_kind() == MODAL_OW && !h.toggle && in_box(CLAY_ID("ow-field"), x, y);', '', 'f73'),
    (G, '        if (in_box(CLAY_IDI("toggle", k), x, y)) h.toggle = k;', '', 'f73'),
    (G, '        if (ui_press(&app.ow_press, app.ui.ow_list.top + h.ow_row, now_ms()) == 2) in->ow_drow = h.ow_row;', '        if (false) in->ow_drow = h.ow_row;', 'f73'),
    (G, '    in->toggle = h.toggle;\n', '', 'f73'),
    (G, '        if (app.ui.ow) in.app_name = ow.names, in.app_n = ow.shown < UI_APPS_MAX ? ow.shown : UI_APPS_MAX;', '', 'f73'),
    (G, '    case GLFW_KEY_TAB: return m == 0 || m == GLFW_MOD_SHIFT ? UK_TAB : UK_NONE;', '    case GLFW_KEY_TAB: return m == 0 ? UK_TAB : UK_NONE;', 'f73'),
    (G, '    ui_resize(&app.ui.ow_list, ow_rows(GetScreenHeight()));   // Open With\'s list (F7.3)', '', 'f73'),
    # ---- after Michael's try: listed at once, the apps' icons
    (OW, '    ui_ow_filter(&app.ui, &in);\n}', '}', 'f73'),
    (OW, '            if (access(f, R_OK) == 0) file = f;', '', 'f73'),
    (OW, '            if ((str_ends_with(icon, S(".png")) || str_ends_with(icon, S(".svg"))) && access(f, R_OK) == 0) file = f;', '', 'f73'),
    (OW, '    return s->tex.id ? s : icon_for(generic, &generic, 1);', '    return s;', 'f73'),
    (OW, '        if (file) icon_tex_load(s, file, ICON_PX);', '', 'f73'),
    (OW, '            file = icon_find(&scratch, &ow.idb, &icon, 1, ICON_PX, app.icon_scale);', '            file = NULL;', 'f73'),
    (OW, '        if (ow.icon[i].tex.id) UnloadTexture(ow.icon[i].tex);\n        ow.icon[i] = (IconSlot){ 0 }, ow.icon_tried[i] = false;', '        ow.icon_tried[i] = false;', 'f73'),
    (G, '        if (!app.ui.ow && ow.icons_on) ow_drop();   // the panel closed: its icons go (F7.3)', '', 'f73'),
    (G, '    if (str_ends_with(str_from_c(file), S(".svg"))) {', '    if (false) {', 'f73'),
    (G, '    if (stat(file, &st) != 0 || st.st_size > SVG_BYTES_MAX) return img;', '    if (stat(file, &st) != 0) return img;', 'f73'),
    # ---- the find bar (Michael, 2026-10-26)
    (G, '        if (app.ui.finding) layout_find_bar();', '', 'f2'),
    (G, '        case GLFW_KEY_DOWN: return UK_SEL_DOWN;', '', 'f2'),
    (G, '    if (stat(dir, &sst) == 0 && !S_ISDIR(sst.st_mode)) {', '    if (false) {', 'f73'),
    (G, '    if (!same) app.ui.find_from = app.ui.list.sel;', '', 'f2'),
    (G, ' - (app.ui.finding ? FIND_BAR_H : 0);', ';', 'f2'),
    (G, '        h.find_x = in_box(CLAY_ID("find-x"), x, y);', '', 'f2'),
    (G, '        h.find_field = !h.toggle && !h.find_x && in_box(CLAY_ID("find-field"), x, y);', '', 'f2'),
    (G, '    in->find_x = h.find_x, in->find_field = h.find_field, in->find_step = h.find_step;', '', 'f2'),
    # ---- its count and chevrons (Michael, 2026-10-26)
    (G, '        h.find_step = in_box(CLAY_ID("find-prev"), x, y) ? -1 : in_box(CLAY_ID("find-next"), x, y) ? 1 : 0;', '', 'f2'),
    (G, '        h.find_step = in_box(CLAY_ID("find-prev"), x, y) ? -1 : in_box(CLAY_ID("find-next"), x, y) ? 1 : 0;', '        h.find_step = in_box(CLAY_ID("find-prev"), x, y) ? -1 : 0;', 'f2'),
    (G, '    if (at > 0) snprintf(buf, cap, "%td of %td", at, n);', '    if (false) snprintf(buf, cap, "%td of %td", at, n);', 'f2'),
    (G, '    if (!c.valid || c.lists != app.lists || c.sel != app.ui.list.sel ||', '    if (!c.valid || c.lists != app.lists ||', 'f2'),
    (G, '    if (!c.valid || c.lists != app.lists || c.sel != app.ui.list.sel || c.len != m->len || memcmp(c.text, m->text, (size_t)m->len) ||', '    if (!c.valid || c.lists != app.lists || c.sel != app.ui.list.sel || c.len != m->len ||', 'f2'),
    (G, '    if (h.find_step) return TIP_FIND + (h.find_step > 0);', '', 'f2'),
    (G, 'c.finding != app.ui.finding || c.find_len != app.ui.find_len || ', '', 'f2'),
    (G, '"Previous match (Up)" : "Next match (Down)"', '"Next match (Down)" : "Next match (Down)"', 'f2'),
    (G, '(app.ui.ow || (app.ui.finding && modal_kind() == MODAL_NONE && !app.ui.menu_on))', '(app.ui.ow || app.ui.finding)', 'f72'),
    (G, '    ui_find_close(&app.ui);   // (a tab\'s own find isn\'t kept)', '', 'f2'),
    # ---- F7.3.1: Set as default
    (OW, '    if (strcmp(ow.type, "application/x-zerosize") == 0) snprintf(', '    if (false) snprintf(', 'f73'),
    (OW, '    else if (apps_shadowed(&owa, str_from_c(ow.type), shadow, sizeof shadow))', '    else if (false)', 'f73'),
    (OW, '    app.ui.ow_def_can = !ow.def_why[0];', '    app.ui.ow_def_can = true;', 'f73'),
    # Not a mutant: `&& !ow.def_why[0]` in ow_launch is inert, ui/ hands def only when gui/ allowed it.
    (OW, '    if (set && !op_set_default(&scratch, str_from_c(ow.type), id, &err)) {', '    if (false) {', 'f73'),
    (OW, '    if (set) set_status(false, "opened %s with %.*s, the default for %s now"', '    if (false) set_status(false, "opened %s with %.*s, the default for %s now"', 'f73'),
    (G, '    case UA_LAUNCH: ow_launch(a.index, a.all); break;', '    case UA_LAUNCH: ow_launch(a.index, false); break;', 'f73'),
    (G, '    for (int k = 1; k <= 4 && !h.toggle && (app.ui.ow || (app.ui.finding && modal_kind() == MODAL_NONE && !app.ui.menu_on)); k++)', '    for (int k = 1; k <= 3 && !h.toggle && (app.ui.ow || (app.ui.finding && modal_kind() == MODAL_NONE && !app.ui.menu_on)); k++)', 'f73'),
    # Left out: nsvgRasterize gone leaves a blank texture, which no state word sees (the pixels);
    # f73's ow-panel.png shows the SVG drawn.
]
import concurrent.futures, queue, threading

args = sys.argv[1:]
full = '--full' in args
jobs = 6   # the time levels off there: 46 mutants on a quiet machine took 414-451 s at 6, 411-419 at 8, 442 at 10, 477 at 12, CPU 43-62% (2026-10-26)
if '--jobs' in args:
    jobs = int(args[args.index('--jobs') + 1])
    del args[args.index('--jobs'):args.index('--jobs') + 2]
args = [a for a in args if a != '--full']
if len(args) not in (1, 2) or any(a.startswith('-') for a in args):
    sys.exit(__doc__.strip())   # an unknown flag isn't taken for the scratch folder
scratch = args[0]
only = [int(x) for x in args[1].split(',')] if len(args) > 1 else None
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# OFX_GUI_TAGS: another tags file, for a run beside one that will write this one.
tags_path = os.environ.get('OFX_GUI_TAGS') or os.path.join(root, 'tools', 'mutation', 'gui-tags.txt')

# run.sh's groups, in its order: a mutant runs them one at a time and stops at the first that
# kills it (Michael, 2026-10-25).
GROUPS = ['browse', 'big', 'gone', 'watchgap', 'f2', 'f3', 'f4', 'f5', 'f52', 'f53', 'f54', 'f6', 'f62', 'f63', 'f64', 'f7',
          'f72', 'f73', 'hang', 'guard3', 'guard']
run_text = open(os.path.join(root, 'tests/gui/run.sh')).read()
assert re.findall(r'want ([a-z0-9]+)', run_text) == GROUPS, 'GROUPS in gui.py no longer match tests/gui/run.sh'

# A mutant is (file, old, new) or (file, old, new, hint): the hint names the group(s) likeliest to
# kill it, tried first when it has no tags. Its key is the first three only, so a hint added later
# keeps its tags.
for m in MUTANTS:
    assert len(m) in (3, 4) and (len(m) == 3 or all(g in GROUPS for g in m[3].split())), m

# Each worker its own copy of the repo and its own build; the scripts run from the copy, so the
# repo can be edited meanwhile.
def make_work(w):
    work = os.path.join(scratch, f'gui-mut-src-{w}')
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(root, work, ignore=shutil.ignore_patterns('.git', 'build'))
    os.makedirs(os.path.join(work, 'build'))
    os.symlink(os.path.join(root, 'build', 'raylib'), os.path.join(work, 'build', 'raylib'))
    return work
works = [make_work(w) for w in range(jobs)]
for f, old, new, *_ in MUTANTS:
    n = open(os.path.join(works[0], f)).read().count(old)
    assert n == 1, (f, old, n)

# A tree removed whole: a mutant that fails f6 half way leaves its read-only folders (`locked`),
# which rmtree alone can't empty (two such trees stayed behind on 2026-10-24).
def rmtree(path):
    for d, dirs, _ in os.walk(path):
        for x in dirs:
            try:
                os.chmod(os.path.join(d, x), 0o700)
            except OSError:
                pass
    shutil.rmtree(path, ignore_errors=True)

# A mutant's tag key: its file and texts, so a mutant edited is a new one, untagged.
def key(m):
    return hashlib.sha1('\0'.join(m[:3]).encode()).hexdigest()[:12]

# The group of a script that failed: run.sh prints "== <script>: FAILED" or "<group>: FAILED".
GROUP_OF = {'f6b': 'f6', 'f62b': 'f62', 'f62c': 'f62', 'f63b': 'f63', 'f63c': 'f63', 'f64b': 'f64', 'f64c': 'f64', 'f64d': 'f64', 'f7b': 'f7', 'f73b': 'f73'}

# The tags as groups run.sh knows: a script's name (f7b, written before GROUP_OF had it) as its
# group; anything else dropped, so a mutant never "survives" a group that runs nothing.
tags = {}
if os.path.exists(tags_path):
    for line in open(tags_path):
        w = line.split('#')[0].split()
        g = list(dict.fromkeys(GROUP_OF.get(x, x) for x in w[1:] if GROUP_OF.get(x, x) in GROUPS))
        if len(w) >= 2 and g:
            tags[w[0]] = g
def failed_groups(out):
    g = []
    for l in out.splitlines():
        m = re.match(r'(?:== )?([a-z0-9]+): FAILED', l)
        if m and GROUP_OF.get(m[1], m[1]) not in g:
            g.append(GROUP_OF.get(m[1], m[1]))
    return g

def run_groups(work, base, groups):
    return subprocess.run([os.path.join(work, 'tests/gui/run.sh'), base, os.path.join(work, 'build/ofx-mut')] + groups,
                          capture_output=True, text=True)

# The unmutated build first, on the whole suite, in every worker at once: under the load the
# mutants will run with. If any fails, every mutant would count as killed (as they did while f6
# couldn't start, from fe01f7c to e8c2c57), and a script that fails only under load would kill
# mutants it doesn't see.
def base_run(w):
    b = subprocess.run(['make', '-s', 'build/ofx-mut'], cwd=works[w], capture_output=True, text=True)
    r = run_groups(works[w], os.path.join(scratch, f'gm-base{w}'), [])
    rmtree(os.path.join(scratch, f'gm-base{w}'))
    return b, r
with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
    bases = list(ex.map(base_run, range(jobs)))
bad = [(w, b, r) for w, (b, r) in enumerate(bases) if b.returncode != 0 or r.returncode != 0]
if bad:
    print(f'the unmutated build fails tests/gui/run.sh in {len(bad)} of {jobs} workers at once: fix that first (or fewer --jobs)')
    for w, b, r in bad:
        print(f'worker {w}:', b.stderr[-300:], '\n'.join(l for l in r.stdout.splitlines() if 'FAILED' in l or (l.startswith('script:') and not l.startswith('script: 0 of'))))
    sys.exit(2)
print(f'base: tests/gui/run.sh passes unmutated, {jobs} at once', flush=True)
if 'GUI_BASE_ONLY' in os.environ:
    sys.exit(0)

MIN_FREE = 2 << 30   # bytes left in the scratch dir's file system before a mutant starts
free_works = queue.Queue()
for w in works:
    free_works.put(w)
lock = threading.Lock()
survivors, unbuilt, retagged, stopped = [], [], 0, False

# One mutant in a free worker: its groups one run.sh at a time, until one kills it. Tagged (and
# not --full): its tags only. Else its hint and its old tags first, then the rest of the groups;
# the group that kills it becomes its tag.
def one(i):
    global retagged, stopped
    m = MUTANTS[i]
    f, old, new = m[:3]
    k = key(m)
    with lock:
        if stopped or shutil.disk_usage(scratch).free < MIN_FREE:
            if not stopped:
                print(f'stopped before #{i}: less than 2 GiB free in {scratch}', flush=True)
            stopped = True
            return
    work = free_works.get()
    try:
        path = os.path.join(work, f)
        text = open(path).read()
        open(path, 'w').write(text.replace(old, new, 1))
        b = subprocess.run(['make', '-s', 'build/ofx-mut'], cwd=work, capture_output=True, text=True)
        if b.returncode != 0:
            open(path, 'w').write(text)
            print(f'#{i} build failed: {b.stderr[-300:]}', flush=True)
            with lock:
                unbuilt.append(i)
            return
        tagged = tags.get(k, []) if not full else []
        hint = m[3].split() if len(m) == 4 else []
        order = tagged or list(dict.fromkeys(hint + tags.get(k, []) + GROUPS))
        killer, out, ran = None, '', 0
        for g in order:
            r = run_groups(work, os.path.join(scratch, f'gm{i}'), [g])
            ran += 1
            if r.returncode != 0:
                killer, out = (failed_groups(r.stdout) or [g])[0], r.stdout
                break
        open(path, 'w').write(text)
        rmtree(os.path.join(scratch, f'gm{i}'))
        fails = [l for l in out.splitlines() if l.startswith('script:') and not l.startswith('script: 0 of')]
        with lock:
            if killer and not tagged:
                tags[k] = [killer]
                retagged += 1
            if not killer:
                survivors.append(i)
            how = f'killed by {killer}' if killer else 'SURVIVED'
            print(f'#{i} {how} [{ran} of {len(order)} groups] {f}: {old.strip()[:60]!r}  {fails[:1]}', flush=True)
    finally:
        free_works.put(work)

todo = [i for i in range(len(MUTANTS)) if only is None or i in only]
with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
    list(ex.map(one, todo))
if retagged:
    live = {key(m) for m in MUTANTS}
    with open(tags_path, 'w') as out:
        out.write('# tools/mutation/gui.py: each GUI mutant (its key) and the run.sh group that killed it\n')
        for k in sorted(tags):
            if k in live and tags[k] != ['all']:
                out.write(f'{k} {" ".join(tags[k])}\n')
    print(f'{retagged} mutants tagged in {tags_path}')
for w in works:
    shutil.rmtree(w, ignore_errors=True)
# What didn't run is said, so a summary never reads 0 for mutants that weren't tested (reviewer-2,
# F7.1.1 review).
untested = sorted(unbuilt) + (['the rest: out of disk'] if stopped else [])
print(f'{len(todo)} GUI mutants, {len(survivors)} survived: {sorted(survivors)}' + (f'; NOT TESTED: {untested}' if untested else ''))
if untested:
    sys.exit(1)
