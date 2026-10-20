#!/usr/bin/env python3
"""GUI mutants: each applied to a scratch copy of the repo, built, and run through
tests/gui/run.sh on a headless output the caller made (CLAUDE.md, "Testing the GUI").

    tools/mutation/gui.py <output> <scratch-dir> [index,...]
"""
import os, shutil, subprocess, sys

G, W = 'gui/main.c', 'gui/watch.c'
MUTANTS = [
    (G, "    go_to(up, str_from_c(child_buf), 0);", "    go_to(up, STR_EMPTY, 0);"),
    (G, "    if (go_to(app.cwd, str_from_c(keep), app.ui.sel)) return;", "    if (go_to(app.cwd, STR_EMPTY, app.ui.sel)) return;"),
    (G, "        memmove(up, d.p, (size_t)d.len);\n        up[d.len] = 0;", "        snprintf(up, sizeof up, \"%.*s\", STR_ARG(d));"),
    (G, "        atomic_store(&watch.list_ready, true);\n        glfwPostEmptyEvent();\n        SetWindowTitle", "        SetWindowTitle"),
    (W, "static const u32 WATCH_DIR_MASK = IN_CREATE | ", "static const u32 WATCH_DIR_MASK = "),
    (W, "    if (rewatch) watch.theme_wd = inotify_add_watch(watch.fd, watch.theme, WATCH_THEME_MASK);", ""),
    (W, "        if (theme) atomic_store(&watch.theme_ready, true);", ""),
]
out, scratch = sys.argv[1], sys.argv[2]
only = [int(x) for x in sys.argv[3].split(',')] if len(sys.argv) > 3 else None
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
    r = subprocess.run([os.path.join(root, 'tests/gui/run.sh'), out, os.path.join(scratch, f'gm{i}'), os.path.join(work, 'build/fx')],
                       capture_output=True, text=True)
    open(path, 'w').write(text)
    verdict = 'killed' if r.returncode != 0 else 'SURVIVED'
    fails = [l for l in r.stdout.splitlines() if l.startswith('script:') and not l.startswith('script: 0 of')]
    print(f'#{i} {verdict} {f}: {old.strip()[:60]!r}  {fails[:1]}', flush=True)
    if r.returncode == 0:
        survivors.append(i)
print(f'{len(MUTANTS)} GUI mutants, {len(survivors)} survived: {survivors}')
