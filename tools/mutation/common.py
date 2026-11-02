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
