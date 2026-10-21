# fx: Claude instructions

fx is Michael's file explorer: one folder at a time as a list, in the Omarchy theme's colours and
icons, in C on the prdash and uml-viewer stack (arenas, static memory, no malloc in our code;
Clay; raylib on native Wayland). `docs/plan.md` has the milestones, the decisions and each
milestone's exit criteria. This file holds what the code can't tell you. `../CLAUDE.md` has the
rules shared by every tool here.

## What a test run could do to Michael's desktop

- **fx launches programs.** Enter on a file runs `uwsm-app -- xdg-open <file>`, `t` runs
  `uwsm-app -- xdg-terminal-exec --dir=<folder>`. A test that reaches them with the real
  programs on PATH opens real windows on his screen. Every GUI run goes through
  `tools/gui/run-script.sh`, which puts logging fakes for all three first on PATH (they write
  `<scratch>/spawn.log`). The unit tests use their own fakes in a mkdtemp directory.
- **fx opens a window** (it links raylib): never from a plain shell, only in the test weston
  through `run-script.sh` (below); with this session's WAYLAND_DISPLAY it would open on his screen. `build/fx-probe` and `build/test` link no raylib and are safe
  to run anywhere.
- **The theme:** fx reads `$XDG_STATE_HOME/omarchy/current/theme`. `run-script.sh` points it at a
  scratch copy (`<scratch>/state`), so a theme test edits the copy, never Michael's files. Never
  run `omarchy-theme-set` or anything else that changes his theme to test a switch.
- **The sidebar** reads the GTK bookmarks (shared with Nemo) and the XDG user dirs under
  `$XDG_CONFIG_HOME`; `run-script.sh` points that at `<scratch>/config`. fx never writes them.
- **fx changes files since F3:** rename, new folder, trash, delete for good, copy and move. Every
  write in fs/ goes through `ops_allowed` (fs/ops.c), which refuses any path outside
  `FX_GUI_ROOT` when it is set: `run-script.sh` sets it to the run's scratch dir, the unit tests
  to their mkdtemp root. A guard test aims at a decoy beside that root, never at `/tmp` or a real
  folder, so a mutant that switches the guard off can only reach the decoy. Never start fx in a
  test on a folder that isn't scratch; `tests/gui/guard3.script` shows the guard refusing there.
- **The trash:** fx trashes into `$XDG_DATA_HOME/Trash`, or a drive's own `.Trash-$UID` (on his
  `/mnt/sd` that is `/mnt/sd/.Trash-1000`, shared with Nemo). `run-script.sh` points
  `XDG_DATA_HOME` at `<scratch>/data`; the unit tests set it to their root.
- **The clipboard:** fx sets the system clipboard through `ext-data-control-v1` on Ctrl+C or
  Ctrl+X, in the list and in the edit lines (text, F5.2), which replaces Michael's clipboard. weston has no data-control, so no test touches it;
  the one live check (copy in fx, paste in Nemo and a terminal, and back) needs his go first.
- The scripts' `sh` lines make and change files: point them at scratch folders only, never his
  home or a work repo.
- **A window that stops answering** pops Hyprland's "terminate?" dialog on his screen, even on the
  headless output. The script's `sh` runs under `timeout 5` for that reason; keep everything a
  script waits for short.

## Testing the GUI

Since 2026-10-20 fx's window tests run in a **private headless weston** (`tools/gui/weston.sh`),
started and stopped by `tools/gui/run-script.sh` for each run: its own Wayland socket
(`fx-test-<pid>` under `$XDG_RUNTIME_DIR`), never `wayland-1` (Michael's Hyprland). Nothing touches
his Hyprland: no output, no workspace, no dock script, no hyprctl at all.

```sh
tests/gui/run.sh <absolute scratch>          # every script, each in its own weston
tools/gui/run-script.sh <absolute scratch> tests/gui/<script> "$PWD/build/fx" <folder>   # one
tools/mutation/gui.py <absolute scratch>     # the GUI mutants
```

- **Why not a Hyprland headless output any more:** a new output takes a workspace Hyprland picks,
  and that can't be steered (workspace rules with `default` and `persistent` didn't). Three times
  on 2026-10-20 it was one holding Michael's windows (Nemo on 6, Chromium on 2). Never go back to
  `hyprctl output create` for fx.
- **weston's flags matter** (`weston.sh`): `--fake-seat`, or GLFW 3.4 crashes reading a seat that
  isn't there; `--shell=kiosk` fills the 944x518 logical output, and the window opens at
  `FX_TEST_SIZE=936x490` (the kiosk shell rejects a larger first frame; GLFW's own decorations
  take the other 8x28). `--width`/`--height` are logical pixels.
- `run-script.sh` checks the socket is the one weston made before launching, takes the pid from the
  launch script, and stops weston after the run. For an unscripted run (`-`), kill the pid, then
  `tools/gui/weston.sh stop <scratch>`.
- Close a window with SIGTERM (`kill <pid>`; fx exits through main, so LeakSanitizer runs).
- `FX_TEST_LEAK=1` plants a 4 KiB leak: the control that shows LeakSanitizer reports arrive.
- `FX_GUI_DEBUG=1` logs every key, character and mouse button event.
- `FX_TEST_COPY_DELAY_MS` sleeps per copied block (500 ms at most), so a script sees the progress
  line and can cancel a copy in the middle.
- The script language is at the top of `gui/script.c`. Start every script with `settle`; check
  the `state` line rather than screenshots where you can.

## Vendored code

`vendor/`: Clay (`main` at e6cc369, `clay.h` and its licence) and raylib 6.0 (dbc56a8, `src/` and
`LICENSE`), both as in prdash and uml-viewer, built by our Makefile. `base/` and `gui/font.c`,
`gui/render.c` are uml-viewer's at cca8e45 (`render.c` gained images). Do not edit vendored files.

## Git

Local only, with no remote yet. Commit and branch freely. Pushing and creating the remote are
Michael's calls.
