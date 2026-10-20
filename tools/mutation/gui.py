#!/usr/bin/env python3
"""GUI mutants: each applied to a scratch copy of the repo, built, and run through
tests/gui/run.sh, each script in its own headless weston (CLAUDE.md, "Testing the GUI").

    tools/mutation/gui.py <scratch-dir> [index,...]
"""
import os, shutil, subprocess, sys

G, W = 'gui/main.c', 'gui/watch.c'
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
