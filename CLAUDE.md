# fx: Claude instructions

fx is Michael's file explorer: one folder at a time as a list, in the Omarchy theme's colours and
icons, in C on the prdash and uml-viewer stack (arenas, static memory, no malloc in our code;
Clay; raylib on native Wayland). `docs/plan.md` has the milestones, the decisions and F1's exit
criteria. This file holds what the code can't tell you. `../CLAUDE.md` has the rules shared by
every tool here.

## What a test run could do to Michael's desktop

- **fx launches programs.** Enter on a file runs `uwsm-app -- xdg-open <file>`, `t` runs
  `uwsm-app -- xdg-terminal-exec --dir=<folder>`. A test that reaches them with the real
  programs on PATH opens real windows on his screen. Every GUI run goes through
  `tools/gui/run-script.sh`, which puts logging fakes for all three first on PATH (they write
  `<scratch>/spawn.log`). The unit tests use their own fakes in a mkdtemp directory.
- **fx opens a window** (it links raylib): never from a plain shell, only on a Hyprland headless
  output through `run-script.sh`. `build/fx-probe` and `build/test` link no raylib and are safe
  to run anywhere.
- **The theme:** fx reads `$XDG_STATE_HOME/omarchy/current/theme`. `run-script.sh` points it at a
  scratch copy (`<scratch>/state`), so a theme test edits the copy, never Michael's files. Never
  run `omarchy-theme-set` or anything else that changes his theme to test a switch.
- **fx only reads in F1:** listing folders, the MIME database, the icon theme. The scripts' `sh`
  lines make and change files: point them at scratch folders only, never his home or a work repo.
- **A window that stops answering** pops Hyprland's "terminate?" dialog on his screen, even on the
  headless output. The script's `sh` runs under `timeout 5` for that reason; keep everything a
  script waits for short.

## Testing the GUI

```sh
hyprctl output create headless FXGUI          # your own name; it gets the next free workspace and scale 2
tools/gui/run-script.sh FXGUI <scratch> tests/gui/<script> "$PWD/build/fx" <folder>
hyprctl output remove FXGUI                   # only once no fx window is left on it
tail -2 ~/.local/state/omarchy/monitor-hook.log
```

- `run-script.sh` refuses to run when the output's workspace holds a window that isn't fx (a
  new headless output may take one of Michael's hidden workspaces), takes the pid from the launch
  script (never a class lookup: a reviewer may run fx at the same time), and reports what is left
  on the output.
- Every rule in `../uml-viewer/CLAUDE.md`, "Testing the GUI", holds here too: only the vetted
  hyprctl commands, no mouse, never reconfigure a monitor, remove the output when you stop, close
  a window with SIGTERM (`kill <pid>`; fx exits through main, so LeakSanitizer runs).
- `FX_TEST_LEAK=1` plants a 4 KiB leak: the control that shows LeakSanitizer reports arrive.
- `FX_GUI_DEBUG=1` logs every key, character and mouse button event.
- The script language is at the top of `gui/script.c`. Start every script with `settle`; check
  the `state` line rather than screenshots where you can.

## Vendored code

`vendor/`: Clay (`main` at e6cc369, `clay.h` and its licence) and raylib 6.0 (dbc56a8, `src/` and
`LICENSE`), both as in prdash and uml-viewer, built by our Makefile. `base/` and `gui/font.c`,
`gui/render.c` are uml-viewer's at cca8e45 (`render.c` gained images). Do not edit vendored files.

## Git

Local only, with no remote yet. Commit and branch freely. Pushing and creating the remote are
Michael's calls.
