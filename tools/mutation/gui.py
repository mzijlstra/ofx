#!/usr/bin/env python3
"""GUI mutants: each applied to a scratch copy of the repo, built, and run through
tests/gui/run.sh, each script in its own headless weston (CLAUDE.md, "Testing the GUI").

    tools/mutation/gui.py <scratch-dir> [index,...]
"""
import os, shutil, subprocess, sys

G, W, K = 'gui/main.c', 'gui/watch.c', 'gui/counts.c'
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
    (G, "        if (a.index >= 0 && a.index < app.places.n) go_to(app.places.p[a.index].path, STR_EMPTY, 0);", ""),
    (G, "        hist_unstep(&hist, dir);\n", ""),
    # F3
    (G, "        if (k >= 0 && !mark[k]) mark[k] = 1, marked++;", ""),
    (G, "        if (app.ui.marked ? !app.ui.mark[i] : i != app.ui.list.sel) continue;", "        if (i != app.ui.list.sel) continue;"),
    (G, "            if (it->out != OUT_NEW) continue;", ""),
    (G, "    if (run.own_cut && !atomic_load(&j->cancel) && own_gen == run.own_gen) own.n = 0, clip_clear_own();", ""),
    (G, "if (!name_free(app.cwd, base, S_ISDIR(st.st_mode), true, true, name, sizeof name)) continue;", "if (!name_free(app.cwd, base, S_ISDIR(st.st_mode), false, true, name, sizeof name)) continue;"),
    (G, "        if (S_ISDIR(st.st_mode) && into_itself(src, app.cwd)) {", "        if (0) {"),
    (G, "            if (c->cut) {\n                here++;", "            if (0) {\n                here++;"),
    (G, "        undo_begin(&undo, app.edit_new ? UNDO_MKDIR : UNDO_RENAME);", "        undo_begin(&undo, UNDO_RENAME);"),
    (G, "    if (back[0]) go_to(app.cwd, str_from_c(back), app.ui.list.sel);\n    else reload();", "    reload();"),
    (G, "        return m == GLFW_MOD_SHIFT ? UK_SHIFT_DELETE : m == 0 ? UK_DELETE : UK_NONE;", "        return m == 0 ? UK_DELETE : UK_SHIFT_DELETE;"),
    (G, "    if (m == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_N) return UK_NEWFOLDER;", ""),
    (G, "    if (ask && !app.ui.asking) app.ui.asking = true, app.ui.ask_all = false;", ""),
    (G, "    ops_guard(getenv(\"FX_GUI_ROOT\"));", ""),
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
    (G, "    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {", "    if (stat(path, &st) != 0) {"),
    (G, "    if (h.tab >= 0) in->tab_close = h.tab;\n    else if (h.row >= 0)", "    if (h.row >= 0)"),
    (G, "    if (m == GLFW_MOD_CONTROL && key == GLFW_KEY_W) return UK_TAB_CLOSE;", ""),
    (G, "    if (action == GLFW_PRESS && m == GLFW_MOD_ALT && key >= GLFW_KEY_1 && key <= GLFW_KEY_9)", "    if (action == GLFW_PRESS && key >= GLFW_KEY_1 && key <= GLFW_KEY_9)"),
    (G, "    return HEADER_H + (tabbar.n >= 2 ? TAB_H : 0);", "    return HEADER_H;"),
    (G, "    case UA_TAB_MOVE: ui_tabs_move(&tabbar, (int)a.index); break;", "    case UA_TAB_MOVE: break;"),
    # F5 review: the delete question's targets taken when it opens
    (G, "    for (isize i = 0; i < n; i++) run.item[i] = (JobItem){ .src = arena_cstr(&joba, str_from_c(doomed.path[i])) };", "    char *late[CLIP_PATHS_MAX];\n    n = targets(&joba, late, CLIP_PATHS_MAX);\n    for (isize i = 0; i < n; i++) run.item[i] = (JobItem){ .src = late[i] };"),
    (G, "    if (undo.n == 0) undo_clear(&undo);\n    }", "    if (undo.n == 0) undo_clear(&undo);\n    }\n    if (j->kind == JOB_DELETE) ui_mark_clear(&app.ui);"),
    (G, "        if (!was_confirming && (app.ui.confirming || a.kind == UA_DELETE)) delete_ask();", "        if (!was_confirming && app.ui.confirming) delete_ask();"),
    (G, "    doomed.n = 0;\n    if (app.ui.marked > CLIP_PATHS_MAX) {", "    if (app.ui.marked > CLIP_PATHS_MAX) {"),
    # F5.1: the modal panels
    (G, "        for (int k = 0; k < COUNT(MB) && !h.modal; k++)\n            if (in_box(Clay_GetElementId(cz(MB[k].id)), x, y)) h.modal = MB[k].b;\n        return h;", "        return h;"),
    # Not run: hit_at's early return while a panel is up; ui_handle drops every other click during a
    # prompt anyway, so leaving it out changes nothing a test can see.
    (G, "    if (ask && !app.ui.asking) app.ui.asking = true, app.ui.ask_all = false;", "    if (ask && !app.ui.asking) app.ui.asking = true;"),
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
    (G, "!atomic_load(&j->cancel) && own_gen == run.own_gen)", "!atomic_load(&j->cancel))"),
]
scratch = sys.argv[1]
only = [int(x) for x in sys.argv[2].split(',')] if len(sys.argv) > 2 else None
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
work = os.path.join(scratch, 'gui-mut-src')
shutil.rmtree(work, ignore_errors=True)
shutil.copytree(root, work, ignore=shutil.ignore_patterns('.git', 'build'))
os.makedirs(os.path.join(work, 'build'))
os.symlink(os.path.join(root, 'build', 'raylib'), os.path.join(work, 'build', 'raylib'))
for f, old, new in MUTANTS:
    n = open(os.path.join(work, f)).read().count(old)
    assert n == 1, (f, old, n)
survivors = []
for i, (f, old, new) in enumerate(MUTANTS):
    if only is not None and i not in only:
        continue
    path = os.path.join(work, f)
    text = open(path).read()
    open(path, 'w').write(text.replace(old, new, 1))
    b = subprocess.run(['make', '-s', 'build/fx'], cwd=work, capture_output=True, text=True)
    if b.returncode != 0:
        print(f'#{i} build failed: {b.stderr[-300:]}')
        open(path, 'w').write(text)
        continue
    r = subprocess.run([os.path.join(root, 'tests/gui/run.sh'), os.path.join(scratch, f'gm{i}'), os.path.join(work, 'build/fx')],
                       capture_output=True, text=True)
    open(path, 'w').write(text)
    verdict = 'killed' if r.returncode != 0 else 'SURVIVED'
    fails = [l for l in r.stdout.splitlines() if l.startswith('script:') and not l.startswith('script: 0 of')]
    print(f'#{i} {verdict} {f}: {old.strip()[:60]!r}  {fails[:1]}', flush=True)
    if r.returncode == 0:
        survivors.append(i)
print(f'{len(MUTANTS)} GUI mutants, {len(survivors)} survived: {survivors}')
