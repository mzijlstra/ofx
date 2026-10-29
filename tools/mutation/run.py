#!/usr/bin/env python3
"""Single-site mutation testing for ofx's unit-tested layers (fs/, ui/).

    tools/mutation/run.py [mutants.py] [filter] [-j N] [--clang-only]

Each mutant is (file, old, new): `old` must occur exactly once in the file (asserted before the
run, so a mutant never lands on a second site unseen). The repo is copied to one scratch directory
per worker (-j, the cores by default); each mutant is applied in a worker's copy, `build/test`
built with clang (AddressSanitizer and UBSan as in `make test`, a little over twice as fast to
compile as gcc: Michael, 2026-10-23) and run, and the file put back. Every worker runs the
unmutated tests first, all at once, so a test that only fails under load shows before any kill
counts. A mutant the tests don't fail on SURVIVES: a missing test until it is shown inert (house
standard, ../CLAUDE.md). Prints one line per mutant as it finishes and the survivors at the end.

Two passes when tcc is there (Michael, 2026-10-24): tcc builds the tests in about 25 ms, against
clang's 1.45 s, but has no sanitizers. So every mutant runs first under tcc; what its tests fail on
is killed. Only the rest (survivors, and mutants tcc can't build: it may refuse valid C) run again
under clang with ASan and UBSan, where a sanitizer may still catch them. --clang-only skips tcc.
"""
import os, shutil, signal, subprocess, sys, tempfile, time, importlib.util
from concurrent.futures import ThreadPoolExecutor
from queue import Queue

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
args = sys.argv[1:]
use_tcc = '--clang-only' not in args and shutil.which('tcc') is not None
args = [a for a in args if a != '--clang-only']
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
    os.symlink(os.path.join(root, 'vendor'), os.path.join(work, 'vendor'))   # stb_image_resize2 (fs/thumb.c), never mutated
    os.makedirs(os.path.join(work, 'tmp'))
    workers.append(work)

def run(work, cc='clang'):
    if cc == 'tcc':
        os.makedirs(os.path.join(work, 'build'), exist_ok=True)
        b = subprocess.run(['tcc', '-std=c11', '-D_GNU_SOURCE', '-pthread', '-isystem', 'vendor/raylib/src', '-o', 'build/test-tcc',
                            'tests/test.c', '-lturbojpeg', '-lwebp', '-lpng16', '-lgif', '-lz', '-lm'],
                           cwd=work, capture_output=True, text=True)
        exe = './build/test-tcc'
    else:
        b = subprocess.run(['make', '-s', 'CC=clang', 'build/test'], cwd=work, capture_output=True, text=True)
        exe = './build/test'
    if b.returncode != 0:
        return 'build-fail'
    env = dict(os.environ, TMPDIR=os.path.join(work, 'tmp'))
    # 10 s: the tests take 0.5-0.7 s, and a mutant that hangs them (#172 waited 120 s here, holding
    # up a whole pass) is killed by the timeout either way. In its own process group, killed whole
    # afterwards: a test killed or crashed early leaves the children it forks (test_f7's paused
    # ones) behind, 264 of them by 2026-10-29.
    t = subprocess.Popen([exe], cwd=work, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env,
                         start_new_session=True)
    try:
        code = t.wait(timeout=10)
    finally:
        try:
            os.killpg(t.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        t.wait()
    return 'pass' if code == 0 else 'fail'

# A tree removed whole: a test killed half way leaves its unreadable folders (test_f65's `locked`),
# which rmtree alone can't empty (132 runs' copies stayed in /tmp by 2026-10-29).
def rmtree(path):
    for d, dirs, _ in os.walk(path):
        for x in dirs:
            p = os.path.join(d, x)
            if os.path.islink(p):   # (chmod follows a link: its target may be anywhere; reviewer-2, 2026-10-29)
                continue
            try:
                os.chmod(p, 0o700)
            except OSError:
                pass
    shutil.rmtree(path, ignore_errors=True)

free = Queue()
for work in workers:
    free.put(work)

def one(item, cc='clang'):
    i, (f, old, new) = item
    work = free.get()
    t0 = time.time()
    try:
        path = os.path.join(work, f)
        text = open(path).read()
        open(path, 'w').write(text.replace(old, new, 1))
        try:
            r = run(work, cc)
        except subprocess.TimeoutExpired:
            r = 'timeout'
        open(path, 'w').write(text)
    finally:
        free.put(work)
    took = time.time() - t0
    if cc == 'tcc' and r in ('pass', 'build-fail'):
        return i, r   # clang decides
    verdict = 'SURVIVED' if r == 'pass' else 'killed' + (' (build)' if r == 'build-fail' else '') + (' (tcc)' if cc == 'tcc' else '')
    print(f'#{i:02d} {verdict:16} {took:5.1f}s {f}: {old.strip()[:70]!r} -> {new.strip()[:50]!r}', flush=True)
    return i, r

try:
    passes = ['tcc', 'clang'] if use_tcc else ['clang']
    for cc in passes:
        with ThreadPoolExecutor(jobs) as pool:
            bases = list(pool.map(lambda w: run(w, cc), workers))
        if any(b != 'pass' for b in bases):
            print(f'baseline does not pass under {cc} in every worker:', bases)
            sys.exit(2)
    print(f'baseline passes under {" and ".join(passes)} in {jobs} workers at once', flush=True)
    todo = list(enumerate(mutants))
    results = []
    for cc in passes:
        t0 = time.time()
        with ThreadPoolExecutor(jobs) as pool:
            got = list(pool.map(lambda it: one(it, cc), todo))
        if cc == 'tcc':
            results += [(i, r) for i, r in got if r not in ('pass', 'build-fail')]
            again = {i for i, r in got if r in ('pass', 'build-fail')}
            todo = [it for it in todo if it[0] in again]
            print(f'tcc: {len(got) - len(todo)} killed in {time.time() - t0:.0f} s; {len(todo)} go to clang', flush=True)
        else:
            results += got
            print(f'{cc}: {len(got)} run in {time.time() - t0:.0f} s', flush=True)
finally:
    rmtree(top)   # an interrupted run too
survivors = sorted(i for i, r in results if r == 'pass')
print(f'{len(mutants)} mutants, {len(survivors)} survived: {survivors}')
