"""What run.py and gui.py share."""
import os, shutil

# A tree removed whole: a test killed half way leaves its unreadable folders (test_f65's and f6's
# `locked`), which rmtree alone can't empty (two trees stayed on 2026-10-24, 132 by 2026-10-29).
# chmod follows a link, whose target may be anywhere: links are left as they are (review,
# 2026-10-29).
def rmtree(path):
    for d, dirs, _ in os.walk(path):
        for x in dirs:
            p = os.path.join(d, x)
            if os.path.islink(p):
                continue
            try:
                os.chmod(p, 0o700)
            except OSError:
                pass
    shutil.rmtree(path, ignore_errors=True)


# Each mutant's old text must occur exactly once in its file under root (else it would land on a
# second site unseen, or on none): those that don't, printed. True when any.
def check_unique(root, mutants):
    bad = 0
    for i, m in enumerate(mutants):
        f, old = m[0], m[1]
        path = os.path.join(root, f)
        if not os.path.exists(path):   # (a file removed: its mutants go too, said as the stale are)
            print(f'#{i} {f}: the file is gone')
            bad += 1
            continue
        n = open(path).read().count(old)
        if n != 1:
            print(f'#{i} {f}: old text occurs {n} times, not once: {old!r}')
            bad += 1
    return bad > 0
