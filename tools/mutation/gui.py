#!/usr/bin/env python3
"""GUI mutants: each applied to a scratch copy of the repo, built, and run through
tests/gui/run.sh, each script in its own headless weston (CLAUDE.md, "Testing the GUI").

    tools/mutation/gui.py <scratch-dir> [index,...] [--jobs N]

The whole GUI suite takes about 90 s, so each mutant runs the script groups that killed it before
first (its tags, in gui-tags.txt): a grid mutant runs f62, not the copy jobs' f53. Groups run one
at a time and a mutant stops at the first that kills it (Michael, 2026-10-25), and within a group
at its first failure (OFX_GUI_FAIL_FAST: the script ends there, run.sh skips the group's other
scripts; 2026-10-26, a run 1591 s to 881 s: every wait-for after a failure ran out its time). A mutant its tags
don't kill (new, its text changed, or the tests moved) tries its hint (a fourth field, `'f7'`),
then the rest; the group that kills it becomes its tag, and the summary names it as retagged.
Until 2026-10-29 that fallback was --full's, run only before a major milestone's last install, and a
tagged run called a mutant its tags missed a survivor; since the first-kill stop it costs nothing
when the tags kill (Michael, 2026-10-29: 484 mutants in 1358 s either way). --jobs N (6 by default) runs N mutants at once, each in its
own copy of the repo (6 is as fast as 8, 2026-10-25). The unmutated build runs the whole suite
once, its groups the first tasks in the same pool, so under the mutants' load while they start: a
script that fails there stops the run and throws its results away, instead of killing mutants it
doesn't see (until 2026-10-26 it ran in all N workers before any mutant: 30% of a run). Mutants that didn't build, or
weren't run for want of disk, are named in the summary and fail the run.
The unmutated build always runs the whole suite (GUI_BASE_ONLY=1: only that).

Everything runs from the scratch copy, scripts included, so the repo can be edited meanwhile.
Each run's trees (about 50 MB: the copy tests' big files) are removed once it is judged, and the
run stops while there is still room: the scratch dir is usually /tmp, a tmpfs in RAM, and keeping
them filled it on 2026-10-24.
"""
import hashlib, os, re, shutil, subprocess, sys

G, W, K, J, TB, WK, OW = 'gui/main.c', 'gui/watch.c', 'gui/counts.c', 'gui/jobs.c', 'gui/thumbs.c', 'gui/walk.c', 'gui/openwith.c'
MUTANTS = [
    ('gui/folder.c', "    go_to(up, str_from_c(child_buf), 0);", "    go_to(up, STR_EMPTY, 0);"),
    ('gui/folder.c', "    if (go_to(app.cwd, str_from_c(keep), app.ui.list.sel)) return;", "    if (go_to(app.cwd, STR_EMPTY, app.ui.list.sel)) return;"),
    ('gui/folder.c', "        memmove(up, d.p, (size_t)d.len);\n        up[d.len] = 0;", "        snprintf(up, sizeof up, \"%.*s\", STR_ARG(d));"),
    (W, "static const u32 WATCH_DIR_MASK = IN_CREATE | ", "static const u32 WATCH_DIR_MASK = "),
    (W, "    if (rewatch) atomic_store(&watch.theme_wd, inotify_add_watch(watch.fd, watch.theme, WATCH_THEME_MASK | IN_MASK_ADD));", ""),
    (W, "        if (theme) atomic_store(&watch.theme_ready, true);", ""),
    # F2
    ('gui/folder.c', "    if (!same && app.have_list && !stepping) hist_leave(&hist, app.cwd, sel_name());\n", ""),
    ('gui/folder.c', "    isize sel = listing_find(&app.list, str_from_c(keep));\n    ui_set(&app.ui.list, app.list.n, sel >= 0 ? sel : 0);", "    ui_set(&app.ui.list, app.list.n, 0);"),
    ('gui/actions.c', "        go_to(app.cwd, str_from_c(keep), 0);\n        break;", "        go_to(app.cwd, STR_EMPTY, 0);\n        break;"),
    ('gui/input.c', "        if (in_box(CLAY_IDI(\"side-row\", r), x, y)) h.side = r;", "        if (0) h.side = r;"),
    ('gui/input.c', '        if (in_box(Clay_GetElementId(cz(COL_IDS[k])), x, y)) h.header = k;', ''),
    ('gui/input.c', "(button == GLFW_MOUSE_BUTTON_5 || button == GLFW_MOUSE_BUTTON_6)) mouse_forwards++;", "(button == GLFW_MOUSE_BUTTON_6)) mouse_forwards++;"),
    ('gui/actions.c', "        if (p) go_to(p, STR_EMPTY, 0);", "        if (false) {}"),
    ('gui/folder.c', "        hist_unstep(&hist, dir);\n", ""),
    # F3
    ('gui/folder.c', "        if (k >= 0 && !mark[k]) mark[k] = 1, marked++;", ""),
    ('gui/actions.c', "        if (app.ui.marked ? !app.ui.mark[i] : i != app.ui.list.sel) continue;\n        char p[PATH_MAX];", "        if (i != app.ui.list.sel) continue;\n        char p[PATH_MAX];"),
    (J, "            if (it->out != OUT_NEW) continue;", ""),
    (J, "    if (r->own_cut && !atomic_load(&j->cancel) && own_gen == r->own_gen) own.n = 0, clip_clear_own();", ""),
    ('gui/actions.c', "if (!name_free(dest, base, S_ISDIR(st.st_mode), true, true, name, sizeof name)) continue;", "if (!name_free(dest, base, S_ISDIR(st.st_mode), false, true, name, sizeof name)) continue;"),
    ('gui/actions.c', "        if (S_ISDIR(st.st_mode) && into_itself(src, dest)) {", "        if (0) {"),
    ('gui/actions.c', "            if (c->cut) {\n                here++;", "            if (0) {\n                here++;"),
    ('gui/actions.c', '        undo_start(app.edit_new ? app.edit_new : UNDO_RENAME);', '        undo_start(UNDO_RENAME);'),
    ('gui/actions.c', "    if (back[0]) go_to(app.cwd, str_from_c(back), app.ui.list.sel);\n    else reload();", "    reload();"),
    ('gui/input.c', "        return m == GLFW_MOD_SHIFT ? UK_SHIFT_DELETE : m == 0 ? UK_DELETE : UK_NONE;", "        return m == 0 ? UK_DELETE : UK_SHIFT_DELETE;"),
    ('gui/input.c', "    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_N) return UK_NEWFOLDER;", ""),
    (J, "        if (jobs.asking >= 0) app.ui.asking = true, app.ui.ask_all = false;", ""),
    (G, "    ops_guard(getenv(\"OFX_GUI_ROOT\"));", ""),
    (G, '        if (!was_editing && app.ui.focus == UI_FOCUS_EDIT && !app.ui.edit_path) edit_opened();', ''),
    # F4
    ('gui/actions.c', "    if (i + 1 < c->n) snprintf(below, sizeof below, \"%.*s\", STR_ARG(ui_crumb_label(c, i + 1)));", ""),
    ('gui/folder.c', "    // The counts are by row too: asked again in the new order.\n    for (isize i = 0; i < app.list.n; i++) app.count[i] = COUNT_UNKNOWN;", "    // The counts are by row too: asked again in the new order."),
    (G, "        if (app.have_list && count_take(app.count, app.list.n)) dirty = true;", ""),
    ('gui/actions.c', '    if (!S_ISDIR(st.st_mode)) {\n        select = path_base', '    if (0) {\n        select = path_base'),
    ('gui/input.c', "        if (in_box(CLAY_IDI(\"crumb\", i), x, y)) h.bar = UB_CRUMB, h.crumb = i;", ""),
    # Not run: the cursor's row drawing an edit box while the location line is open
    # (`&& !app.ui.edit_path` in layout_row) shows only on screen; location.png shows the names.
    ('gui/folder.c', "        ui_crumbs_set(&app.crumbs, app.cwd, getenv(\"HOME\"));", ""),
    ('gui/input.c', "    if (m == GLFW_MOD_ALT && key == GLFW_KEY_UP) return UK_PARENT;", ""),
    ('gui/actions.c', "    ui_edit_close(&app.ui);\n    if (name[0] == '.'", "    if (name[0] == '.'"),
    # Not run: counts.c drops an old listing's answer twice over (in count_loop and in count_take),
    # so taking out either check alone changes nothing a test can see.
    ('gui/files.c', "    if (app.count[i] >= 0) snprintf(size, cap, \"%td item%s\", app.count[i], app.count[i] == 1 ? \"\" : \"s\");", "    if (app.count[i] >= 0) snprintf(size, cap, \"%td items\", app.count[i]);"),
    # F5
    ('gui/actions.c', "    ui_mark_clear(&app.ui);   // the same folder in two tabs: this tab's marks, not the other's", ""),
    ('gui/actions.c', "        if (k >= 0) ui_mark(&app.ui, k, true);\n        at += nm.len + 1;", "        at += nm.len + 1;"),
    ('gui/actions.c', "    app.ui.list.top = t->top;\n", ""),
    ('gui/actions.c', "    t->hist = hist, t->crumbs = app.crumbs;", "    t->crumbs = app.crumbs;"),
    ('gui/actions.c', "        go_up_from(t->dir);\n        if (there)", "        if (there)"),
    ('gui/actions.c', "    if (tabbar.n == 1) {\n        app.quit = true;\n        return;\n    }", ""),
    ('gui/actions.c', "    if (shown) tab_load();", ""),
    ('gui/actions.c', "    if (strcmp(path, TRASH_URI) != 0 && !is_search(path) && (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))) {", "    if (strcmp(path, TRASH_URI) != 0 && !is_search(path) && stat(path, &st) != 0) {"),
    ('gui/input.c', "    if (h.tab >= 0) in->tab_close = h.tab;\n    else if (h.row >= 0)", "    if (h.row >= 0)"),
    ('gui/input.c', "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_W) return UK_TAB_CLOSE;", ""),
    ('gui/input.c', "    if (action == GLFW_PRESS && m == GLFW_MOD_ALT && key >= GLFW_KEY_1 && key <= GLFW_KEY_9)", "    if (action == GLFW_PRESS && key >= GLFW_KEY_1 && key <= GLFW_KEY_9)"),
    (G, "    return bar_h() + HEADER_H + (tabbar.n >= 2 ? TAB_H : 0);", "    return bar_h() + HEADER_H;"),
    ('gui/actions.c', "    case UA_TAB_MOVE: ui_tabs_move(&tabbar, (int)a.index); break;", "    case UA_TAB_MOVE: break;"),
    # F5 review: the delete question's targets taken when it opens
    ('gui/actions.c', "    isize k = 0;\n    for (isize i = 0; i < n; i++) {\n        r->item[k++] = (JobItem){ .src = arena_cstr(&r->arena, str_from_c(doomed.path[i])) };", "    isize k = 0;\n    char *late[CLIP_PATHS_MAX];\n    n = targets(&r->arena, late, CLIP_PATHS_MAX);\n    for (isize i = 0; i < n; i++) {\n        r->item[k++] = (JobItem){ .src = late[i] };"),
    (J, "    if (undo.n == 0) undo_clear(&undo);\n    }", "    if (undo.n == 0) undo_clear(&undo);\n    }\n    if (j->kind == JOB_DELETE) ui_mark_clear(&app.ui);"),
    (G, "        if (!was_confirming && (app.ui.confirming || a.kind == UA_DELETE)) delete_ask();", "        if (!was_confirming && app.ui.confirming) delete_ask();"),
    ('gui/actions.c', '    doomed.n = 0;\n    if (too_many_marked()) {', '    if (too_many_marked()) {'),
    # F5.1: the modal panels
    ('gui/input.c', '        for (int b = 1; b < COUNT(MODAL_IDS) && !h.modal; b++)\n            if (in_box(Clay_GetElementId(cz(MODAL_IDS[b])), x, y)) h.modal = b;\n', ''),
    # Not run: hit_at's early return while a panel is up; ui_handle drops every other click during a
    # prompt anyway, so leaving it out changes nothing a test can see.
    (J, "        if (jobs.asking >= 0) app.ui.asking = true, app.ui.ask_all = false;", "        if (jobs.asking >= 0) app.ui.asking = true;"),
    # F5.2: the edit lines' text clipboard, the path buttons by keyboard. clipboard.c's
    # data-control text: with the clipboard's own, at the end (clip, in the test sway).
    ('gui/input.c', "        case GLFW_KEY_LEFT: return UK_SEL_LEFT;\n", ""),
    ('gui/input.c', "        case GLFW_KEY_KP_ENTER: return UK_SHIFT_ENTER;\n", ""),
    ('gui/input.c', "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_K) return UK_CRUMBS;\n", ""),
    ('gui/topbar.c', "    if (app.ui.focus == UI_FOCUS_CRUMBS) ui_crumbs_show(c, wd, room, ARROW_W, app.ui.crumb_at);\n", ""),
    (G, "        in.crumb_n = app.crumbs.n, in.crumb_last = app.crumb_hi - 1;", "        in.crumb_n = app.crumbs.n, in.crumb_last = app.crumb_hi;"),
    (G, '        if (pasting && ui_line_active(&app.ui, NULL)) in.paste = text_paste();   // (whichever text field has the keys)\n', ''),
    (G, "        if (app.ui.copied) text_copy((Str){ app.ui.copy, app.ui.copy_len }), app.ui.copied = false;", "        app.ui.copied = false;"),
    ('gui/actions.c', "    if (!clip_set_text(t)) SetClipboardText(app.text_copied);\n    own.n = 0;", "    if (!clip_set_text(t)) SetClipboardText(app.text_copied);"),
    ('gui/actions.c', "    if (own.n > 0) return clip_payload(&pastea, &own, CLIP_FMT_TEXT);\n", ""),
    ('gui/actions.c', "    if (!go_to(dir, str_from_c(name), 0)) return;", "    if (!go_to(dir, STR_EMPTY, 0)) return;"),
    ('gui/actions.c', "    if (name[0] == '.' && !app.show_hidden) set_status(", "    if (0) set_status("),
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
    ('gui/input.c', "        if (in_box(CLAY_IDI(\"job-x\", i), x, y)) h.job_x = i;", ""),
    ('gui/input.c', "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_J) return UK_JOBS;\n", ""),
    ('gui/panels.c', "    case UI_ON_CANCEL: return MODAL_CANCEL;\n", ""),
    (G, "        for (int k = 0; k < jobs.nrun; k++) in.job_id[k] = job_recs[jobs.run[k]].id;", ""),
    # F5.3 review fixes. Not run: the refusals in rename, new folder and undo (the same
    # jobs_refuse as trash's), and the timed join at quit (it needs a worker stuck in a hung mount).
    (J, "    if (undo_gen != r->undo_gen) {", "    if (0) {"),
    ('gui/actions.c', "    for (isize i = 0; i < n; i++)\n        if (jobs_refuse(paths[i])) return;\n", ""),
    # F5.4: a theme switch as omarchy-theme-set makes it
    (W, "        if (e->wd == atomic_load(&watch.current_wd)) *theme = true, rewatch = true;", "        if (e->wd == atomic_load(&watch.current_wd)) rewatch = true;"),
    (W, "        if (e->wd == atomic_load(&watch.theme_wd) && !(e->mask & IN_IGNORED)) *theme = true;", ""),
    (G, "    app.theme = theme_load(&scratch);\n    icons_reset();", "    app.theme = theme_load(&scratch);"),
    # F6
    (W, "        if (watch.mount_wd >= 0 && e->wd == watch.mount_wd) *mounts = true;\n", ""),
    (G, "        if (atomic_exchange(&watch.mounts_ready, false)) places_reload(), trash_state(), dirty = true;\n", ""),
    ('gui/actions.c', '        if (app.ui.view == UI_VIEW_TRASH && str_eq(path_base(files), S("files")))\n            r->item[k++]', '        if (0)\n            r->item[k++]'),
    ('gui/actions.c', "        job_submit(r, JOB_DELETE, k);\n        r->trash = JOB_TRASH_EMPTY;", "        r->trash = JOB_TRASH_EMPTY;"),
    ('gui/actions.c', "    case UA_RESTORE: trash_restore_targets(); break;\n", ""),
    (G, "        if (ui_input_acts(&in) && ui_status_clears(app.status_bad, app.status_at, now_ms())) app.status[0] = 0;", ""),
    ('gui/actions.c', "        if (state_path(path, sizeof path) && !state_put(&scratch, path, \"sidebar\", SIDE_MODE_NAMES[app.ui.side_mode], &err)) set_status(true, \"%s\", err.msg);\n", ""),
    (G, "    if (state_path(state_file, sizeof state_file)) {\n        State st", "    if (0) {\n        State st"),
    ('gui/actions.c', "    case UA_SIDE_MODE: {\n        side_refresh();", "    case UA_SIDE_MODE: {"),
    ('gui/input.c', "        if (in_box(CLAY_IDI(\"mode\", k), x, y)) h.mode = k;", "        if (0) h.mode = k;"),
    (J, "    if (r->trash == JOB_TRASH_ITEMS) done /= 2, shown_n /= 2;", ""),
    ('gui/folder.c', "    if (kind_is_dir(e->kind) && e->trash) {", "    if (0) {"),
    ('gui/folder.c', "    if (app.ui.side_mode == SIDE_PLACES && app.ui.side.sel < app.places.n) keep = app.places.p[app.ui.side.sel];", "    if (app.places.n) keep = app.places.p[app.ui.side.sel];"),
    # F6 review
    (G, "        if (space_take(&app.places, app.cwd, &app.cwd_avail)) dirty = true;", ""),
    ('gui/folder.c', "    space_ask(&app.places, app.cwd);\n    return true;", "    return true;"),
    ('gui/actions.c', ".src = arena_cstr(&r->arena, str_from_c(doomed_all[at + i].path)), .after_ok = doomed_all[at + i].after_ok };", ".src = arena_cstr(&r->arena, str_from_c(doomed_all[at + i].path)) };"),
    # F6.1
    ('gui/files.c', "            if (drive) ui_size(avail, sizeof avail, hover ? p->size - p->avail : p->avail);", "            if (drive) ui_size(avail, sizeof avail, p->avail);"),
    ('gui/files.c', "                if (hover && place_ejectable(p)) {", "                if (0) {"),
    ('gui/input.c', "    if (h.side >= 0 && app.ui.side_mode == SIDE_PLACES && in_box(CLAY_IDI(\"side-eject\", h.side), x, y)) h.eject = h.side;", ""),
    (G, "        if (eject_take()) dirty = true;", ""),
    ('gui/eject.c', "    if (op != DRIVE_MOUNT && jobs_refuse(p->path)) return false;   // a job of ours writes there: it says so", ""),
    (G, "    if (chdir(\"/\") != 0) fprintf(stderr, \"ofx: chdir /: %s\\n\", strerror(errno));", ""),
    # F6.2: the icon view
    ('gui/files.c', "    int c = (w - side_w() - 2 * PAD) / cell_w();", "    int c = (w - 2 * PAD) / cell_w();"),
    ('gui/files.c', "    int r = app.ui.grid ? (room - PAD) / cell_h() : (room - COLS_H) / ROW_H;", "    int r = (room - COLS_H) / ROW_H;"),
    ('gui/input.c', "    for (int r = 0; r < list_cells() && h.row < 0; r++)", "    for (int r = 0; r < app.ui.list.rows && h.row < 0; r++)"),
    ('gui/folder.c', "    if (!same && app.nslots >= app.icons_max) icons_reset(), app.icon_full_resets++;\n", ""),
    (G, "        if (rows != app.ui.list.rows) ui_resize(&app.ui.list, rows), dirty = true;\n", ""),
    ('gui/files.c', "    if (!app.ui.grid) return 1;\n    int c = (w", "    int c = (w"),
    ('gui/input.c', "    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_1 || key == GLFW_KEY_KP_1)) return UK_VIEW_LIST;", ""),
    ('gui/input.c', "    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_2 || key == GLFW_KEY_KP_2)) return UK_VIEW_ICONS;", ""),
    ('gui/icons.c', "    return app.ui.grid ? grid_px() : ICON_PX;", "    return ICON_PX;"),
    ('gui/files.c', "            int n = name_lines(\"cell-line\", k, e->name, 2, name_w, fg, &cut);", "            int n = name_lines(\"cell-line\", k, e->name, CELL_LINES_FULL, name_w, fg, &cut);"),
    ('gui/files.c', "        isize first = app.ui.list.top + (isize)r * cols;", "        isize first = app.ui.list.top + r;"),
    (G, "        if (cols != (int)ui_cols(&app.ui.list)) ui_set_cols(&app.ui.list, cols), dirty = true;", ""),
    ('gui/input.c', '        if (BAR_IDS[b] && in_box(bar_id((UiBar)b), x, y)) h.bar = (UiBar)b;', '        if (BAR_IDS[b] && b != UB_VIEW_ICONS && in_box(bar_id((UiBar)b), x, y)) h.bar = (UiBar)b;'),
    ('gui/frame.c', "            if (app.have_list && app.ui.grid) layout_grid();\n", ""),
    ('gui/icons.c', "    if (strmap_get(&app.slot_of, slot_key, &k))", "    if (strmap_get(&app.slot_of, key, &k))"),
    # F6.2 keys (Michael, 2026-10-24): Ctrl and an arrow, Ctrl+S
    ('gui/input.c', '    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_LEFT) return UK_WORD_LEFT;', '    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_LEFT) return UK_NONE;'),
    ('gui/input.c', "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_UP) return UK_CRUMBS;\n", ""),
    ('gui/input.c', "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_DOWN) return UK_JOBS;\n", ""),
    ('gui/input.c', '    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_RIGHT) return UK_WORD_RIGHT;', '    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_RIGHT) return UK_NONE;'),
    ('gui/input.c', "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_S) return UK_F9;\n", ""),
    # tooltips (Michael, 2026-10-24)
    ('gui/input.c', "        timer_at(wake_due());\n", ""),
    (G, "        if (ui_tip_tick(&app.tip, now_ms())) dirty = true;\n", ""),
    (G, "        if (ui_input_acts(&in) && ui_tip_act(&app.tip)) seen = true;   // a click or key: the tooltip goes\n", ""),
    (G, "        pthread_cond_timedwait(&timer.cv, &timer.mu, &ts);", "        pthread_cond_wait(&timer.cv, &timer.mu);"),
    ('gui/input.c', "    if (h.mode >= 0) return TIP_MODE + h.mode;\n", ""),
    # F6.3: the icon view's sizes
    ('gui/icons.c', "    return w > CELL_W_MIN ? w : CELL_W_MIN;", "    return w;"),
    ('gui/icons.c', "    return grid_px() + CELL_H_ROOM;", "    return 48 + CELL_H_ROOM;"),
    ('gui/icons.c', "    return app.ui.grid ? grid_px() : ICON_PX;", "    return app.ui.grid ? 48 : ICON_PX;"),
    ('gui/icons.c', "        if (img.data && big > want) ImageResize(", "        if (0) ImageResize("),
    (G, "        if (app.zoomed) ui_refit(&app.ui.list, cols, rows), app.zoomed = false, dirty = true;\n", ""),
    ('gui/actions.c', "        if (a.kind == UA_ZOOM) app.zoomed = true;\n", ""),
    ('gui/topbar.c', "    int k = (int)lroundf((x - d.boundingBox.x - ZOOM_KNOB_W / 2.0f) / step);", "    int k = (int)((x - d.boundingBox.x - ZOOM_KNOB_W / 2.0f) / step);"),
    ('gui/input.c', "    if (h.zoom) in->zoom_to = zoom_level_at(x), app.zoom_drag = true;", "    if (h.zoom) in->zoom_to = zoom_level_at(x);"),
    ('gui/input.c', "    in->zoom_to = zoom_level_at(x);\n    return false;", "    return false;"),
    ('gui/input.c', "    if (x >= side_w() && ctrl) in->zoom_wheel = notches;", "    if (0) in->zoom_wheel = notches;"),
    ('gui/input.c', "    if ((m == GLFW_MOD_CONTROL || m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT)) && (key == GLFW_KEY_EQUAL", "    if ((m == GLFW_MOD_CONTROL) && (key == GLFW_KEY_EQUAL"),
    ('gui/input.c', " (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT)) return UK_ZOOM_OUT;", " key == GLFW_KEY_MINUS) return UK_ZOOM_OUT;"),
    ('gui/frame.c', "            if (app.ui.grid) layout_zoom();\n", ""),
    ('gui/frame.c', "    Clay_FloatingAttachPoints at = above && right ?", "    Clay_FloatingAttachPoints at = 0 ?"),
    ('gui/input.c', "    if (h.zoom) return TIP_ZOOM;\n", ""),
    ('gui/frame.c', '        { UB_ZOOM_OUT, "Smaller icons (Ctrl+-)" }, { UB_ZOOM_IN, "Bigger icons (Ctrl+=)" },', '        { UB_ZOOM_OUT, "Smaller icons (Ctrl+-)" },'),
    # the hover for the pointer where it is (Michael, 2026-10-24)
    ('gui/input.c', "    return on ? hover_at(x, y) : hover_at(-1e6f, -1e6f);", "    return hover_at(x, y);"),
    (G, "            if (scripted ? script.pointing && hover_refresh(true, script.px, script.py) : hover_refresh(IsCursorOnScreen(), m.x, m.y))\n                dirty = true, glfwPostEmptyEvent();\n", ""),
    # a full icon cache at a zoom (reviewer-2, F6.3 review)
    (G, "        if (app.icons_missed && app.icons_reset_armed) icons_reset()", "        if (app.icons_missed) icons_reset()"),
    ('gui/actions.c', "        app.icons_reset_armed = true;   // the other", "        ;   // the other"),
    ('gui/icons.c', "return app.icons_missed = true, arena_restore(&scratch, mark), NULL;", "return arena_restore(&scratch, mark), NULL;"),
    # F6.4: previews in the icon view
    (TB, "        if (thumbs.e[i].state == TS_ASKED) thumbs.e[i].state = TS_UNKNOWN;\n", ""),
    (TB, "    if ((e->state == TS_UNKNOWN && (draw || kind != TK_NONE)) || (draw && e->state == TS_DROPPED)) {", "    if (e->state == TS_UNKNOWN || e->state == TS_DROPPED) {"),
    (TB, "    if ((e->state == TS_UNKNOWN && (draw || kind != TK_NONE)) || (draw && e->state == TS_DROPPED)) {", "    if (e->state == TS_UNKNOWN || (draw && e->state == TS_DROPPED)) {"),
    (TB, "    if (draw) e->drawn = thumbs.frame, thumbs.drawn_now++;", "    if (draw) thumbs.drawn_now++;"),
    (TB, "thumbs.e[i].state == TS_SHOWN && thumbs.e[i].drawn < thumbs.frame && (old < 0", "thumbs.e[i].state == TS_SHOWN && (old < 0"),
    (TB, "    while (thumbs.held > thumbs.held_max) {", "    while (0) {"),
    (TB, "    if (e->mtime != mtime && e->state != TS_ASKED) {", "    if (0) {"),
    (TB, "        if ((got == TG_CACHED || got == TG_MADE) && out.w <= THUMB_PX_MAX", "        if (got == TG_MADE && out.w <= THUMB_PX_MAX"),
    (TB, "                             .make = local && kind != TK_NONE && size <= thumbs.src_max, .px = px, .src_max = thumbs.src_max };", "                             .make = kind != TK_NONE && size <= thumbs.src_max, .px = px, .src_max = thumbs.src_max };"),
    ('gui/files.c', "    for (isize i = ahead; i < ahead + (isize)app.ui.list.rows * cols && i < app.list.n; i++) entry_thumb(i, false);\n", ""),
    ('gui/files.c', "        Texture2D *th = entry_thumb(i, true);", "        Texture2D *th = NULL;"),
    ('gui/files.c', "CLAY_SIZING_FIXED(th->width / s), CLAY_SIZING_FIXED(th->height / s)", "CLAY_SIZING_FIXED(px), CLAY_SIZING_FIXED(px)"),
    # ---- F7.1: the menu bar, New Window, Create New Empty File, Close All Windows
    ('gui/input.c', "    if (app.ui.menu_on && modal_kind() == MODAL_NONE) return h;\n", "", 'f7'),
    ('gui/input.c', "        in->menu_away = h.menu_title < 0 && h.menu_item < 0;", "        in->menu_away = false;", 'f7'),
    ('gui/input.c', "    changed |= ui_menu_hover(&app.ui, h.menu_title, h.menu_item);", "", 'f7'),
    ('gui/input.c', '    app.hov = h;\n', '', 'f7'),
    ('gui/input.c', "            if (in_box(CLAY_IDI(\"menu-title\", m), x, y)) h.menu_title = m;", "", 'f7'),
    ('gui/actions.c', '    UndoKind kind = file ? UNDO_MKFILE : UNDO_MKDIR;', '    UndoKind kind = UNDO_MKDIR;', 'f7'),
    ('gui/actions.c', 'bool ok = file ? op_mkfile(', 'bool ok = false ? op_mkfile(', 'f7'),
    ('gui/actions.c', '    case UA_NEWFILE: new_made(true); break;', '    case UA_NEWFILE: new_made(false); break;', 'f7'),
    ('gui/actions.c', 'if (spawn_window(app.exe, dir, &err))', 'if (spawn_window("ofx", dir, &err))', 'f7'),
    (G, "    if (app.windows[0]) win_signal_others(app.windows, getpid(), SIGUSR1);", "", 'f7'),
    (G, "    if (quit && jobs.nrun + jobs.nwait > 0) close_asked = true;", "", 'f7'),
    (G, "        if (sig == SIGUSR1) {", "        if (sig == SIGUSR2) {", 'f7'),
    (G, "        if (atomic_exchange(&close_signal, false)) close_asked = true, closing_all = true;", "", 'f7'),
    (G, "    if (win_dir(app.windows, sizeof app.windows) && !win_register(app.windows, getpid(), &werr)) {", "    if (win_dir(app.windows, sizeof app.windows) && false) {", 'f7'),
    (G, "    if (app.windows[0]) win_unregister(app.windows, getpid());", "", 'f7'),
    (G, "    sigaddset(&quit_signals, SIGUSR1);", "", 'f7'),
    ('gui/input.c', "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_N) return UK_NEW_WINDOW;", "", 'f7'),
    ('gui/input.c', "    case GLFW_KEY_F10: return m == 0 ? UK_F10 : UK_NONE;", "", 'f7'),
    ('gui/input.c', "key >= GLFW_KEY_A && key <= GLFW_KEY_Z) ev_push((UiEvent){ UK_MENU, (u32)('a' + key - GLFW_KEY_A) });", "key >= GLFW_KEY_A && key <= GLFW_KEY_Z) ev_push((UiEvent){ UK_MENU, (u32)('A' + key - GLFW_KEY_A) });", 'f7'),
    ('gui/frame.c', "        float body_h = (float)(h - bar_h() - HEADER_H - FOOTER_H);", "        float body_h = (float)(h - HEADER_H - FOOTER_H);", 'f7'),
    ('gui/files.c', "    int r = (h - bar_h() - HEADER_H - FOOTER_H - PAD) / SIDE_ROW_H;", "    int r = (h - HEADER_H - FOOTER_H - PAD) / SIDE_ROW_H;", 'f7'),
    ('gui/frame.c', "        if (bar_shown()) layout_menubar();\n", "", 'f7'),
    ('gui/topbar.c', "                if (open) layout_menu(m);", "", 'f7'),
    ('gui/actions.c', '    ui_edit_open_all(&app.ui, str_from_c(made));', '    ui_edit_start(&app.ui, str_from_c(made), false);', 'f3'),
    ('gui/input.c', "    return kind_is_dir(app.list.e[i].kind);\n}", "    return false;\n}", 'f7'),
    ('gui/folder.c', "    if (e->size == 0 && (e->kind == K_FILE || e->kind == K_LINK_FILE) && apps_default(", "    if (e->kind == K_FILE && false && apps_default(", 'f7'),
    ('gui/folder.c', "    if (e->size == 0 && (e->kind == K_FILE || e->kind == K_LINK_FILE) && apps_default(", "    if ((e->kind == K_FILE || e->kind == K_LINK_FILE) && apps_default(", 'f7'),
    ('gui/folder.c', "        if (spawn_launch(desktop, path, &err)) set_status(false, \"opened %.*s\", STR_ARG(e->name));\n        else set_status(true, \"%s\", err.msg);\n        return;", "        if (spawn_launch(desktop, path, &err)) set_status(false, \"opened %.*s\", STR_ARG(e->name));\n        else set_status(true, \"%s\", err.msg);", 'f7'),
    ('gui/panels.c', '    app.ui.prop_can = app.ui.view != UI_VIEW_TRASH && !props.broken && props.st.st_uid == geteuid();', '    app.ui.prop_can = !props.broken && props.st.st_uid == geteuid();', 'f72'),
    ('gui/panels.c', '    app.ui.prop_can = app.ui.view != UI_VIEW_TRASH && !props.broken && props.st.st_uid == geteuid();', '    app.ui.prop_can = app.ui.view != UI_VIEW_TRASH && !props.broken;', 'f72'),
    ('gui/panels.c', '        props.broken = stat(path, &props.st) != 0;', '        props.broken = false;', 'f72'),
    ('gui/panels.c', '        props.link[k > 0 ? k : 0] = 0;', '        props.link[0] = 0;', 'f72'),
    ('gui/panels.c', '    if (n == 0 && app.ui.view != UI_VIEW_TRASH && app.ui.view != UI_VIEW_SEARCH) props_path[0] = arena_cstr(&propsa, str_from_c(app.cwd)), n = 1;', '', 'f72'),
    ('gui/panels.c', '    if (app.ui.view == UI_VIEW_TRASH) snprintf(props.where, sizeof props.where, "Trash");', '', 'f72'),
    # Not mutants: `n >= bn` in props_drive (mountinfo lists a drive before those mounted inside it,
    # so the last above is the longest); prop_can in props_chmod (ui/ sends no UA_CHMOD without it);
    # walk_tick's sums (the final ones are the same; progress can't be timed in a script); `n > 0`
    # in walk_ask (an empty walk ends at once).
    ('gui/panels.c', '    props.walking = props.local;\n    if (props.local) walk_ask(props_path, n);', '    props.walking = false;\n    if (false) {}', 'f72'),
    (G, '    if (props.walking && walk_take(&props.sum, &props.done)) dirty = true;', '', 'f72'),
    ('gui/panels.c', '    mode_t old, perms = (props.st.st_mode & 0777) ^ perm_bit(k);', '    mode_t old, perms = (props.st.st_mode & 0777) | perm_bit(k);', 'f72'),
    ('gui/panels.c', '    undo_start(UNDO_CHMOD), undo_add(&undo, NULL, props_path[0], was);', '', 'f72'),
    ('gui/panels.c', '    snprintf(was, sizeof was, "%o %llu %llu", (unsigned)old,', '    snprintf(was, sizeof was, "%u %llu %llu", (unsigned)old,', 'f72'),
    ('gui/panels.c', '    props_read();\n    set_status(false,', '    set_status(false,', 'f72'),
    ('gui/input.c', '            if (in_box(CLAY_IDI("prop-box", k), x, y)) h.prop_box = k;', '', 'f72'),
    ('gui/input.c', '    in->prop_box = h.prop_box;\n', '', 'f72'),
    ('gui/panels.c', '    case UI_ON_PROPS: return MODAL_PROPS;\n', '', 'f72'),
    ('gui/actions.c', '    case UA_CHMOD: props_chmod((int)a.index); break;', '', 'f72'),
    ('gui/input.c', '    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_I) return UK_PROPERTIES;', '', 'f72'),
    ('gui/input.c', '    if (m == GLFW_MOD_ALT && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) return UK_TERMINAL;', '', 'f72'),
    ('gui/panels.c', '    if (statx(AT_FDCWD, path, props.broken ? AT_SYMLINK_NOFOLLOW : 0, STATX_BTIME, &sx) == 0 && (sx.stx_mask & STATX_BTIME))', '    if (false)', 'f72'),
    ('gui/panels.c', '            if (app.ui.marked ? app.ui.mark[i] : i == app.ui.list.sel) props.idx = i;', '            {}', 'f72'),
    (WK, '        if (whole && gen == atomic_load(&walk.gen)) walk.sum = s, walk.sum_gen = gen, walk.done = walk.fresh = true;', '', 'f72'),
    # ---- F7.2 follow-ups: hard links, the MIME type (Michael, 2026-10-26)
    (WK, '                    .inodes = walk_inodes };', '                    .inodes = NULL };', 'f72'),
    ('gui/panels.c', '        props.type_by_name = !app.thumb_local || !spawn_gio_type(props_path[0], props.type, sizeof props.type, 1000);', '        props.type_by_name = true;', 'f72'),
    ('gui/panels.c', '        mime_comment(&scratch, str_from_c(props.type), props.type_name, sizeof props.type_name);', '', 'f72'),
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
    ('gui/actions.c', '    case UA_OPEN: a.all ? open_marked() : open_entry(a.index); break;', '    case UA_OPEN: open_entry(a.index); break;', 'f73'),
    ('gui/actions.c', '    case UA_OPEN_WITH: ow_open(); break;', '', 'f73'),
    ('gui/actions.c', '    case UA_LAUNCH: ow_launch(a.index, a.all); break;', '', 'f73'),
    ('gui/panels.c', '    case UI_ON_OW: return MODAL_OW;\n', '', 'f73'),
    ('gui/input.c', '            if (in_box(CLAY_IDI("ow-row", r), x, y)) h.ow_row = r;', '', 'f73'),
    ('gui/input.c', '        h.ow_field = modal_kind() == MODAL_OW && !h.toggle && in_box(CLAY_ID("ow-field"), x, y);', '', 'f73'),
    ('gui/input.c', '        if (in_box(CLAY_IDI("toggle", k), x, y)) h.toggle = k;', '', 'f73'),
    ('gui/input.c', '        if (ui_press(&app.panel_press, app.ui.ow_list.top + h.ow_row, now_ms()) == 2) in->ow_drow = h.ow_row;', '        if (false) in->ow_drow = h.ow_row;', 'f73'),
    ('gui/input.c', '    in->toggle = h.toggle;\n', '', 'f73'),
    (G, '        if (app.ui.ow) in.app_name = ow.names, in.app_n = ow.shown < UI_APPS_MAX ? ow.shown : UI_APPS_MAX;', '', 'f73'),
    ('gui/input.c', '    case GLFW_KEY_TAB: return m == 0 || m == GLFW_MOD_SHIFT ? UK_TAB : UK_NONE;', '    case GLFW_KEY_TAB: return m == 0 ? UK_TAB : UK_NONE;', 'f73'),
    ('gui/files.c', '    ui_resize(&app.ui.ow_list, ow_rows(GetScreenHeight()));   // Open With\'s list (F7.3)', '', 'f73'),
    # ---- after Michael's try: listed at once, the apps' icons
    (OW, '    ui_ow_filter(&app.ui, &in);\n}', '}', 'f73'),
    (OW, '            if (access(f, R_OK) == 0) file = f;', '', 'f73'),
    (OW, '            if ((str_ends_with(icon, S(".png")) || str_ends_with(icon, S(".svg"))) && access(f, R_OK) == 0) file = f;', '', 'f73'),
    (OW, '    return s->tex.id ? s : icon_for(generic, &generic, 1);', '    return s;', 'f73'),
    (OW, '        if (file) icon_tex_load(s, file, ICON_PX);', '', 'f73'),
    (OW, '            file = icon_find(&scratch, &ow.idb, &icon, 1, ICON_PX, app.icon_scale);', '            file = NULL;', 'f73'),
    (OW, '        if (ow.icon[i].tex.id) UnloadTexture(ow.icon[i].tex);\n        ow.icon[i] = (IconSlot){ 0 }, ow.icon_tried[i] = false;', '        ow.icon_tried[i] = false;', 'f73'),
    (G, '        if (!app.ui.ow && ow.icons_on) ow_drop();   // the panel closed: its icons go (F7.3)', '', 'f73'),
    ('gui/icons.c', '    if (str_ends_with(str_from_c(file), S(".svg"))) {', '    if (false) {', 'f73'),
    ('gui/icons.c', '    if (stat(file, &st) != 0 || st.st_size > SVG_BYTES_MAX) return img;', '    if (stat(file, &st) != 0) return img;', 'f73'),
    # ---- the find bar (Michael, 2026-10-26)
    ('gui/frame.c', '        if (app.ui.finding) layout_find_bar();', '', 'f2'),
    ('gui/input.c', '        case GLFW_KEY_DOWN: return UK_SEL_DOWN;', '', 'f2'),
    (G, ' || (stat(dir, &sst) == 0 && !S_ISDIR(sst.st_mode))) {', ') {', 'f73'),
    ('gui/folder.c', '    if (!same) app.ui.find_from = app.ui.list.sel;', '', 'f2'),
    ('gui/files.c', ' -\n               (app.ui.finding ? FIND_BAR_H : 0);', ';', 'f2'),
    ('gui/input.c', '        h.find_x = in_box(CLAY_ID("find-x"), x, y);', '', 'f2'),
    ('gui/input.c', '        if (h.field < 0 && in_box(Clay_GetElementId(cz(FIELD_IDS[f].id)), x, y)) h.field = f;', '', 'f2'),
    ('gui/input.c', '    in->find_x = h.find_x, in->find_step = h.find_step;', '    in->find_step = h.find_step;', 'f2'),
    # ---- F7.4: the Edit, View and Go menus
    ('gui/frame.c', '    u->can_undo = undo.kind != UNDO_NONE && undo.n > 0;', '    u->can_undo = false;', 'f74'),
    ('gui/frame.c', '    u->can_back = hist.nback > 0, u->can_forward = hist.nfwd > 0;', '    u->can_back = hist.nback > 0, u->can_forward = true;', 'f74'),
    ('gui/frame.c', '    u->can_paste = clip.ok || own.n > 0;', '    u->can_paste = clip.ok;', 'f74'),
    ('gui/frame.c', '    u->shows_hidden = app.show_hidden;', '', 'f74'),
    ('gui/actions.c', '    case UA_RELOAD: app.reloads_asked++, app.ui.view == UI_VIEW_SEARCH ? search_again() : reload(); break;', '', 'f74'),
    ('gui/actions.c', '    case UA_TRASH_VIEW: go_to(TRASH_URI, STR_EMPTY, 0); break;', '', 'f74'),
    ('gui/input.c', '    if (m == GLFW_MOD_ALT && key == GLFW_KEY_HOME) return UK_HOMEDIR;', '', 'f74'),
    ('gui/input.c', '    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_R) return UK_RELOAD;', '', 'f74'),
    ('gui/input.c', '    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_I) return UK_INVERT;', '', 'f74'),
    ('gui/topbar.c', '            if (!ui_menu_shown(&app.ui, it) || (it == UM_SEP', '            if ((it == UM_SEP', 'f74'),
    # Left out: the marks' column and the ✓ / • drawn (pixels only); menu-view.png shows them.
    ('gui/input.c', '    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_A) return UK_SELECT_MATCH;', '', 'f74'),
    ('gui/input.c', '    if (m == GLFW_MOD_ALT && key == GLFW_KEY_END) return UK_TRASH_VIEW;', '', 'f74'),
    # Left out: the bar's "Select" label and its footer hint (pixels only); select-match.png shows them.
    ('gui/actions.c', '    if (dup.n > 0) copy_here(&dup, NULL);', '', 'f74'),
    ('gui/actions.c', '    dup.cut = false, dup.n = targets(&pastea, dup.path, CLIP_PATHS_MAX);', '    dup.cut = true, dup.n = targets(&pastea, dup.path, CLIP_PATHS_MAX);', 'f74'),
    ('gui/actions.c', '        if (!too_many_marked()) duplicate();\n', '', 'f74'),
    # ---- F7.5: the Bookmarks menu
    ('gui/folder.c', '    arena_reset(&bma);\n    bm_parse(&bma, read_file(&bma, bm), &bmarks);', '    arena_reset(&bma);', 'f75'),
    ('gui/frame.c', '        if (strcmp(bmarks.b[i].path, app.cwd) == 0) u->bm_here = u->bm_n;', '', 'f75'),
    ('gui/frame.c', '        if (!bmarks.b[i].path[0]) continue;\n        if (strcmp(bmarks.b[i].path, app.cwd)', '        if (strcmp(bmarks.b[i].path, app.cwd)', 'f75'),
    ('gui/folder.c', '        if (bmarks.b[i].path[0] && k-- == 0) return &bmarks.b[i];', '        if (k-- == 0) return &bmarks.b[i];', 'f75'),
    ('gui/folder.c', '    return b->label.len ? b->label : strcmp(b->path, "/") == 0', '    return false ? b->label : strcmp(b->path, "/") == 0', 'f75'),
    ('gui/folder.c', '    bool ok = bm_read(&bmwa, file, &now, &err) && bm_add(', '    bool ok = (now.n = 0, true) && bm_add(', 'f75'),
    # Not a mutant: bookmark_add's places_reload, the watch reads them again 100 ms later; it is kept so
    # the menu is right at once.
    ('gui/actions.c', '        if (b) go_to(b->path, STR_EMPTY, 0);', '', 'f75'),
    ('gui/input.c', '    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_D) return UK_ADD_BOOKMARK;   // (F7.5, as Nemo)', '', 'f75'),
    ('gui/topbar.c', ' || (it == UM_SEP && !ui_menu_sep_shown(&app.ui, m, i))) continue;', ') continue;', 'f74'),
    (W, '        if (e->wd == atomic_load(&watch.bm_wd) && e->len && strcmp(e->name, "bookmarks") == 0) *mounts = true;', '', 'f75'),
    (W, '    watch_bookmarks();\n    watch_conf();', '    watch_conf();', 'f75'),
    # ---- F7.5: Edit Bookmarks
    ('gui/bookmarks.c', '    if (now.full) {', '    if (false) {', 'f75'),
    # Not a mutant: bmp_change's bm_locate for `i`; the panel shows the file as it is (a change
    # reads it again), so only a race between the watch and a key tells; test_f75.c tests bm_locate.
    ('gui/bookmarks.c', '        bm_swap(&now, j, other);', '', 'f75'),
    ('gui/bookmarks.c', '        k = op == UA_BM_UP ? i - 1 : i + 1;', '', 'f75'),
    ('gui/bookmarks.c', '        bm_remove(&now, j);', '', 'f75'),
    ('gui/bookmarks.c', '        bm_rename(&bmwa, &now, j, (Str){ app.ui.bmp_name.b, app.ui.bmp_name.len });', '', 'f75'),
    ('gui/bookmarks.c', '    places_reload();\n    bmp_refresh(k);', '    bmp_refresh(k);', 'f75'),
    ('gui/bookmarks.c', '        if (bmarks.b[a.index].path[0]) go_to(bmarks.b[a.index].path, STR_EMPTY, 0);', '        if (true) go_to(bmarks.b[a.index].path, STR_EMPTY, 0);', 'f75'),
    ('gui/bookmarks.c', '        ui_line_set(ui_bmp_line(&app.ui), s);\n', '', 'f75'),
    ('gui/bookmarks.c', '    case UA_BM_REMOVE: case UA_BM_UP: case UA_BM_DOWN: case UA_BM_RENAME: bmp_change(a.kind, (int)a.index); break;', '', 'f75'),
    ('gui/actions.c', '    case UA_BM_EDIT: case UA_BM_GO: case UA_BM_NAME: case UA_BM_REMOVE: case UA_BM_UP: case UA_BM_DOWN: case UA_BM_RENAME: bmp_act(a); break;', '', 'f75'),
    ('gui/frame.c', '        if (u->bm_more++ == 0) u->bm_more_at = i;', '        u->bm_more++;', 'f75'),
    ('gui/frame.c', '        if (!bmarks.b[i].path[0] || local++ < UI_MENU_BOOKMARKS) continue;', '        if (local++ < UI_MENU_BOOKMARKS) continue;', 'f75'),
    ('gui/panels.c', '    case UI_ON_BM: return MODAL_BM;\n', '', 'f75'),
    ('gui/input.c', '            if (in_box(CLAY_IDI("bmp-row", r), x, y)) h.bmp_row = r;', '', 'f75'),
    ('gui/input.c', '        if (ui_press(&app.panel_press, app.ui.bmp_list.top + h.bmp_row, now_ms()) == 2) in->bmp_drow = h.bmp_row;', '        if (false) in->bmp_drow = h.bmp_row;', 'f75'),
    ('gui/input.c', '    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_B) return UK_EDIT_BOOKMARKS;', '', 'f75'),
    ('gui/input.c', '    if (m == GLFW_MOD_ALT && key == GLFW_KEY_DOWN) return UK_ALT_DOWN;   // (Edit Bookmarks\' move down)', '', 'f75'),
    ('gui/folder.c', '        int at = bm_keep >= 0 ? bm_locate(&bmarks, &prev, bm_keep) : -1;', '        int at = -1;', 'f75'),
    # Not run: the bmp buttons' and the field's drawing, the hint (pixels); edit-bookmarks.png shows them.
    # ---- F7.5: reviewer-2's MEDIUM, a bookmarks file not read whole
    ('gui/folder.c', '    bool ok = bm_read(&bmwa, file, &now, &err) && bm_add(', '    bool ok = (bm_read(&bmwa, file, &now, &err) || true) && bm_add(', 'f75'),
    # Not run: bmp_change's bm_read refusal (with the file unreadable the panel lists no line to act
    # on) and the empty-format refusals (an over-a-MiB file); test_f75.c covers bm_read and bm_format.
    # ---- F7.5: Keyboard Shortcuts
    ('gui/panels.c', '    case UI_ON_KEYS: return MODAL_KEYS;\n', '', 'f75'),
    ('gui/input.c', '    if ((m == 0 || m == GLFW_MOD_CONTROL) && key == GLFW_KEY_F1) return UK_SHORTCUTS;', '    if (m == 0 && key == GLFW_KEY_F1) return UK_SHORTCUTS;', 'f75'),
    ('gui/input.c', '    if (app.ui.ow || app.ui.keys) return in->ow_wheel = notches, false;', '    if (app.ui.ow) return in->ow_wheel = notches, false;', 'f75'),
    ('gui/files.c', '    ui_resize(&app.ui.keys_list, keys_rows(GetScreenHeight()));   // Keyboard Shortcuts\'', '', 'f75'),
    # Left out: the panel's drawing (pixels); shortcuts.png shows it.
    # ---- Help → About (2026-10-27); its drawing left out (pixels), about.png shows it
    ('gui/panels.c', '    case UI_ON_ABOUT: return MODAL_ABOUT;\n', '', 'f75'),
    ('gui/panels.c', '    else if (k == MODAL_ABOUT) layout_about_panel();', '', 'f75'),
    # ---- F8 step 1: the files' right-click menus. Not run: input_real's right button (scripts press
    # through right_at), the footer's hint (pixels); ctx-entry.png, ctx-key.png, ctx-here.png show them.
    # Nor ctx_place's menu above the cursor's row: f8's four rows leave room under the last.
    ('gui/input.c', '    if ((m == GLFW_MOD_SHIFT && key == GLFW_KEY_F10) || (m == 0 && key == GLFW_KEY_MENU)) return UK_CONTEXT;', '    if (m == 0 && key == GLFW_KEY_MENU) return UK_CONTEXT;', 'f8'),
    ('gui/input.c', '    if ((m == GLFW_MOD_SHIFT && key == GLFW_KEY_F10) || (m == 0 && key == GLFW_KEY_MENU)) return UK_CONTEXT;', '    if (m == GLFW_MOD_SHIFT && key == GLFW_KEY_F10) return UK_CONTEXT;', 'f8'),
    ('gui/frame.c', '        if (app.ui.menu_on && app.ui.menu >= UI_MENUS) layout_menu(app.ui.menu);   // a right-click menu (F8)', '', 'f8'),
    ('gui/input.c', '    if (app.ui.menu_on) {\n        in->menu_away = true;\n        return;\n    }\n    if (modal_kind()', '    if (modal_kind()', 'f8'),
    ('gui/input.c', '    else if (h.row >= 0) in->ctx_row = h.row;', '    else if (false) in->ctx_row = h.row;', 'f8'),
    ('gui/input.c', '    else if (in_box(CLAY_ID("list"), x, y)) in->ctx_here = true;', '    else in->ctx_here = true;', 'f8'),
    ('gui/input.c', '    else if (in_box(CLAY_ID("list"), x, y)) in->ctx_here = true;', '', 'f8'),
    ('gui/input.c', '        if (x + sz.x > w) x -= sz.x;', '', 'f8'),
    ('gui/input.c', '        if (y + sz.y > h) y -= sz.y;', '', 'f8'),
    ('gui/input.c', '    if (x + sz.x > w) x = w - sz.x;\n    if (y + sz.y > h) y = h - sz.y;', '', 'f8'),
    ('gui/input.c', '            y = r.boundingBox.y + r.boundingBox.height;', '            y = r.boundingBox.y;', 'f8'),
    (G, '        if (ctx_up && !app.ctx_placed) ctx_place();', '', 'f8'),
    ('gui/actions.c', '    case UA_PROPS: props_open(a.index == 1 ? app.cwd : a.index == 2 ? app.ctx_path : NULL); break;', '    case UA_PROPS: props_open(a.index == 2 ? app.ctx_path : NULL); break;', 'f8'),
    ('gui/panels.c', '    if (!here && app.have_list && app.list.n > 0 && n == 1)', '    if (app.have_list && app.list.n > 0 && n == 1)', 'f8'),
    # ---- F8 step 2: a folder's own entries, Make Link, Follow Link. Not run: make_link's messages
    # (the status line), its name-too-long branch (NAME_MAX names in a scratch tree); into_itself's
    # dest (a mutant would copy a folder into itself without end; f8b checks the refusal).
    ('gui/input.c', "        case GLFW_KEY_M: return UK_MAKE_LINK;   // (F8, as Nemo)", '', 'f8'),
    ('gui/frame.c', '    u->sel_link = e && (e->kind == K_LINK_FILE || e->kind == K_LINK_DIR);', '    u->sel_link = false;', 'f8'),
    ('gui/frame.c', '    u->sel_bm = e && kind_is_dir(e->kind) && entry_path(u->list.sel, p, sizeof p) && bm_find(&bmarks, p) >= 0;', '    u->sel_bm = false;', 'f8'),
    ('gui/folder.c', '    bool ok = bm_read(&bmwa, file, &now, &err) && bm_add(&bmwa, &now, dir, &err);', '    bool ok = bm_read(&bmwa, file, &now, &err) && bm_add(&bmwa, &now, app.cwd, &err);', 'f8'),
    ('gui/actions.c', '        if (same_dir(sdir, dest)) {', '        if (same_dir(sdir, app.cwd)) {', 'f8'),
    ('gui/actions.c', '        if (!path_put(dst, sizeof dst, dest, str_from_c(name))) continue;', '        if (!path_put(dst, sizeof dst, app.cwd, str_from_c(name))) continue;', 'f8'),
    ('gui/actions.c', '        if (!op_symlink(in, paths[i], str_from_c(name), one, sizeof one, &err)', '        if (!op_symlink(in, in, str_from_c(name), one, sizeof one, &err)', 'f8'),
    ('gui/actions.c', 'if (done++ == 0) undo_start(UNDO_LINK);', 'if (done++ == 0) undo_gen++;', 'f8'),
    ('gui/actions.c', '        undo_add(&undo, paths[i], p, NULL);\n', '', 'f8'),
    ('gui/actions.c', '    if (done) ui_mark_clear(&app.ui), go_to(app.cwd, app.ui.view == UI_VIEW_SEARCH ? sel_name() : str_from_c(made), app.ui.list.sel);', '    if (done) ui_mark_clear(&app.ui);', 'f8'),
    ('gui/actions.c', '    go_to(dir, strcmp(real, "/") == 0 ? STR_EMPTY : base, 0);', '    go_to(dir, STR_EMPTY, 0);', 'f8'),
    ('gui/actions.c', '        search_in(act_dir(a, in, sizeof in));', '        search_in(app.cwd);', 'f9'),
    ('gui/actions.c', '        window_on(act_dir(a, on, sizeof on));', '        window_on(app.cwd);', 'f8'),
    ('gui/actions.c', '        else if (a.index < app.list.n && entry_path(a.index, into, sizeof into)) paste(into);', '        else paste(app.cwd);', 'f8'),
    ('gui/actions.c', '        else if (a.index < app.list.n && entry_path(a.index, bm, sizeof bm)) bookmark_add(bm);', '        else bookmark_add(app.cwd);', 'f8'),
    ('gui/actions.c', '        if (!too_many_marked()) make_link();\n', '', 'f8'),
    ('gui/actions.c', '    case UA_FOLLOW: follow_link(a.index); break;', '', 'f8'),
    # ---- F8 step 3: a folder elsewhere's menu. Not run: the lit sidebar row (side_ctx, pixels;
    # ctx-bookmark.png shows it), Eject (f8c has no removable drive; F6's eject_start is f6's).
    ('gui/input.c', '    else if (h.side >= 0) in->ctx_side = h.side;', '', 'f8'),
    ('gui/input.c', '    else if (h.bar == UB_CRUMB) in->ctx_crumb = h.crumb;', '', 'f8'),
    (G, '        if (ctx_up && !app.ctx_placed && app.ui.menu == UI_CTX_FOLDER) ctx_facts();', '', 'f8'),
    ('gui/input.c', '    u->ctx_trash = strcmp(app.ctx_path, TRASH_URI) == 0;', '    u->ctx_trash = false;', 'f8'),
    ('gui/input.c', '    u->ctx_full = u->ctx_trash && (trash_state(), app.trash_full);', '    u->ctx_full = u->ctx_trash;', 'f8'),
    ('gui/folder.c', ' < (int)sizeof files && !dir_empty(files)) return true;', ' < (int)sizeof files) return true;', 'f8'),
    ('gui/input.c', '    u->ctx_bmed = bm_find(&bmarks, app.ctx_path) >= 0;', '    u->ctx_bmed = false;', 'f8'),
    ('gui/input.c', '    u->ctx_bms = bmarks.n;', '', 'f8'),
    ('gui/input.c', '        u->ctx_place = true;\n', '', 'f8'),
    ('gui/input.c', '        if (pl->kind == PL_BOOKMARK) u->ctx_bmk = bm_find(&bmarks, pl->path);', '', 'f8'),
    ('gui/input.c', '    case UM_OPEN_IT: go_to(dir, STR_EMPTY, 0); break;', '', 'f8'),
    ('gui/input.c', '    case UM_OPEN_TAB: tab_open_bg(dir); break;', '', 'f8'),
    ('gui/input.c', '    case UM_TERMINAL_IN: terminal_in(dir); break;', '    case UM_TERMINAL_IN: terminal_in(app.cwd); break;', 'f8'),
    ('gui/input.c', '    case UM_CUT_IT: case UM_COPY_IT: copy_targets(it == UM_CUT_IT, dir); break;', '    case UM_CUT_IT: case UM_COPY_IT: copy_targets(it == UM_CUT_IT, NULL); break;', 'f8'),
    ('gui/actions.c', '    if (only) own.path[0] = arena_cstr(&owna, str_from_c(only)), own.n = 1;', '    if (false) own.path[0] = arena_cstr(&owna, str_from_c(only)), own.n = 1;', 'f8'),
    ('gui/actions.c', '    case UA_PROPS: props_open(a.index == 1 ? app.cwd : a.index == 2 ? app.ctx_path : NULL); break;', '    case UA_PROPS: props_open(a.index == 1 ? app.cwd : NULL); break;', 'f8'),
    ('gui/actions.c', '    case UA_FOLDER_DO: folder_do((UiMenuItem)a.index); break;', '', 'f8'),
    ('gui/input.c', '        if (f.found) {   // under the sidebar row or path button (F8)', '        if (false) {   // under the sidebar row or path button (F8)', 'f8'),
    # ---- F8 step 4: a tab's menu
    ('gui/input.c', '    if (h.tab >= 0) in->ctx_tab = h.tab;\n    else if', '    if (false) in->ctx_tab = h.tab;\n    else if', 'f8'),
    ('gui/frame.c', '    u->tab_at = tabbar.cur, u->tab_n = tabbar.n;   // (the tab menu', '    u->tab_at = 0, u->tab_n = tabbar.n;   // (the tab menu', 'f8'),
    ('gui/actions.c', '            if (i != tabbar.cur) tab_close(i);', '            if (i != 0) tab_close(i);', 'f8'),
    # ---- F8 review: a folder menu closes when the places are read again; Paste Into greyed when
    # its folder takes no new files (f8c sees it open on one that does; ui/'s test sees it greyed)
    ('gui/folder.c', '    if (app.ui.menu_on && app.ui.menu == UI_CTX_FOLDER && app.ui.ctx_crumb < 0) app.ui.menu_on = false;', '', 'f8'),
    ('gui/input.c', '    u->ctx_w = app.ctx_path[0] && access(app.ctx_path, W_OK) == 0;', '    u->ctx_w = false;', 'f8'),
    (G, '            app.ui.ctx_w = app.ui.ctx_dir && app.ui.list.sel < app.list.n && entry_path(app.ui.list.sel, e, sizeof e) && access(e, W_OK) == 0;', '            app.ui.ctx_w = false;', 'f8'),
    # ---- the list's Owner, Group, Permissions and links' targets (2026-10-27). Not run: the cells
    # and the target drawn (pixels: f8b's list-columns.png, list-no-perms.png show them)
    (G, '        app.ui.side_mode = st.side, app.ui.perms_cols = !st.no_perms,', '        app.ui.side_mode = st.side, app.ui.perms_cols = true,', 'f8'),
    ('gui/actions.c', '        if (state_path(path, sizeof path) && !state_put(&scratch, path, "perms", app.ui.perms_cols ? "on" : "off", &err))', '        if (state_path(path, sizeof path) && !state_put(&scratch, path, "perms", "on", &err))', 'f8'),
    # ---- the three columns give way to a Name under 200 px (2026-10-27)
    ('gui/files.c', '    return app.ui.perms_cols && rest - perms_w() >= NAME_MIN_W;', '    return app.ui.perms_cols;', 'f8'),
    ('gui/files.c', '    return app.ui.perms_cols && rest - perms_w() >= NAME_MIN_W;', '    return rest - perms_w() >= NAME_MIN_W;', 'f8'),
    # ---- View → Show Colors and the note (2026-10-27). Not run: the colours drawn (pixels:
    # f8e's list-plain.png, f8b's list-columns.png)
    (G, '        app.ui.colors = !st.no_colors,', '        app.ui.colors = true,', 'f8'),
    ('gui/actions.c', '        if (state_path(path, sizeof path) && !state_put(&scratch, path, "colors", app.ui.colors ? "on" : "off", &err))', '        if (state_path(path, sizeof path) && !state_put(&scratch, path, "colors", "on", &err))', 'f8'),
    ('gui/footer.c', '                  .cramped = app.ui.perms_cols && !app.ui.grid && !perms_shown((float)GetScreenWidth()) };', '                  .cramped = false };', 'f8'),
    ('gui/footer.c', '                  .cramped = app.ui.perms_cols && !app.ui.grid && !perms_shown((float)GetScreenWidth()) };', '                  .cramped = !perms_shown((float)GetScreenWidth()) };', 'f8'),
    # ---- View → Show Menu (2026-10-27). Not run: the note's drawing and footer hint (pixels:
    # f8b's menubar-hidden.png, menubar-float.png)
    (G, '    return !app.ui.bar_hidden || (app.ui.menu_on && app.ui.menu < UI_MENUS);', '    return !app.ui.bar_hidden;', 'f8'),
    (G, '    return !app.ui.bar_hidden || (app.ui.menu_on && app.ui.menu < UI_MENUS);', '    return true;', 'f8'),
    (G, ', app.ui.perms_cols = !st.no_perms, app.ui.bar_hidden = st.no_bar,', ', app.ui.perms_cols = !st.no_perms,', 'f8'),
    ('gui/actions.c', '        if (state_path(path, sizeof path) && !state_put(&scratch, path, "menubar", app.ui.bar_hidden ? "off" : "on", &err))', '        if (state_path(path, sizeof path) && !state_put(&scratch, path, "menubar", "on", &err))', 'f8'),
    ('gui/panels.c', '    case UI_ON_BAR: return MODAL_BAR;\n', '', 'f8'),
    # ---- View → Show Group, Ctrl+P, Ctrl+G (2026-10-27). Not run: the key chords' GLFW mapping
    # (scripts send ui/'s keys); the cells drawn (pixels: f8b's list-group.png)
    (G, '        app.ui.colors = !st.no_colors, app.ui.group_col = st.group, app.show_hidden = st.hidden;', '        app.ui.colors = !st.no_colors, app.show_hidden = st.hidden;', 'f8'),
    ('gui/actions.c', '        if (state_path(path, sizeof path) && !state_put(&scratch, path, "group", app.ui.group_col ? "on" : "off", &err))', '        if (state_path(path, sizeof path) && !state_put(&scratch, path, "group", "on", &err))', 'f8'),
    ('gui/files.c', '    return app.ui.group_col ? 3 * GAP + OWNER_W + GROUP_W + PERM_W : 2 * GAP + OWNER_W + PERM_W;', '    return 3 * GAP + OWNER_W + GROUP_W + PERM_W;', 'f8'),
    ('gui/files.c', '            if (k == 1 && !app.ui.group_col) continue;', '', 'f8'),
    ('gui/files.c', '            if (app.ui.group_col) info_cell(CLAY_IDI("group", r)', '            info_cell(CLAY_IDI("group", r)', 'f8'),
    # ---- quit doesn't wait for a hung drive (2026-10-26: no mutant was hang's or f6b's alone)
    (G, '    if (pthread_timedjoin_np(t, NULL, until) != 0) pthread_detach(t);', '    pthread_join(t, NULL);', 'hang'),   # (join_by: every worker's stop)
    ('gui/script.c', '    if (atomic_load(&idle_gen) != gen) return NULL;\n', '', 'f2 f63'),
    # ---- --select (FileManager1's ShowItems, 2026-10-26)
    (G, '    if ((selecting && strcmp(dir, "/") != 0) || (stat(', '    if ((stat(', 'f73'),
    (G, '    if (selecting ? !select_path(start, dir, sizeof dir) : !realpath(', '    if (!realpath(', 'f73'),
    # ---- its count and chevrons (Michael, 2026-10-26)
    ('gui/input.c', '        h.find_step = in_box(CLAY_ID("find-prev"), x, y) ? -1 : in_box(CLAY_ID("find-next"), x, y) ? 1 : 0;', '', 'f2'),
    ('gui/input.c', '        h.find_step = in_box(CLAY_ID("find-prev"), x, y) ? -1 : in_box(CLAY_ID("find-next"), x, y) ? 1 : 0;', '        h.find_step = in_box(CLAY_ID("find-prev"), x, y) ? -1 : 0;', 'f2'),
    ('gui/footer.c', '    if (at > 0) snprintf(buf, cap, "%td of %td", at, n);', '    if (false) snprintf(buf, cap, "%td of %td", at, n);', 'f2'),
    ('gui/footer.c', 'c.lists != app.lists || c.sel != app.ui.list.sel || c.len', 'c.lists != app.lists || c.len', 'f2'),
    ('gui/footer.c', 'c.len != m->len || memcmp(c.text, m->text, (size_t)m->len) ||', 'c.len != m->len ||', 'f2'),
    ('gui/input.c', '    if (h.find_step) return TIP_FIND + (h.find_step > 0);', '', 'f2'),
    ('gui/footer.c', 'c.finding != app.ui.finding || c.find_len != app.ui.find.len || ', '', 'f2'),
    ('gui/frame.c', '"Previous match (Up)" : "Next match (Down)"', '"Next match (Down)" : "Next match (Down)"', 'f2'),
    ('gui/input.c', '            if (in_box(CLAY_IDI("toggle", FIELD_IDS[f].toggles + k), x, y)) h.field = f, h.field_toggle = k;', '', 'f72'),   # (the guard is ui_handle's since 2026-10-28)
    ('gui/actions.c', '    ui_find_close(&app.ui);   // (a tab\'s own find isn\'t kept)', '', 'f2'),
    # ---- F7.3.1: Set as default
    (OW, '    if (strcmp(ow.type, "application/x-zerosize") == 0) snprintf(', '    if (false) snprintf(', 'f73'),
    (OW, '    else if (apps_shadowed(&owa, str_from_c(ow.type), shadow, sizeof shadow))', '    else if (false)', 'f73'),
    (OW, '    app.ui.ow_def_can = !ow.def_why[0];', '    app.ui.ow_def_can = true;', 'f73'),
    # Not a mutant: `&& !ow.def_why[0]` in ow_launch is inert, ui/ hands def only when gui/ allowed it.
    (OW, '    if (set && !op_set_default(&scratch, str_from_c(ow.type), id, &err)) {', '    if (false) {', 'f73'),
    (OW, '    if (set) set_status(false, "opened %s with %.*s, the default for %s now"', '    if (false) set_status(false, "opened %s with %.*s, the default for %s now"', 'f73'),
    ('gui/actions.c', '    case UA_LAUNCH: ow_launch(a.index, a.all); break;', '    case UA_LAUNCH: ow_launch(a.index, false); break;', 'f73'),
    ('gui/input.c', '    for (int k = 1; k <= 4 && !h.toggle && app.ui.ow; k++)', '    for (int k = 1; k <= 3 && !h.toggle && app.ui.ow; k++)', 'f73'),
    # Left out: nsvgRasterize gone leaves a blank texture, which no state word sees (the pixels);
    # f73's ow-panel.png shows the SVG drawn.
    # ---- F9 step 1: the search view (2026-10-28). Left out: what is only pixels (the tab's label,
    # the rows' folder, the empty list's words; f9's shots show them), the walk left running after
    # leaving a search (no state word sees it), the publishing pace (the scripts wait for the end).
    ('gui/folder.c', '    if (app.ui.view == UI_VIEW_SEARCH) {\n        char dir[PATH_MAX];', '    if (false) {\n        char dir[PATH_MAX];', 'f9'),
    ('gui/folder.c', '        return path_put(out, cap, e->dir.len ? dir : app.search_root, e->name);', '        return path_put(out, cap, app.search_root, e->name);', 'f9'),
    ('gui/folder.c', '    if (!search_is(q)) {', '    if (false) {', 'f9'),
    ('gui/folder.c', '    fs_sort(l, app.ui.sort);\n    snprintf(app.search_root', '    snprintf(app.search_root', 'f9'),
    ('gui/folder.c', '    go_to_in(app.cwd, str_from_c(app.search_pick[0] ? app.search_pick : keep), app.ui.list.sel, true);', '    go_to_in(app.cwd, str_from_c(keep), app.ui.list.sel, true);', 'f9'),
    ('gui/folder.c', '    snprintf(app.cwd, sizeof app.cwd, "%s", uri);\n    app.search_pick[0] = 0;', '    app.search_pick[0] = 0;', 'f9'),
    ('gui/folder.c', '        if (text.len == 0 && inside.len == 0) app.ui.focus = UI_FOCUS_SEARCH;', '        if (false) app.ui.focus = UI_FOCUS_SEARCH;', 'f9'),
    ('gui/folder.c', '        snprintf(app.search_pick, sizeof app.search_pick, "%.*s", STR_ARG(select));\n    }', '    }', 'f9'),
    ('gui/actions.c', '    if (dir && search_uri(uri, sizeof uri, dir, STR_EMPTY, STR_EMPTY)) tab_open(uri, STR_EMPTY, true);', '    if (dir) tab_open(dir, STR_EMPTY, true);', 'f9'),
    ('gui/actions.c', '    case UA_NOT_IN_SEARCH: set_status(true, "A search isn\'t a folder: that works in one"); break;', '    case UA_NOT_IN_SEARCH: break;', 'f9'),
    ('gui/input.c', '    [UI_FIELD_SEARCH] = { "search-field", TOGGLE_SEARCH },', '    [UI_FIELD_SEARCH] = { "search-field", 0 },', 'f9'),
    ('gui/input.c', '    [UI_FIELD_SEARCH] = { "search-field", TOGGLE_SEARCH },', '    [UI_FIELD_SEARCH] = { "inside-field", TOGGLE_SEARCH },', 'f9'),
    ('gui/main.c', '        if (app.ui.view == UI_VIEW_SEARCH && search_take()) search_relist(), dirty = true;', '', 'f9'),
    (G, '        if (app.search_due && now_ms() >= app.search_due) app.search_due = 0, search_retype(), dirty = true;', '        if (app.search_due) app.search_due = 0, search_retype(), dirty = true;', 'f9'),
    ('gui/input.c', '    i64 ab = !a ? b : !b ? a : a < b ? a : b;', '    i64 ab = a;', 'f9'),
    ('gui/frame.c', '        if (app.ui.view == UI_VIEW_SEARCH) layout_search_bar();', '', 'f9'),
    ('gui/footer.c', '    else snprintf(buf, cap, "%td found%s%s%s", app.list.n, read, sep, skipped);', '    else snprintf(buf, cap, "%td found", app.list.n);', 'f9'),
    ('gui/folder.c', '    if (app.ui.view == UI_VIEW_SEARCH) {   // the folder searched (F9)\n        go_to(app.search_root, STR_EMPTY, 0);\n        return;\n    }', '', 'f9'),
    (G, '        if (app.ui.search_edited) app.ui.search_edited = false, app.search_due = now_ms() + SEARCH_TYPE_MS, timer_at(wake_due());', '        if (app.ui.search_edited) app.ui.search_edited = false, app.search_due = now_ms() + SEARCH_TYPE_MS;', 'f9'),
    ('gui/search.c', '    if (search_wrote == SEARCH_MAX) return false;', '', 'f9'),
    ('gui/search.c', '    if (search_wrote == 0 || !str_eq(dir, search_dir_last)) {', '    if (search_wrote == 0) {', 'f9'),
    ('gui/search.c', '    search_res[search_wrote++].dir = search_dir_last;', '    search_wrote++;', 'f9'),
    ('gui/search.c', ' && search.hidden == q.hidden && ', ' && ', 'f9'),
    ('gui/search.c', 'search_opts_eq(search.opts, q.o) && ', '', 'f9'),
    ('gui/search.c', '    search.done = (q.text.len == 0 && q.inside.len == 0) || !search.started;', '    search.done = !search.started;', 'f9'),
    # the review's two LOWs (reviewer-2, 2026-10-28)
    ('gui/actions.c', '    if (i >= app.list.n || !entry_path(i, path, sizeof path)) return;', '    if (i >= app.list.n || !path_put(path, sizeof path, app.cwd, app.list.e[i].name)) return;', 'f9'),
    ('gui/folder.c', '    static char key[PATH_MAX];\n    return app.have_list', '    static char key[NAME_MAX + 1];\n    return app.have_list', 'f9'),
    # ---- F9 step 2: working with results (2026-10-28). Left out: Ctrl+Enter's GLFW mapping (scripts
    # send ui/'s keys).
    ('gui/input.c', '    if (p && is_search(p)) p = app.search_root;', '', 'f9'),
    ('gui/folder.c', '        ui_line_set(ui_search_line(&app.ui), text);\n', '', 'f9'),
    ('gui/search.c', '        search_gone[i] = search_res_path(i, root, p, sizeof p) && lstat(p, &st) != 0;', '        search_gone[i] = lstat(p, &st) != 0;', 'f9'),
    ('gui/search.c', '        if (search_gone[i]) continue;\n        const Entry *r', '        const Entry *r', 'f9'),
    ('gui/search.c', '    memset(search_gone, 0, sizeof search_gone), arena_reset(&search_edits);', '    arena_reset(&search_edits);', 'f9'),
    ('gui/search.c', '        r->name = (Str){ p, name.len }, ok = true;', '        ok = true;', 'f9'),
    ('gui/search.c', '        if (search_gone[i] || !str_eq(r->name, was) || !str_eq(r->dir, dir)) continue;', '        if (search_gone[i] || !str_eq(r->name, was)) continue;', 'f9'),
    ('gui/actions.c', '    if (app.ui.view == UI_VIEW_SEARCH) search_rename(key, to);', '', 'f9'),
    ('gui/actions.c', '    if (rel.len) snprintf(keep, sizeof keep, "%.*s/%.*s", STR_ARG(rel), STR_ARG(to));', '    if (false) {}', 'f9'),
    ('gui/actions.c', '    if (e && entry_path(app.ui.list.sel, path, sizeof path)) path_parent(app.edit_dir, sizeof app.edit_dir, path);', '    if (false) {}', 'f9'),
    ('gui/actions.c', '    snprintf(app.edit_from, sizeof app.edit_from, "%.*s", e ? (int)e->name.len : 0, e ? e->name.p : "");', '    snprintf(app.edit_from, sizeof app.edit_from, "%.*s", STR_ARG(sel_name()));', 'f9'),
    ('gui/actions.c', '        if (!op_symlink(in, paths[i], str_from_c(name), one, sizeof one, &err) || !path_put(p, sizeof p, in, str_from_c(one))) continue;', '        if (!op_symlink(app.cwd, paths[i], str_from_c(name), one, sizeof one, &err) || !path_put(p, sizeof p, app.cwd, str_from_c(one))) continue;', 'f9'),
    ('gui/actions.c', '        const char *dest = into ? into : sdir;', '        const char *dest = into ? into : app.cwd;', 'f9'),
    ('gui/folder.c', '    return app.ui.view == UI_VIEW_SEARCH ? app.search_root : app.cwd;', '    return app.cwd;', 'f9'),
    ('gui/folder.c', '    if (!a.all) return here_dir();', '    if (!a.all) return app.cwd;', 'f9'),
    ('gui/actions.c', '        go_to(dir, app.list.e[a.index].name, 0);', '        go_to(dir, STR_EMPTY, 0);', 'f9'),
    ('gui/actions.c', '        path_parent(dir, sizeof dir, path);\n        go_to(dir, app.list.e[a.index].name, 0);', '        snprintf(dir, sizeof dir, "%s", app.search_root);\n        go_to(dir, app.list.e[a.index].name, 0);', 'f9'),
    ('gui/folder.c', '    char keep[PATH_MAX];\n    snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));\n    snprintf(app.search_pick', '    char keep[NAME_MAX + 1];\n    snprintf(keep, sizeof keep, "%.*s", STR_ARG(sel_name()));\n    snprintf(app.search_pick', 'f9'),
    # The system clipboard (gui/clipboard.c), run since 2026-10-28 in the test sway (clip): the lock
    # clip_get kept, the types read and offered, what each type serves, and another program's text.
    ('gui/clipboard.c', "    if (!types) {   // (left held, the next paste or clipboard change hung the window: 2026-10-28)\n        pthread_mutex_unlock(&clip.mu);\n", "    if (!types) {   // (left held, the next paste or clipboard change hung the window: 2026-10-28)\n", 'clip'),
    ('gui/clipboard.c', "    if (i >= 0 && strcmp(mime, CLIP_MIME_GNOME) == 0) clip.offers[i].types |= OFFER_GNOME;\n", "", 'clip'),
    ('gui/clipboard.c', "    if (i >= 0 && strcmp(mime, CLIP_MIME_URIS) == 0) clip.offers[i].types |= OFFER_URIS;\n", "", 'clip'),
    ('gui/clipboard.c', "    if (files) ext_data_control_source_v1_offer(clip.src, CLIP_MIME_GNOME);\n", "", 'clip'),
    ('gui/clipboard.c', "    if (files) ext_data_control_source_v1_offer(clip.src, CLIP_MIME_URIS);\n", "", 'clip'),
    ('gui/clipboard.c', "    ext_data_control_source_v1_offer(clip.src, CLIP_MIME_TEXT[1]);\n", "", 'clip'),
    ('gui/clipboard.c', "    if (strcmp(mime, CLIP_MIME_GNOME) == 0) p = clip.payload[CLIP_FMT_GNOME];", "    if (strcmp(mime, CLIP_MIME_GNOME) == 0) p = clip.payload[CLIP_FMT_URIS];", 'clip'),
    ('gui/clipboard.c', "strcmp(mime, CLIP_MIME_TEXT[1]) == 0) p = clip.payload[CLIP_FMT_TEXT];", "strcmp(mime, CLIP_MIME_TEXT[1]) == 0) {}", 'clip'),
    ('gui/clipboard.c', "    bool gnome = types & OFFER_GNOME;", "    bool gnome = false;", 'clip'),
    ('gui/clipboard.c', "    if (!(types & (OFFER_UTF8 | OFFER_TEXT))) {", "    if (true) {", 'clip'),
    # Keys through the compositor (keys, in the test sway with vkbd, 2026-10-28): the mappings no
    # other script reaches, and the scripted window's door for them.
    ('gui/input.c', "    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_F) return UK_SEARCH;\n", "", 'keys'),
    ('gui/input.c', "    if (m == GLFW_MOD_CONTROL && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) return UK_CONTAINING;", "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_KP_ENTER) return UK_CONTAINING;", 'keys'),
    ('gui/input.c', "    if (m == GLFW_MOD_ALT && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) return UK_TERMINAL;", "    if (m == GLFW_MOD_ALT && key == GLFW_KEY_KP_ENTER) return UK_TERMINAL;", 'keys'),
    ('gui/input.c', "    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_RIGHT) return UK_SEL_WORD_RIGHT;\n", "", 'keys'),
    ('gui/input.c', "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_BACKSPACE) return UK_WORD_BACKSPACE;\n", "", 'keys'),
    ('gui/input.c', "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_DELETE) return UK_WORD_DELETE;\n", "", 'keys'),
    (G, "                memcpy(in.ev, real.ev, sizeof *real.ev * (size_t)real.nev);\n                in.nev = real.nev;", "                memcpy(in.ev, real.ev, sizeof *real.ev * (size_t)real.nev);", 'keys'),
    # The views in gui/folder.c (2026-10-28): listing, watching, Up, what changes prune, the table.
    ('gui/folder.c', '    return strcmp(dir, TRASH_URI) == 0 ? UI_VIEW_TRASH : is_search(dir)', '    return is_search(dir)', 'f6'),
    ('gui/folder.c', 'is_search(dir) ? UI_VIEW_SEARCH : UI_VIEW_FOLDER;', 'UI_VIEW_FOLDER;', 'f9'),
    ('gui/folder.c', '    case UI_VIEW_TRASH: trash_state(); return', '    case UI_VIEW_TRASH: return', 'f6'),
    ('gui/folder.c', '    case UI_VIEW_TRASH: trash_state(); return', '    case UI_VIEW_TRASH: trash_scan(); return', 'f6'),
    # Unmounted drives, mounted by a click (2026-10-30). Not run: the dimmed label (drawn only; the
    # shot usb-unmounted.png shows it), and the guard's NULL folders (no test runs without its own).
    ('gui/watch.c', "        if (e->wd == watch.udev_wd && e->len && e->name[0] == 'b') *mounts = true;", '', 'usb'),
    ('gui/watch.c', "        if (e->wd == watch.udev_wd && e->len && e->name[0] == 'b') *mounts = true;", "        if (e->wd == watch.udev_wd && e->len && e->name[0] == 'c') *mounts = true;", 'usb'),
    ('gui/folder.c', ' || app.places.p[i].kind == PL_UNMOUNTED) return NULL;', ') return NULL;', 'usb'),
    ('gui/actions.c', '            mount_start(&app.places.p[a.index]);   // a drive not mounted yet', '            {}   // a drive not mounted yet', 'usb'),
    ('gui/input.c', '    if (it == UM_MOUNT_IT && ctx_pl && ctx_pl->kind == PL_UNMOUNTED) mount_start(ctx_pl);', '', 'usb'),
    ('gui/input.c', '        u->ctx_unmounted = pl->kind == PL_UNMOUNTED;\n', '', 'usb'),
    ('gui/eject.c', '    if (ej.op == DRIVE_MOUNT) mount_argv(ej.dev, argv);\n    else if', '    if (false) mount_argv(ej.dev, argv);\n    else if', 'usb'),
    # Unmount (2026-10-30)
    ('gui/eject.c', '    else if (ej.op == DRIVE_UNMOUNT) unmount_argv(ej.dev, argv);\n', '', 'usb'),
    ('gui/eject.c', '        else set_status(false, "%s is unmounted", ej.label);', '', 'usb'),
    ('gui/eject.c', '        if (r == EJECT_FAILED) set_status(true, "Can\'t unmount %s: %s"', '        if (false) set_status(true, "Can\'t unmount %s: %s"', 'usb'),
    ('gui/input.c', '        if (ctx_pl && place_ejectable(ctx_pl)) (it == UM_EJECT_IT ? eject_start : unmount_start)(ctx_pl);', '        if (ctx_pl && place_ejectable(ctx_pl)) eject_start(ctx_pl);', 'usb'),
    ('gui/folder.c', '        if (path_within(here_dir(), was[k])) app.drive_gone = true;\n', '', 'usb'),
    ('gui/folder.c', '        drive_gone_tabs(was[k]);\n', '', 'usb'),
    ('gui/folder.c', '        if (!t->used || !path_within(dir, mp)) continue;', '        if (!t->used) continue;', 'usb'),
    # F10 step 1: ofx.conf (2026-10-30). Not run: conf_shown's ~ (drawn into the status, which a
    # script matches only as statusbad), and prefs_open's spawn_open fallback (a script always has
    # the fake nvim recipe).
    ('gui/main.c', '    conf_apply();   // (F10', '    // (F10', 'f10'),
    ('gui/main.c', '        if (atomic_exchange(&watch.conf_ready, false)) conf_apply(), reload(), dirty = true;', '        if (atomic_exchange(&watch.conf_ready, false)) dirty = true;', 'f10'),
    ('gui/main.c', '        if (atomic_exchange(&watch.conf_ready, false)) conf_apply(), reload(), dirty = true;', '        if (atomic_exchange(&watch.conf_ready, false)) conf_apply(), dirty = true;', 'f10'),
    ('gui/watch.c', '        if (e->wd == atomic_load(&watch.conf_wd) && e->len && strcmp(e->name, "ofx.conf") == 0) *conf = true;', '', 'f10'),
    ('gui/watch.c', '    watch_bookmarks();\n    watch_conf();', '    watch_bookmarks();', 'f10'),
    ('gui/watch.c', '    watch_file_dir(&watch.conf_wd, conf_path(file, sizeof file) ? file : NULL);', '', 'f10'),
    ('gui/watch.c', '    watch_file_dir(&watch.bm_wd, bookmarks_path(file, sizeof file));', '', 'f75'),
    ('gui/watch.c', '        if (e->wd == atomic_load(&watch.bm_wd) && (e->mask & IN_IGNORED)) atomic_store(&watch.bm_wd, -1);', '', 'f75'),
    ('gui/prefs.c', '    watch_conf();   // (its folder may be new)\n', '', 'f10'),
    ('gui/prefs.c', '    if (!conf_create(path, &err)) {', '    if (false) {', 'f10'),
    ('gui/prefs.c', '    if (conf_path(path, sizeof path) && !conf_create(path, &err)) set_status', '    if (false) set_status', 'f10'),
    ('gui/main.c', '    conf_start();   // (F10', '    // (F10', 'f10'),
    ('gui/watch.c', '        if (e->wd == atomic_load(&watch.conf_wd) && (e->mask & IN_IGNORED)) atomic_store(&watch.conf_wd, -1);', '', 'f10'),
    ('gui/folder.c', '        if (app.cwd[0]) app.status[0] = 0;', '        app.status[0] = 0;', 'f10'),
    ('gui/prefs.c', '    thumbs.off = !c.previews, thumbs.src_max = c.preview_max;', '    thumbs.src_max = c.preview_max;', 'f10'),
    ('gui/prefs.c', '    thumbs.off = !c.previews, thumbs.src_max = c.preview_max;', '    thumbs.off = !c.previews, thumbs.src_max = CONF_PREVIEW_MAX;', 'f10'),
    ('gui/prefs.c', '    if (previews) thumbs_reset();', '', 'f10'),
    ('gui/prefs.c', '    if (c.bad) {', '    if (false) {', 'f10'),
    ('gui/prefs.c', '    int n = e ? editor_argv(e, path, 1, 1, buf', '    int n = e ? editor_argv(e, path, 2, 1, buf', 'f10'),
    ('gui/thumbs.c', ' || thumbs.full || thumbs.off) return NULL;', ' || thumbs.full) return NULL;', 'f10'),
    # Not a mutant: the request's src_max as THUMB_SRC_MAX (its make flag has the limit already; the
    # worker's own guards a file grown between the listing and the read).
    ('gui/folder.c', '                          .skips = &app.conf.skip };', '                          .skips = &SEARCH_SKIP_DEFAULT };', 'f10'),
    ('gui/search.c', '    *s = search.s, *names = search.skips, *done = search.done;', '    *s = search.s, *done = search.done;', 'f10'),
    ('gui/folder.c', 'search_ask((SearchQuery){ .root = "", .skips = &app.conf.skip })', 'search_ask((SearchQuery){ .root = "" })', 'f4'),
    # F11 step 1 (2026-11-01)
    ('gui/folder.c', '    if (!same && app.have_list && fold_view_of(app.ui.view)) remember_cursor(app.cwd, sel_name());\n', '', 'f11'),
    ('gui/folder.c', '        remember_apply(dir);\n', '', 'f11'),
    ('gui/folder.c', '        if (!select.len && fallback == 0 && kept) select = str_from_c(kept);', '', 'f11'),
    ('gui/folder.c', '        app.ui.grid = grid, app.ui.zoom = zoom, app.ui.sort = sort;\n        if (!app.have_list)', '        if (!app.have_list)', 'f11'),
    ('gui/remember.c', '    return v == UI_VIEW_FOLDER || v == UI_VIEW_TRASH;', '    return v != UI_VIEW_TRASH;', 'f11'),
    ('gui/remember.c', '    bool grid = f && f->view >= 0 ? f->view == 1 : app.conf.icons;', '    bool grid = f && f->view >= 0 ? f->view == 1 : false;', 'f11'),
    ('gui/remember.c', '    int zoom = f && f->zoom > 0 ? f->zoom : app.conf.zoom;', '    int zoom = app.conf.zoom;', 'f11'),
    ('gui/remember.c', '    if (grid != app.ui.grid || zoom != ui_zoom_px(&app.ui)) app.icons_reset_armed = true, app.zoomed = true;', '', 'f63'),
    ('gui/remember.c', '    if (mask & FP_VIEW) f->view = app.ui.grid == app.conf.icons ? -1 : app.ui.grid;', '    if (mask & FP_VIEW) f->view = app.ui.grid;', 'f62'),
    ('gui/remember.c', '    if (mask & FP_SORT) f->sort = (int)app.ui.sort.key == app.conf.sort ? -1 : (int)app.ui.sort.key;', '    if (mask & FP_SORT) f->sort = (int)app.ui.sort.key;', 'f11'),
    ('gui/remember.c', '    if (mask & FP_REV) f->rev = app.ui.sort.reverse == app.conf.sort_rev ? -1 : app.ui.sort.reverse;', '    if (mask & FP_REV) f->rev = app.ui.sort.reverse;', 'f11'),
    ('gui/remember.c', '    if (rem.a[rem.cur].used > (isize)sizeof rem.mem[0] / 2) remember_write();', '', 'f11'),
    ('gui/remember.c', '    if (st.folder_defaults[0]) {', '    if (true) {', 'f11'),
    ('gui/remember.c', '        mask |= strcmp(sort, FOLDER_SORTS[app.conf.sort]) ? FP_SORT : 0;', '', 'f11'),
    ('gui/remember.c', '    int n = mask ? folders_wipe(&rem.t, mask) : 0;', '    int n = 0;', 'f11'),
    ('gui/remember.c', '    if (!state_put(&scratch, path, "folderdefaults", now, &err))', '    if (false)', 'f11'),
    ('gui/main.c', '    remember_defaults();   // (F11', '    // (F11', 'f11'),
    ('gui/main.c', '    if (fold_view_of(app.ui.view) && app.have_list) remember_cursor(app.cwd, sel_name());   // (F11)', '', 'f11'),
    ('gui/main.c', '    if (rem.dirty) remember_write();\n    if (app.windows[0])', '    if (app.windows[0])', 'f11'),
    ('gui/actions.c', '        remember_view(a.kind == UA_ZOOM ? FP_ZOOM : FP_VIEW);', '        remember_view(FP_VIEW);', 'f11'),
    ('gui/actions.c', '    case UA_SORT: resort(), remember_view(FP_SORT | FP_REV); break;', '    case UA_SORT: resort(); break;', 'f11'),
    ('gui/actions.c', '        if (state_path(sp, sizeof sp) && !state_put(&scratch, sp, "hidden", app.show_hidden ? "on" : "off", &err))', '        if (false)', 'f11'),
    ('gui/prefs.c', '        remember_defaults();\n        if (fold_view_of', '        if (fold_view_of', 'f11'),
    ('gui/prefs.c', '        if (fold_view_of(app.ui.view)) remember_apply(app.cwd);', '', 'f11'),
    # F11 step 2: Reopen Closed Tab, the tree's open folders (2026-11-01)
    ('gui/actions.c', '        tree_remember();   // (F11)\n', '', 'f11'),
    ('gui/main.c', '        tree_open_load(&tree, state_value(&scratch, state_file, "tree"));', '', 'f11'),
    ('gui/actions.c', '"%s", shown ? app.cwd : tabs[tabbar.slot[at]].dir);', '"%s", tabs[tabbar.slot[at]].dir);', 'f11'),
    ('gui/actions.c', '        if (closed_tabs.n > 0) closed_tabs.n--, tab_open(', '        if (closed_tabs.n > 0) tab_open(', 'f11'),
    ('gui/frame.c', '    u->can_reopen = closed_tabs.n > 0;   // (F11)', '    u->can_reopen = true;', 'f11'),
    ('gui/input.c', '    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_T) return UK_TAB_REOPEN;   // (F11)\n', '', 'keys'),
    # F11 step 3: the tabs across a restart (2026-11-01)
    ('gui/restore.c', '    if (!app.conf.restore_tabs || (others > 0 && !closing_all)) {', '    if (!app.conf.restore_tabs || others > 0) {', 'f11'),
    ('gui/restore.c', '    if (!app.conf.restore_tabs || (others > 0 && !closing_all)) {', '    if ((others > 0 && !closing_all)) {', 'f11'),
    ('gui/restore.c', ' dir[n++] = i == tabbar.cur ? app.cwd : tabs[tabbar.slot[i]].dir;', ' dir[n++] = tabs[tabbar.slot[i]].dir;', 'f11'),
    ('gui/restore.c', 'session_line((long)getpid(), (i64)time(NULL), tabbar.cur, live', 'session_line((long)getpid(), (i64)time(NULL), 0, live', 'f11'),
    ('gui/restore.c', '    if (plain) go_to(w.dir[0], STR_EMPTY, 0);', '    go_to(w.dir[0], STR_EMPTY, 0);', 'f11'),
    ('gui/restore.c', '    for (int i = plain ? 1 : 0; i < w.n; i++)', '    for (int i = 1; i < w.n; i++)', 'f11'),
    ('gui/restore.c', '    if (plain) tab_show(w.shown);', '', 'f11'),
    ('gui/restore.c', '    app.status[0] = 0;   // (not', '    // (not', 'f11'),
    ('gui/restore.c', '    if (!id && restore.n > 0) app.ui.restore_n = restore.n;', '    if (restore.n > 0) app.ui.restore_n = restore.n;', 'f11'),
    ('gui/restore.c', '        const char *cmd[] = { app.exe, "--restore", id };', '        const char *cmd[] = { app.exe, id };', 'f11'),
    ('gui/restore.c', ': state_path(path, sizeof path) && !session_forget(', ': false && !session_forget(', 'f11'),
    ('gui/main.c', '    closing_all = true;\n    if (quit && jobs.nrun', '    if (quit && jobs.nrun', 'f11'),
    ('gui/main.c', '    restore_save();   // (F11 step 3', '    // (F11 step 3', 'f11'),
    ('gui/main.c', '    if (atomic_load(&quit_signal)) closing_all = true;', '', 'f11'),
    # F11 step 3b: the line kept as the tabs change (2026-11-02)
    ('gui/restore.c', '    if (tail && tail != restore.last) restore.due = now + RESTORE_WRITE_MS;', '    if (tail && tail != restore.last) restore.due = now;', 'f11'),
    ('gui/restore.c', '    if (tail && tail != restore.last) restore.due = now + RESTORE_WRITE_MS;', '', 'f11'),
    ('gui/restore.c', '    if (restore.due && now >= restore.due) restore_write();', '', 'f11'),
    ('gui/restore.c', '    restore.last = tail, restore.wrote = true;', '    restore.wrote = true;', 'f11'),
    ('gui/restore.c', '        if (restore.wrote && !session_put(&scratch, path, (long)getpid(), me, NULL, &err))', '        if (false)', 'f11'),
    ('gui/restore.c', '    bool others = !id && app.windows[0] && win_signal_others(app.windows, getpid(), 0) > 0;', '    bool others = false;', 'f11'),
    ('gui/restore.c', '    restore_write();   // (this window', '    // (this window', 'f11'),
    ('gui/restore.c', '    restore.have_me = session_boot(restore.me.boot, sizeof restore.me.boot) && start >= 0;', '    restore.have_me = false;', 'f11'),
    ('gui/main.c', '        if (restore_tick(now_ms()) != was_due && restore.due) timer_at(wake_due());', '        restore_tick(now_ms());', 'f11'),
    ('gui/prefs.c', '    if (off) restore_off();\n', '', 'f11'),
    ('gui/restore.c', '    for (int i = 0; i < tabbar.n && i < SESSION_TABS; i++) cap += 3 * strlen(', '    cap += (size_t)SESSION_TABS * PATH_MAX * 3; for (int i = 0; i < 0; i++) cap += 3 * strlen(', 'f11'),
    ('gui/main.c', "    restore_start(restore_id, !selecting && (!start || (h && realpath(h, home_real) && strcmp(home_real, dir) == 0)));", "    restore_start(restore_id, !selecting && !start);", 'f11'),
    ('gui/main.c', '            restore_id = strtol(argv[++i], NULL, 10);', '            restore_id = 0, i++;', 'f11'),
    ('gui/actions.c', '    case UA_RESTORE_WINDOWS: restore_others(a.all); break;   // (F11 step 3)', '    case UA_RESTORE_WINDOWS: break;', 'f11'),
    ('gui/files.c', '    if (e->mtime) ui_when(buf, cap, e->mtime, now, app.conf.date, app.conf.date_pattern);', '    if (e->mtime) ui_when(buf, cap, e->mtime, now, DATE_SHORT, "");', 'f10'),
    ('gui/files.c', '    if (app.conf.date == DATE_SHORT) return DATE_W;\n    static', '    return DATE_W;\n    static', 'f10'),
    ('gui/files.c', '    for (int k = 0; k < COUNT(WIDE); k++) {', '    for (int k = 0; k < 1; k++) {', 'f10'),
    ('gui/actions.c', '    case UA_PREFERENCES: prefs_open(); break;\n', '', 'f10'),
    ('gui/main.c', '        if (atomic_exchange(&watch.mounts_ready, false)) places_reload(), trash_state(), dirty = true;\n        if (app.drive_gone) {', '        if (atomic_exchange(&watch.list_ready, false)) reload(), dirty = true;\n        if (atomic_exchange(&watch.mounts_ready, false)) places_reload(), trash_state(), dirty = true;\n        if (app.drive_gone) {', 'usb'),
    ('gui/eject.c', '        else if (go_to(at, STR_EMPTY, 0)) set_status(false, "Mounted %s", ej.label);', '        else set_status(false, "Mounted %s", ej.label);', 'usb'),
    ('gui/eject.c', '        if (!mounted) set_status(true, "Can\'t mount %s: %s"', '        if (false) set_status(true, "Can\'t mount %s: %s"', 'usb'),
    ('gui/script.c', ' : p->kind == PL_UNMOUNTED ? "~" : ""', ' : ""', 'usb'),
    # Not run (2026-10-30): drive_start's ops_program_allowed (a script can't change the window's
    # PATH to a udisksctl outside the root; the unit test has it), and the mount's longer limit.
    ('gui/folder.c', '    if (v == UI_VIEW_TRASH && trash_home(tinfo, sizeof tinfo - 8)) strcat(tinfo, "/info");\n', '', 'f6'),
    ('gui/folder.c', '    return app.ui.view == UI_VIEW_SEARCH || (app.ui.view == UI_VIEW_FOLDER && strcmp(app.cwd, "/") != 0);', '    return app.ui.view == UI_VIEW_SEARCH || strcmp(app.cwd, "/") != 0;', 'f6'),
    ('gui/folder.c', '    return app.ui.view == UI_VIEW_SEARCH || (app.ui.view == UI_VIEW_FOLDER && strcmp(app.cwd, "/") != 0);', '    return app.ui.view == UI_VIEW_FOLDER && strcmp(app.cwd, "/") != 0;', 'f9'),
    ('gui/folder.c', '    if (app.ui.view == UI_VIEW_SEARCH) search_prune(app.search_root);\n}', '}', 'f9'),
    ('gui/folder.c', '    [UI_VIEW_TRASH] = { "Deleted", TRASH_BAR_H },', '    [UI_VIEW_TRASH] = { "Modified", TRASH_BAR_H },', 'f6'),
    ('gui/folder.c', '    [UI_VIEW_SEARCH] = { "Modified", SEARCH_BAR_H },', '    [UI_VIEW_SEARCH] = { "Modified", 0 },', 'f9'),
    ('gui/folder.c', '    if (app.ui.view == UI_VIEW_SEARCH && view != UI_VIEW_SEARCH) search_ask(', '    if (false) search_ask(', 'f9'),
    # (not can_write's view == UI_VIEW_FOLDER: access() fails on "trash:" and a search's address anyway)
    # (not tree_refresh's view != UI_VIEW_SEARCH: tree_follow skips an address without a '/' first,
    # and the row it keeps by index is the one it keeps by path; 2026-10-29)
    ('gui/folder.c', '    if (!view_can_up()) return;\n', '', 'f6'),
    ('gui/frame.c', '    u->can_bookmark = u->view == UI_VIEW_FOLDER && !bmarks.full', '    u->can_bookmark = !bmarks.full', 'f9'),
    ('gui/actions.c', '    view_changed();   // (a folder renamed: the results below it are gone)\n', '', 'f9'),
    ('gui/files.c', ' - view_bar_h() -', ' -', 'f6'),
    ('gui/folder.c', 'view == UI_VIEW_SEARCH ? app.search_root : dir);   // (not here_dir', 'here_dir());   // (not here_dir', 'f64'),
    # F9 step 3: inside files (2026-10-29). Not run: search_copy's copy of a hit (as of the names
    # beside it, it only matters when a new search resets the walk's arena under a listing still
    # shown); the tab's name (not in the state line); the matchers freed at quit; the red frame of
    # a bad expression inside (drawn only; insidebad is the matcher's); Ctrl+R's text inside (the
    # relist after it asks again from the address).
    ('gui/search.c', '        if (inside.len) w.line = search_line_cb, w.buf = search_buf;', '', 'f9'),
    ('gui/search.c', '    if (hit && !(search_res[search_wrote].hit = search_hit_copy(&search_arena, hit))) return false;', '', 'f9'),
    ('gui/search.c', '    return ui_match_span(&search_im, line, from, to);', '    return ui_match_span(&search_m, line, from, to);', 'f9'),
    ('gui/search.c', ' && str_eq((Str){ search.inside, search.inside_len }, q.inside) &&', ' &&', 'f9'),
    ('gui/search.c', '                (!q.inside.len || search_opts_eq(search.inside_opts, q.io)) &&', '                true &&', 'f9'),
    ('gui/search.c', '    search.inside_len = search_text_put(search.inside, q.inside);', '', 'f9'),
    ('gui/search.c', '    search.opts = q.o, search.inside_opts = q.io, search.hidden = q.hidden;', '    search.opts = q.o, search.hidden = q.hidden;', 'f9'),
    ('gui/search.c', '    search.done = (q.text.len == 0 && q.inside.len == 0) || !search.started;', '    search.done = q.text.len == 0 || !search.started;', 'f9'),
    ('gui/folder.c', ', .io = app.ui.inside_opts, .hidden', ', .hidden', 'f9'),
    ('gui/folder.c', '        ui_line_set(ui_inside_line(&app.ui), inside);\n', '', 'f9'),
    ('gui/folder.c', '        app.ui.search_inside = inside.len > 0;', '', 'f9'),
    ('gui/folder.c', '        if (text.len == 0 && inside.len == 0) app.ui.focus = UI_FOCUS_SEARCH;', '        if (text.len == 0) app.ui.focus = UI_FOCUS_SEARCH;', 'f9'),
    ('gui/folder.c', '    Str inside = app.ui.search_inside ? (Str){ app.ui.inside.b, app.ui.inside.len } : STR_EMPTY;', '    Str inside = (Str){ app.ui.inside.b, app.ui.inside.len };', 'f9'),
    ('gui/folder.c', '    return VIEWS[app.ui.view].bar_h + (app.ui.view == UI_VIEW_SEARCH && app.ui.search_inside ? SEARCH_ROW_H : 0);', '    return VIEWS[app.ui.view].bar_h;', 'f9'),
    ('gui/footer.c', '        if (u->search_inside) {\n            CLAY_AUTO_ID', '        if (false) {\n            CLAY_AUTO_ID', 'f9'),
    ('gui/footer.c', '    bool inside = app.ui.search_inside && app.ui.inside.len;', '    bool inside = false;', 'f9'),
    ('gui/footer.c', '        snprintf(buf, cap, app.ui.search_inside ? "Text to look for', '        snprintf(buf, cap, false ? "Text to look for', 'f9'),
    ('gui/files.c', '                    layout_hit(r, e->hit, room - (dir ? dw + 6 : 0));', '', 'f9'),
    ('gui/input.c', '    h.inside_btn = app.ui.view == UI_VIEW_SEARCH && in_box(CLAY_ID("search-inside"), x, y);', '', 'f9'),
    ('gui/input.c', '    [UI_FIELD_INSIDE] = { "inside-field", TOGGLE_INSIDE },', '    [UI_FIELD_INSIDE] = { "search-field", TOGGLE_INSIDE },', 'f9'),
    ('gui/input.c', '    [UI_FIELD_INSIDE] = { "inside-field", TOGGLE_INSIDE },', '    [UI_FIELD_INSIDE] = { "inside-field", TOGGLE_SEARCH },', 'f9'),
    ('gui/input.c', '    in->field = h.field, in->field_toggle = h.field_toggle, in->inside_btn = h.inside_btn;', '    in->field = h.field, in->field_toggle = h.field_toggle;', 'f9'),
    # F9 step 4: Open at Line (2026-10-29). Not run: the menu entry's label and the width it adds to
    # the menu (drawn only; the shot open-line-menu.png shows them), and editor_now's once-a-second
    # cache for that label (a run can't tell a read from a cached one).
    ('gui/actions.c', '    case UA_OPEN_LINE: open_at_line(a.index); break;\n', '', 'f9'),
    ('gui/actions.c', '    if (!en || !en->hit) {', '    if (!en) {', 'f9'),
    ('gui/actions.c', '    if (!e) {\n        set_status(true, "No Open at Line recipe', '    if (false) {\n        set_status(true, "No Open at Line recipe', 'f9'),
    ('gui/actions.c', '    else if (!editor_open(e, argv, n, &err)) set_status(true, "%s", err.msg);', '    else if (!editor_open(e, argv, n, &err)) {}', 'f9'),
    ('gui/actions.c', '    else if (editors.user.bad) set_status(true, "%s: line %d isn', '    else if (false) set_status(true, "%s: line %d isn', 'f9'),
    ('gui/actions.c', '    int n = editor_argv(e, path, en->hit->line, en->hit->col, buf, sizeof buf, argv, COUNT(argv));', '    int n = editor_argv(e, path, en->hit->line, 1, buf, sizeof buf, argv, COUNT(argv));', 'f9'),
    ('gui/actions.c', '    editors_add(&editors.a, editors_path(path, sizeof path) ? read_file(&editors.a, path) : STR_EMPTY, &editors.user);', '    editors_add(&editors.a, STR_EMPTY, &editors.user);', 'f9'),
    # F10 step 2: ofx.conf's editor lines, before the file's; the note said once
    ('gui/actions.c', '    for (int k = 0; k < app.conf.neditor; k++) editors_add(&editors.a, str_from_c(app.conf.editor[k]), &editors.user);\n', '', 'f9'),
    ('gui/actions.c', '    editors.user = (Editors){ 0 };\n    for', '    for', 'f9'),
    ('gui/actions.c', '    else if (editors.old && !editors.told) editors.told = true, set_status', '    else if (editors.old) editors.told = true, set_status', 'f9'),
    ('gui/actions.c', '    else if (editors.old && !editors.told) editors.told = true, set_status', '    else if (false) editors.told = true, set_status', 'f9'),
    ('gui/actions.c', '    editors.old = editors.user.n - mine;', '    editors.old = 0;', 'f9'),
    ('gui/frame.c', '    u->sel_hit = e && e->hit;   // (F9 step 4: Open at Line)', '', 'f9'),
    ('gui/input.c', '    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) return UK_OPEN_LINE;', '', 'keys'),
    ('gui/folder.c', '        app.ui.search_was_inside = false;   // (another search', '        // (another search', 'f9'),
    # Drives at the top (2026-10-29): an open menu's entry over a menu title is the entry's
    ('gui/input.c', '        for (int m = 0; m < UI_MENUS && h.menu_title < 0 && h.menu_item < 0; m++)', '        for (int m = 0; m < UI_MENUS && h.menu_title < 0; m++)', 'f8'),
    # Tab on the folder shown's row (2026-10-29)
    ('gui/frame.c', '    u->side_here = u->side_mode == SIDE_PLACES ? app.place_cur : ', '    u->side_here = u->side_mode == SIDE_PLACES ? -1 : ', 'f2'),
    ('gui/frame.c', ' : u->side_mode == SIDE_TREE ? tree_find_near(&tree, str_from_c(app.cwd)) : -1;', ' : -1;', 'f6'),
    ('gui/frame.c', ' : u->side_mode == SIDE_TREE ? tree_find_near(&tree, str_from_c(app.cwd)) : -1;', ' : u->side_mode == SIDE_TREE ? tree_find(&tree, str_from_c(app.cwd)) : -1;', 'f6'),
    # The Trash place's icon full or empty (2026-10-30). Not run: trash_state at the start (each
    # script starts with an empty trash), and after a job (the trash folder's watch sees what a
    # job does there as well).
    ('gui/icons.c', 'place_icon_names(&app.places.p[i], &app.user_dirs, app.trash_full, names);', 'place_icon_names(&app.places.p[i], &app.user_dirs, false, names);', 'f6'),
    ('gui/main.c', '        if (atomic_exchange(&watch.trash_ready, false) && trash_state()) dirty = true;\n', '', 'f6'),
    ('gui/main.c', 'places_reload(), trash_state(), dirty = true;', 'places_reload(), dirty = true;', 'f6'),
    ('gui/watch.c', '        if (!(e->mask & IN_IGNORED) && watch_is_trash(e->wd)) *trash = true;\n', '', 'f6'),
    ('gui/watch.c', '    if (old >= 0 && !watch_fixed(old) && !watch_is_trash(old)) inotify_rm_watch', '    if (old >= 0 && !watch_fixed(old)) inotify_rm_watch', 'f6'),
    ('gui/watch.c', '    if (old >= 0 && !watch_fixed(old) && !watch_is_trash(old)) inotify_rm_watch', '    if (old >= 0 && !watch_is_trash(old)) inotify_rm_watch', 'f10'),
    ('gui/folder.c', '    for (int k = 0; read && k < nwas; k++) {', '    for (int k = 0; k < nwas; k++) {', 'usb'),
    ('gui/watch.c', '        if (old[i] > 0 && !kept && old[i] != atomic_load(&watch.dir_wd) && !watch_fixed(old[i])) inotify_rm_watch(watch.fd, old[i]);', '        if (old[i] > 0 && !kept && !watch_fixed(old[i])) inotify_rm_watch(watch.fd, old[i]);', 'f6'),
    ('gui/folder.c', '    watch_trashes(&trashd);   // armed before', '    // armed before', 'f6'),
    ('gui/watch.c', ' | IN_DELETE_SELF | IN_MOVE_SELF |\n                                                    IN_ONLYDIR | IN_MASK_ADD);', ' | IN_DELETE_SELF | IN_MOVE_SELF |\n                                                    IN_ONLYDIR);', 'f6'),
    ('gui/watch.c', '        if (old[i] > 0 && !kept && old[i] != atomic_load(&watch.dir_wd) && !watch_fixed(old[i])) inotify_rm_watch(watch.fd, old[i]);', '        if (old[i] > 0 && old[i] != atomic_load(&watch.dir_wd) && !watch_fixed(old[i])) inotify_rm_watch(watch.fd, old[i]);', 'f6'),
    ('gui/actions.c', '    ui_mark_clear(&app.ui);\n    trash_state();\n    reload();', '    ui_mark_clear(&app.ui);\n    reload();', 'f6'),
]
import concurrent.futures, queue, threading, time

args = sys.argv[1:]
jobs = 6   # the time levels off there: 46 mutants on a quiet machine took 414-451 s at 6, 411-419 at 8, 442 at 10, 477 at 12, CPU 43-62% (2026-10-26)
if '--jobs' in args:
    jobs = int(args[args.index('--jobs') + 1])
    del args[args.index('--jobs'):args.index('--jobs') + 2]
if len(args) not in (1, 2) or any(a.startswith('-') for a in args):
    sys.exit(__doc__.strip())   # an unknown flag isn't taken for the scratch folder
scratch = args[0]
only = [int(x) for x in args[1].split(',')] if len(args) > 1 else None
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# OFX_GUI_TAGS: another tags file, for a run beside one that will write this one.
tags_path = os.environ.get('OFX_GUI_TAGS') or os.path.join(root, 'tools', 'mutation', 'gui-tags.txt')
# The run's times, and each mutant's, are printed. Starting the slowest first was measured on
# 2026-10-26 and left out: 290 mutants took 1360 s against a floor of 1349 (their worker time over
# 6), so the order can't gain; there's no long tail to trim.
t_start = time.monotonic()

# run.sh's groups, in its order: a mutant runs them one at a time and stops at the first that
# kills it (Michael, 2026-10-25).
GROUPS = ['browse', 'big', 'gone', 'watchgap', 'f2', 'f3', 'f4', 'f5', 'f52', 'f53', 'f54', 'f6', 'usb', 'f11', 'f10', 'f62', 'f63', 'f64', 'f7',
          'f72', 'f73', 'f74', 'f75', 'f8', 'f9', 'clip', 'keys', 'hang', 'guard3', 'guard']
run_text = open(os.path.join(root, 'tests/gui/run.sh')).read()
assert re.findall(r'want ([a-z0-9]+)', run_text) == GROUPS, 'GROUPS in gui.py no longer match tests/gui/run.sh'

# A mutant is (file, old, new) or (file, old, new, hint): the hint names the group(s) likeliest to
# kill it, tried first when it has no tags. Its key is the first three only, so a hint added later
# keeps its tags.
for m in MUTANTS:
    assert len(m) in (3, 4) and (len(m) == 3 or all(g in GROUPS for g in m[3].split())), m

# Each worker its own copy of the repo and its own build; the scripts run from the copy, so the
# repo can be edited meanwhile.
# vkbd (the test sway's keyboard, run-script.sh puts it on PATH) is built once here; no mutant touches it.
subprocess.run(['make', '-s', '-C', root, 'build/vkbd'], check=True)
def make_work(w):
    work = os.path.join(scratch, f'gui-mut-src-{w}')
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(root, work, ignore=shutil.ignore_patterns('.git', 'build'))
    os.makedirs(os.path.join(work, 'build'))
    os.symlink(os.path.join(root, 'build', 'raylib'), os.path.join(work, 'build', 'raylib'))
    os.symlink(os.path.join(root, 'build', 'vkbd'), os.path.join(work, 'build', 'vkbd'))
    return work
works = [make_work(w) for w in range(jobs)]
for f, old, new, *_ in MUTANTS:
    n = open(os.path.join(works[0], f)).read().count(old)
    assert n == 1, (f, old, n)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import rmtree

# A mutant's tag key: its file and texts, so a mutant edited is a new one, untagged.
def key(m):
    return hashlib.sha1('\0'.join(m[:3]).encode()).hexdigest()[:12]

# The group of a script that failed: run.sh prints "== <script>: FAILED" or "<group>: FAILED".
GROUP_OF = {'f6b': 'f6', 'f62b': 'f62', 'f62c': 'f62', 'f63b': 'f63', 'f63c': 'f63', 'f64b': 'f64', 'f64c': 'f64', 'f64d': 'f64', 'f7b': 'f7', 'f73b': 'f73', 'f73c': 'f73', 'f73d': 'f73', 'f75b': 'f75', 'f8b': 'f8', 'f8c': 'f8', 'f8d': 'f8', 'f8e': 'f8', 'f9b': 'f9', 'f10b': 'f10', 'f10c': 'f10', 'f10d': 'f10', 'f11b': 'f11', 'f11c': 'f11', 'f11d': 'f11', 'f11e': 'f11', 'f11f': 'f11', 'f11g': 'f11', 'f11h': 'f11', 'f11i': 'f11', 'f11j': 'f11', 'f11k': 'f11', 'f11l': 'f11', 'f11m': 'f11', 'f11n': 'f11', 'f11o': 'f11', 'f11p': 'f11', 'f11q': 'f11'}

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

# A script that failed before its window launched (run-script prints RUN_LAUNCHED as it does: a
# compositor that didn't start, the socket checks): it never ran, so that failure says nothing about
# the build. (2026-10-29: five mutants counted as killed by keys, with no expectation failed, that
# no group kills; a start timed out under load is the likely cause, as the stale wayland-N sockets
# of 2026-10-28. reviewer-2's marker, rather than one message to look for.)
RUN_LAUNCHED = 'run-script: the window launched'

def unstarted(r):
    if r.returncode == 0:
        return False
    heads = [k for k, l in enumerate(r.stdout.splitlines()) if re.fullmatch(r'== [a-z0-9]+', l)]
    last = r.stdout.splitlines()[heads[-1]:] if heads else r.stdout.splitlines()
    return RUN_LAUNCHED not in last

# A mutant's run stops at its first failure (OFX_GUI_FAIL_FAST, run.sh and script.c), the base's
# never: it must pass everything. A group whose compositor didn't start runs again, three times in
# all; unstarted(r) says when it never did.
def run_groups(work, base, groups, fast=True, binary='build/ofx-mut'):
    env = dict(os.environ, OFX_GUI_FAIL_FAST='1') if fast else {k: v for k, v in os.environ.items() if k != 'OFX_GUI_FAIL_FAST'}
    for _ in range(3):
        r = subprocess.run([os.path.join(work, 'tests/gui/run.sh'), base, os.path.join(work, binary)] + groups,
                           capture_output=True, text=True, env=env)
        if not unstarted(r):
            break
    return r

# The unmutated build in each worker, kept as build/ofx-base for the base's groups (a mutant's build
# replaces build/ofx-mut).
def build_base(w):
    b = subprocess.run(['make', '-s', 'build/ofx-mut'], cwd=works[w], capture_output=True, text=True)
    if b.returncode == 0:
        shutil.copy2(os.path.join(works[w], 'build/ofx-mut'), os.path.join(works[w], 'build/ofx-base'))
    return b
with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
    builds = list(ex.map(build_base, range(jobs)))
if any(b.returncode != 0 for b in builds):
    print('the unmutated build fails:', next(b for b in builds if b.returncode != 0).stderr[-600:])
    sys.exit(2)

# The base: the unmutated build on the whole suite, a group a task, the first tasks in the same pool
# as the mutants, so it runs under the load they run with while they start (Michael, 2026-10-26:
# once, not in every worker; it was 30% of a run). If any group fails, every mutant would count as
# killed (as they did while f6 couldn't start, from fe01f7c to e8c2c57), and a script that fails
# only under load would kill mutants it doesn't see: then no mutant starts after it, and every
# result of the run is thrown away.
base_failed = []
t_base = None
def base_group(g):
    global t_base
    work = free_works.get()
    try:
        d = os.path.join(scratch, f'gm-base-{g}')
        r = run_groups(work, d, [g], fast=False, binary='build/ofx-base')
        rmtree(d)
        with lock:
            if r.returncode != 0:
                log = os.path.join(scratch, f'base-{g}.log')   # the whole of it: why a script failed under load
                open(log, 'w').write(r.stdout + r.stderr)
                base_failed.append(g)
                print(f'base: {g} FAILED unmutated: ' + '; '.join(l for l in r.stdout.splitlines() if 'FAILED' in l or (l.startswith('script:') and not l.startswith('script: 0 of')))[:300] + f'  (all of it: {log})', flush=True)
            base_left.remove(g)
            if not base_left and not base_failed:
                t_base = time.monotonic()
                print(f'base: tests/gui/run.sh passes unmutated, under the mutants\' load ({t_base - t_start:.0f} s)', flush=True)
    finally:
        free_works.put(work)

MIN_FREE = 2 << 30   # bytes left in the scratch dir's file system before a mutant starts
free_works = queue.Queue()
for w in works:
    free_works.put(w)
lock = threading.Lock()
survivors, unbuilt, retagged, stopped, unrun = [], [], [], False, []
base_left = list(GROUPS)

# One mutant in a free worker: its groups one run.sh at a time, until one kills it: its tags, its
# hint, then the rest of the groups. The group that kills it becomes its tag.
def one(i):
    global retagged, stopped
    m = MUTANTS[i]
    f, old, new = m[:3]
    k = key(m)
    with lock:
        if base_failed:
            return
        if stopped or shutil.disk_usage(scratch).free < MIN_FREE:
            if not stopped:
                print(f'stopped before #{i}: less than 2 GiB free in {scratch}', flush=True)
            stopped = True
            return
    work = free_works.get()
    t0 = time.monotonic()
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
        tagged = tags.get(k, [])
        hint = m[3].split() if len(m) == 4 else []
        order = list(dict.fromkeys(tagged + hint + GROUPS))
        killer, out, ran, lost = None, '', 0, None
        for g in order:
            r = run_groups(work, os.path.join(scratch, f'gm{i}'), [g])
            ran += 1
            if unstarted(r):   # (its output kept: why it didn't start is not known yet)
                lost = g
                open(os.path.join(scratch, f'unstarted-{i}-{g}.log'), 'w').write(r.stdout + r.stderr)
                break
            if r.returncode != 0:
                killer, out = (failed_groups(r.stdout) or [g])[0], r.stdout
                break
        open(path, 'w').write(text)
        rmtree(os.path.join(scratch, f'gm{i}'))
        fails = [l for l in out.splitlines() if l.startswith('script:') and not l.startswith('script: 0 of')]
        took = time.monotonic() - t0
        with lock:
            if lost:
                unrun.append(i)
                print(f'#{i} NOT TESTED: {lost}\'s compositor never started [{ran} of {len(order)} groups] {f}: {old.strip()[:60]!r} '
                      f'(its output: {scratch}/unstarted-{i}-{lost}.log)', flush=True)
                return
            if killer and killer not in tagged:
                tags[k] = [killer]
                retagged.append(i)
            if not killer:
                survivors.append(i)
            how = f'killed by {killer}' if killer else 'SURVIVED'
            print(f'#{i} {how} [{ran} of {len(order)} groups, {took:.0f} s] {f}: {old.strip()[:60]!r}  {fails[:1]}', flush=True)
    finally:
        free_works.put(work)

todo = [i for i in range(len(MUTANTS)) if only is None or i in only] if 'GUI_BASE_ONLY' not in os.environ else []
with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
    # The pool takes them in order: the base's groups first, the mutants as workers come free.
    futs = [ex.submit(base_group, g) for g in GROUPS] + [ex.submit(one, i) for i in todo]
    for f in futs:
        f.result()
t_end = time.monotonic()
if base_failed:
    for w in works:
        shutil.rmtree(w, ignore_errors=True)
    print(f'the unmutated build fails tests/gui/run.sh under load in {base_failed}: fix that first (or fewer --jobs); '
          f'every mutant result of this run is thrown away, no tags written')
    sys.exit(2)
if 'GUI_BASE_ONLY' in os.environ:
    sys.exit(0)
if retagged:
    live = {key(m) for m in MUTANTS}
    with open(tags_path, 'w') as out:
        out.write('# tools/mutation/gui.py: each GUI mutant (its key) and the run.sh group that killed it\n')
        for k in sorted(tags):
            if k in live and tags[k] != ['all']:
                out.write(f'{k} {" ".join(tags[k])}\n')
    print(f'{len(retagged)} mutants tagged in {tags_path}: {sorted(retagged)}')
for w in works:
    shutil.rmtree(w, ignore_errors=True)
# What didn't run is said, so a summary never reads 0 for mutants that weren't tested (reviewer-2,
# F7.1.1 review).
untested = sorted(unbuilt + unrun) + (['the rest: out of disk'] if stopped else [])
print(f'{len(todo)} GUI mutants, {len(survivors)} survived: {sorted(survivors)}' + (f'; NOT TESTED: {untested}' if untested else ''))
print(f'time: {t_end - t_start:.0f} s, {jobs} at once (the base done at {t_base - t_start:.0f} s)')
if untested:
    sys.exit(1)
