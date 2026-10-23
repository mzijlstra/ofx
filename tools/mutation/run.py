#!/usr/bin/env python3
"""Single-site mutation testing for ofx's unit-tested layers (fs/, ui/).

    tools/mutation/run.py [mutants.py] [filter] [-j N]

Each mutant is (file, old, new): `old` must occur exactly once in the file (asserted before the
run, so a mutant never lands on a second site unseen). The repo is copied to one scratch directory
per worker (-j, the cores by default); each mutant is applied in a worker's copy, `build/test`
built with clang (AddressSanitizer and UBSan as in `make test`, a little over twice as fast to
compile as gcc: Michael, 2026-10-23) and run, and the file put back. Every worker runs the
unmutated tests first, all at once, so a test that only fails under load shows before any kill
counts. A mutant the tests don't fail on SURVIVES: a missing test until it is shown inert (house
standard, ../CLAUDE.md). Prints one line per mutant as it finishes and the survivors at the end.
"""
import os, shutil, subprocess, sys, tempfile, importlib.util
from concurrent.futures import ThreadPoolExecutor
from queue import Queue

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
args = sys.argv[1:]
jobs = os.cpu_count() or 1
if '-j' in args:
    k = args.index('-j')
    jobs = max(1, int(args[k + 1]))
    del args[k:k + 2]
spec_path = args[0] if len(args) > 0 else os.path.join(here, 'mutants.py')
only = args[1] if len(args) > 1 else ''
spec = importlib.util.spec_from_file_location('mutants', spec_path)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
mutants = [m for m in mod.MUTANTS if only in m[0] or only in m[1]]

bad = 0
for i, (f, old, new) in enumerate(mutants):
    n = open(os.path.join(root, f)).read().count(old)
    if n != 1:
        print(f'#{i} {f}: old text occurs {n} times, not once: {old!r}')
        bad += 1
if bad:
    sys.exit(2)

# One copy of the repo per worker. The tests' mkdtemp roots go under each copy (TMPDIR), so a
# mutant that crashes a run before it cleans up leaves nothing in /tmp: the copies go as a whole.
top = tempfile.mkdtemp(prefix='ofx-mut-')
jobs = min(jobs, max(1, len(mutants)))
workers = []
for w in range(jobs):
    work = os.path.join(top, f'w{w}')
    for d in ('base', 'fs', 'ui', 'tests'):
        shutil.copytree(os.path.join(root, d), os.path.join(work, d))
    shutil.copy(os.path.join(root, 'Makefile'), work)
    os.makedirs(os.path.join(work, 'tmp'))
    workers.append(work)

def run(work):
    b = subprocess.run(['make', '-s', 'CC=clang', 'build/test'], cwd=work, capture_output=True, text=True)
    if b.returncode != 0:
        return 'build-fail'
    env = dict(os.environ, TMPDIR=os.path.join(work, 'tmp'))
    t = subprocess.run(['./build/test'], cwd=work, capture_output=True, timeout=120, env=env)   # bytes: a mutant may print anything
    return 'pass' if t.returncode == 0 else 'fail'

free = Queue()
for work in workers:
    free.put(work)

def one(item):
    i, (f, old, new) = item
    work = free.get()
    try:
        path = os.path.join(work, f)
        text = open(path).read()
        open(path, 'w').write(text.replace(old, new, 1))
        try:
            r = run(work)
        except subprocess.TimeoutExpired:
            r = 'timeout'
        open(path, 'w').write(text)
    finally:
        free.put(work)
    verdict = 'SURVIVED' if r == 'pass' else 'killed' + (' (build)' if r == 'build-fail' else '')
    print(f'#{i:02d} {verdict:16} {f}: {old.strip()[:70]!r} -> {new.strip()[:50]!r}', flush=True)
    return i, r

try:
    with ThreadPoolExecutor(jobs) as pool:
        bases = list(pool.map(run, workers))
    if any(b != 'pass' for b in bases):
        print('baseline does not pass in every worker:', bases)
        sys.exit(2)
    print(f'baseline passes in {jobs} workers at once', flush=True)
    with ThreadPoolExecutor(jobs) as pool:
        results = list(pool.map(one, enumerate(mutants)))
finally:
    shutil.rmtree(top, ignore_errors=True)   # an interrupted run too
survivors = sorted(i for i, r in results if r == 'pass')
print(f'{len(mutants)} mutants, {len(survivors)} survived: {survivors}')
