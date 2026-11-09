#!/usr/bin/env python3
"""GUI mutants: each applied to a scratch copy of the repo, built, and run through
tests/gui/run.sh, each script in its own headless weston (AGENTS.md, "Testing the GUI").

    tools/mutation/gui.py <scratch-dir> [mutant,...] [--jobs N]

A mutant is named by its key (the 12 hex digits each result line prints after its index, or 6 or
more of their first), which stays while mutants are inserted or dropped before it, or by its index
in MUTANTS, which doesn't (2026-11-05: F13's survivors moved three times in a day). The summary
prints the survivors' keys as a command line.

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
import hashlib, os, re, shutil, signal, subprocess, sys

# The mutants: gui_mutants.py (MUTANTS), beside this file.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gui_mutants import MUTANTS
from common import rmtree, check_unique

import concurrent.futures, queue, threading, time

args = sys.argv[1:]
jobs = 6   # the time levels off there: 46 mutants on a quiet machine took 414-451 s at 6, 411-419 at 8, 442 at 10, 477 at 12, CPU 43-62% (2026-10-26)
if '--jobs' in args:
    jobs = int(args[args.index('--jobs') + 1])
    del args[args.index('--jobs'):args.index('--jobs') + 2]
if len(args) not in (1, 2) or any(a.startswith('-') for a in args):
    sys.exit(__doc__.strip())   # an unknown flag isn't taken for the scratch folder
scratch = args[0]
picks = args[1].split(',') if len(args) > 1 else None

# A mutant's tag key: its file and texts, so a mutant edited is a new one, untagged.
def key(m):
    return hashlib.sha1('\0'.join(m[:3]).encode()).hexdigest()[:12]
KEYS = [key(m) for m in MUTANTS]

# The mutants asked for: a token under 6 characters, all digits, is an index; anything else is a key
# or the first 6 or more hex digits of one, which must name exactly one mutant.
def pick(t):
    if len(t) < 6 and t.isdigit():
        if int(t) >= len(MUTANTS):
            sys.exit(f'gui.py: no mutant #{t} (there are {len(MUTANTS)})')
        return int(t)
    hexed = re.fullmatch(r'[0-9a-f]{6,12}', t)
    hits = [i for i, k in enumerate(KEYS) if hexed and k.startswith(t)]
    if len(hits) != 1:
        sys.exit(f'gui.py: {t!r} names {len(hits)} mutants: a key (6 to 12 hex digits) or an index (5 digits at most)'
                 + (' (its text changed since, or it was dropped)' if hexed and not hits else ''))
    return hits[0]
only = sorted({pick(t) for t in picks}) if picks else None
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# OFX_GUI_TAGS: another tags file, for a run beside one that will write this one.
tags_path = os.environ.get('OFX_GUI_TAGS') or os.path.join(root, 'tools', 'mutation', 'gui-tags.txt')
# The run's times, and each mutant's, are printed (the order they start in: at `todo` below).
t_start = time.monotonic()

# run.sh's groups, in its order (read from it, F11 review 2026-11-02): a mutant runs them one at a
# time and stops at the first that kills it (Michael, 2026-10-25).
run_text = open(os.path.join(root, 'tests/gui/run.sh')).read()
GROUPS = re.findall(r'^if want ([a-z0-9]+); then$', run_text, flags=re.M)
assert GROUPS and re.findall(r'want ([a-z0-9]+)', run_text) == GROUPS, 'a want in run.sh outside its own if line'

# A mutant is (file, old, new) or (file, old, new, hint): the hint names the group(s) likeliest to
# kill it, tried first when it has no tags. Its key is the first three only, so a hint added later
# keeps its tags.
for m in MUTANTS:
    assert len(m) in (3, 4) and (len(m) == 3 or all(g in GROUPS for g in m[3].split())), m

# Each worker its own copy of the repo and its own build; the scripts run from the copy, so the
# repo can be edited meanwhile.
# vkbd, vptr and dndpeer (the test sway's keyboard, pointer and other program, run-script.sh puts them
# on PATH) are built once here; no mutant touches them.
subprocess.run(['make', '-s', '-C', root, 'build/vkbd', 'build/vptr', 'build/dndpeer', 'build/ofx-clean'], check=True)   # (ofx-clean: f12b's Clear Previews runs it)
def make_work(w):
    work = os.path.join(scratch, f'gui-mut-src-{w}')
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(root, work, ignore=shutil.ignore_patterns('.git', 'build', 'vendor'))
    os.symlink(os.path.join(root, 'vendor'), os.path.join(work, 'vendor'))   # (16 MB a worker otherwise; never mutated)
    os.makedirs(os.path.join(work, 'build'))
    os.symlink(os.path.join(root, 'build', 'raylib'), os.path.join(work, 'build', 'raylib'))
    for t in ('vkbd', 'vptr', 'dndpeer'):
        os.symlink(os.path.join(root, 'build', t), os.path.join(work, 'build', t))
    os.symlink(os.path.join(root, 'build', 'ofx-clean'), os.path.join(work, 'build', 'ofx-clean'))
    return work
works = [make_work(w) for w in range(jobs)]
if check_unique(works[0], MUTANTS):
    sys.exit(2)


# The group of a script that failed: run.sh prints "== <script>: FAILED" or "<group>: FAILED".
# Read from run.sh (F11 review, 2026-11-02: a hand-kept copy before): every script a group's block
# runs, "rs <name>", "$here/<name>.script", or "$here/<name>$x.script" over its "for x in ...".
def group_of(text):
    parts = re.split(r'^if want ([a-z0-9]+); then$', text, flags=re.M)
    out = {}
    for g, body in zip(parts[1::2], parts[2::2]):
        loops = dict(re.findall(r'for (\w+) in ([^;]+); do', body))
        # (an rs whose script isn't named outright would leave its kills untagged: F13 code-health review)
        bad = [l for l in re.findall(r'(?:^|;)\s*rs (\S+)', body, flags=re.M)
               if not re.fullmatch(r'[a-z0-9]+', l) and not (re.fullmatch(r'"[a-z0-9]+\$(\w+)"', l) and re.fullmatch(r'"[a-z0-9]+\$(\w+)"', l).group(1) in loops)]
        assert not bad, f'run.sh group {g}: rs with a script not named outright: {bad}'
        for name, var in (re.findall(r'(?:^|;)\s*rs ([a-z0-9]+)()', body, flags=re.M) + re.findall(r'(?:^|;)\s*rs "([a-z0-9]+)\$(\w+)"', body, flags=re.M) +
                          re.findall(r'\$here/([a-z0-9]+)(?:\$(\w+))?\.script', body)):
            for n in ([name + x for x in loops[var].split()] if var else [name]):
                if n != g:
                    out[n] = g
    return out
GROUP_OF = group_of(run_text)

# The tags as groups run.sh knows: a script's name (f7b, written before GROUP_OF had it) as its
# group; anything else dropped, so a mutant never "survives" a group that runs nothing.
# A mutant that survived its last run is marked `survived` instead (2026-11-05).
tags, survived_before = {}, set()
if os.path.exists(tags_path):
    for line in open(tags_path):
        w = line.split('#')[0].split()
        g = list(dict.fromkeys(GROUP_OF.get(x, x) for x in w[1:] if GROUP_OF.get(x, x) in GROUPS))
        if len(w) >= 2 and g:
            tags[w[0]] = g
        elif w[1:] == ['survived']:
            survived_before.add(w[0])
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
# of 2026-10-28. the review's marker, rather than one message to look for.)
RUN_LAUNCHED = 'run-script: the window launched'

def unstarted(r):
    if r.returncode == 0:
        return False
    heads = [k for k, l in enumerate(r.stdout.splitlines()) if re.fullmatch(r'== [a-z0-9]+', l)]
    last = r.stdout.splitlines()[heads[-1]:] if heads else r.stdout.splitlines()
    return RUN_LAUNCHED not in last

# cmd in a session of its own, its output kept: stopped (Ctrl-C, an error), its whole group is told
# to stop too, so run-script's trap stops the window and the compositor (F11 review, 2026-11-02: an
# interrupted run left westons behind).
def run_own_session(cmd, env):
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env, start_new_session=True)
    try:
        out, err = p.communicate()
    except BaseException:
        try:
            os.killpg(p.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        p.wait()
        raise
    return subprocess.CompletedProcess(cmd, p.returncode, out, err)

# A mutant's run stops at its first failure (OFX_GUI_FAIL_FAST, run.sh and script.c), the base's
# never: it must pass everything. A group whose compositor didn't start runs again, three times in
# all; unstarted(r) says when it never did.
def run_groups(work, base, groups, fast=True, binary='build/ofx-mut'):
    env = dict(os.environ, OFX_GUI_FAIL_FAST='1') if fast else {k: v for k, v in os.environ.items() if k != 'OFX_GUI_FAIL_FAST'}
    for _ in range(3):
        r = run_own_session([os.path.join(work, 'tests/gui/run.sh'), base, os.path.join(work, binary)] + groups, env)
        if not unstarted(r):
            break
    return r

# The unmutated build in each worker, kept as build/ofx-base for the base's groups (a mutant's build
# replaces build/ofx-mut).
def build_base(w):
    b = subprocess.run(['make', '-s', 'build/ofx-mut', 'build/ofx-list'], cwd=works[w], capture_output=True, text=True)   # (ofx-list: run-script copies it beside the binary's, F16)
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
survived_now = set(survived_before)
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
        b = subprocess.run(['make', '-s', 'build/ofx-mut', 'build/ofx-list'], cwd=work, capture_output=True, text=True)
        if b.returncode != 0:
            open(path, 'w').write(text)
            print(f'#{i} {k} build failed: {b.stderr[-300:]}', flush=True)
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
                print(f'#{i} {k} NOT TESTED: {lost}\'s compositor never started [{ran} of {len(order)} groups] {f}: {old.strip()[:60]!r} '
                      f'(its output: {scratch}/unstarted-{i}-{lost}.log)', flush=True)
                return
            if killer and killer not in tagged:
                tags[k] = [killer]
                retagged.append(i)
            if not killer:
                survivors.append(i)
                survived_now.add(k)
            else:
                survived_now.discard(k)
            how = f'killed by {killer}' if killer else 'SURVIVED'
            print(f'#{i} {k} {how} [{ran} of {len(order)} groups, {took:.0f} s] {f}: {old.strip()[:60]!r}  {fails[:1]}', flush=True)
    finally:
        free_works.put(work)

todo = [i for i in range(len(MUTANTS)) if only is None or i in only] if 'GUI_BASE_ONLY' not in os.environ else []
# The long ones first (Michael, 2026-11-05): a run's time is its workers' over --jobs, plus what idles
# at the end; a mutant that survives runs every group (about 470 s on F13's run), and one started
# last left five workers idle that long. So last run's survivors start first, then the untagged
# (their hint may miss), then the rest in order. Ordering by every mutant's last time was left out:
# runs sat within 5% of that floor (579 mutants, 1868 s against 1798 + the base's 74).
todo.sort(key=lambda i: 0 if KEYS[i] in survived_before else 1 if KEYS[i] not in tags else 2)
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
if retagged or survived_now != survived_before:
    live = set(KEYS)
    with open(tags_path, 'w') as out:
        out.write('# tools/mutation/gui.py: each GUI mutant (its key) and the run.sh group that killed it, or survived\n')
        for k in sorted(set(tags) | survived_now):
            if k not in live:
                continue
            if k in survived_now:
                out.write(f'{k} survived\n')
            elif tags[k] != ['all']:
                out.write(f'{k} {" ".join(tags[k])}\n')
    print(f'{len(retagged)} mutants tagged in {tags_path}: {sorted(retagged)}')
for w in works:
    shutil.rmtree(w, ignore_errors=True)
# What didn't run is said, so a summary never reads 0 for mutants that weren't tested (review,
# F7.1.1 review).
untested = sorted(unbuilt + unrun) + (['the rest: out of disk'] if stopped else [])
print(f'{len(todo)} GUI mutants, {len(survivors)} survived: {sorted(survivors)}' + (f'; NOT TESTED: {untested}' if untested else ''))
if survivors:
    print(f'the survivors again: tools/mutation/gui.py {scratch} {",".join(KEYS[i] for i in sorted(survivors))}')
print(f'time: {t_end - t_start:.0f} s, {jobs} at once (the base done at {t_base - t_start:.0f} s)')
if untested:
    sys.exit(1)
