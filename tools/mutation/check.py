#!/usr/bin/env python3
"""Both mutant lists' texts each found exactly once in the code, as run.py and gui.py need them.

    tools/mutation/check.py

Run by make test (the F16 code-health review, 2026-11-10: twelve unit mutants had gone stale during
F16 and no full run.py could start; only filtered runs, which skip the check, had been made). A
commit that changes a mutant's text now fails its own make test, not the next mutation run.
"""
import os, sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
from common import check_unique
import mutants, gui_mutants

root = os.path.join(here, '..', '..')
bad = [name for name, ms in (('mutants.py', mutants.MUTANTS), ('gui_mutants.py', gui_mutants.MUTANTS)) if check_unique(root, ms)]
if bad:
    sys.exit(f'check.py: {" and ".join(bad)}: retarget the entries above (or drop one whose code is gone, saying why)')
