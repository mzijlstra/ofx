#!/usr/bin/env python3
"""Single-site mutation testing for ofx's unit-tested layers (fs/, ui/).

    tools/mutation/run.py [mutants.py] [filter]

Each mutant is (file, old, new): `old` must occur exactly once in the file (asserted before the
run, so a mutant never lands on a second site unseen). The repo is copied to a scratch directory
once; each mutant is applied there, `build/test` built and run, and the file put back. A mutant
the tests don't fail on SURVIVES: a missing test until it is shown inert (house standard,
../CLAUDE.md). Prints one line per mutant and the survivors at the end.
"""
import os, shutil, subprocess, sys, tempfile, importlib.util

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
spec_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, 'mutants.py')
only = sys.argv[2] if len(sys.argv) > 2 else ''
spec = importlib.util.spec_from_file_location('mutants', spec_path)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
mutants = [m for m in mod.MUTANTS if only in m[0] or only in m[1]]

work = tempfile.mkdtemp(prefix='ofx-mut-')
for d in ('base', 'fs', 'ui', 'tests'):
    shutil.copytree(os.path.join(root, d), os.path.join(work, d))
shutil.copy(os.path.join(root, 'Makefile'), work)

bad = 0
for i, (f, old, new) in enumerate(mutants):
    text = open(os.path.join(work, f)).read()
    n = text.count(old)
    if n != 1:
        print(f'#{i} {f}: old text occurs {n} times, not once: {old!r}')
        bad += 1
if bad:
    shutil.rmtree(work, ignore_errors=True)
    sys.exit(2)

# The tests' mkdtemp roots go under work (TMPDIR), so a mutant that crashes a run before it cleans
# up leaves nothing in /tmp: work goes as a whole at the end.
tmp = os.path.join(work, 'tmp')
os.makedirs(tmp)
env = dict(os.environ, TMPDIR=tmp)

def run():
    b = subprocess.run(['make', '-s', 'build/test'], cwd=work, capture_output=True, text=True)
    if b.returncode != 0:
        return 'build-fail'
    t = subprocess.run(['./build/test'], cwd=work, capture_output=True, timeout=120, env=env)   # bytes: a mutant may print anything
    return 'pass' if t.returncode == 0 else 'fail'

base = run()
if base != 'pass':
    print('baseline does not pass:', base)
    shutil.rmtree(work, ignore_errors=True)
    sys.exit(2)
survivors = []
try:
    for i, (f, old, new) in enumerate(mutants):
        path = os.path.join(work, f)
        text = open(path).read()
        open(path, 'w').write(text.replace(old, new, 1))
        try:
            r = run()
        except subprocess.TimeoutExpired:
            r = 'timeout'
        open(path, 'w').write(text)
        verdict = 'SURVIVED' if r == 'pass' else 'killed' + (' (build)' if r == 'build-fail' else '')
        print(f'#{i:02d} {verdict:16} {f}: {old.strip()[:70]!r} -> {new.strip()[:50]!r}', flush=True)
        if r == 'pass':
            survivors.append(i)
finally:
    shutil.rmtree(work, ignore_errors=True)   # an interrupted run too
print(f'{len(mutants)} mutants, {len(survivors)} survived: {survivors}')
