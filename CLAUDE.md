# ofx: Claude instructions

ofx is Michael's file explorer: one folder at a time as a list, in the Omarchy theme's colours and
icons, in C on the prdash and uml-viewer stack (arenas, static memory, no malloc in our code;
Clay; raylib on native Wayland). `docs/plan.md` has the milestones, the decisions and each
milestone's exit criteria. This file holds what the code can't tell you. `../CLAUDE.md` has the
rules shared by every tool here.

## What a test run could do to Michael's desktop

- **ofx launches programs.** Enter on a file runs `uwsm-app -- gio open <file>`, `t` runs
  `uwsm-app -- xdg-terminal-exec --dir=<folder>`. A test that reaches them with the real
  programs on PATH opens real windows on his screen. Every GUI run goes through
  `tools/gui/run-script.sh`, which puts logging fakes for them (and `xdg-open`, the opener before
  2026-10-24) first on PATH (they write
  `<scratch>/spawn.log`). The unit tests use their own fakes in a mkdtemp directory.
- **ofx opens a window** (it links raylib): never from a plain shell, only in the test weston
  through `run-script.sh` (below); with this session's WAYLAND_DISPLAY it would open on his screen. `build/ofx-probe` and `build/test` link no raylib and are safe
  to run anywhere.
- **The theme:** ofx reads `$XDG_STATE_HOME/omarchy/current/theme`. `run-script.sh` points it at a
  scratch copy (`<scratch>/state`), so a theme test edits the copy, never Michael's files. Never
  run `omarchy-theme-set` or anything else that changes his theme to test a switch.
- **The sidebar** reads the GTK bookmarks (shared with Nemo) and the XDG user dirs under
  `$XDG_CONFIG_HOME`; `run-script.sh` points that at `<scratch>/config`. ofx never writes them.
- **The mount list** (F6): ofx polls `/proc/self/mountinfo`, and reads each drive's trash
  (`/mnt/sd/.Trash-1000` here) in the trash view. A GUI test that opens the trash view or counts
  drives sets `OFX_TEST_MOUNTINFO` to a scratch file (run-script passes it on), so it sees only
  `/` and never his drives' trash; Empty Trash in a test with his real mount list would aim at
  `/mnt/sd/.Trash-1000` (the guard refuses it, but don't rely on that). The first f7 run on
  2026-10-25 forgot it and listed his SD trash (read only, nothing changed). Likewise a script
  that types `~` or presses Home sets `OFX_TEST_HOME`, or it browses his real home.
- **Eject** (F6.1) runs `udisksctl unmount` and `power-off` on a removable drive: a test with the
  real udisksctl would unmount and power off his USB drive. `run-script.sh` puts a logging fake
  first on PATH (it fails the step named in `<scratch>/udisks-fail`); the unit test makes its own.
- **The state file** (F6): ofx writes the sidebar's mode, the view (F6.2) and the icon size (F6.3) to
  `$XDG_STATE_HOME/ofx/state`; `run-script.sh` points that at `<scratch>/state`, so a test never
  changes his.
- **The thumbnail cache** (F6.4): ofx reads and writes previews in `$XDG_CACHE_HOME/thumbnails`,
  which is Michael's `~/.cache/thumbnails`, shared with Nemo (and where Nemo's stale-preview loop
  came from, 2026-10-20). `run-script.sh` points `XDG_CACHE_HOME` at `<scratch>/cache`, and the
  unit tests give thumb_get a cache in their root, so no test reads or writes his. Never delete or
  rewrite anything in his cache to test.
- **ofx-clean deletes previews** (F6.5) in `$XDG_CACHE_HOME/thumbnails`, which is his cache unless
  that is set. Michael runs it (his `--dry-run` first, then the timer he enables), not a test.
  Try it on a copy only: copy with `O_NOATIME` (a plain `cp` resets the access times the 90-day
  rule reads, on his whole cache), set `XDG_CACHE_HOME` and `OFX_GUI_ROOT` to the copy's folder.
  The unit tests make their own caches under the root. `make install` writes
  `~/.config/systemd/user/ofx-clean.{service,timer}` and runs `systemctl --user daemon-reload`;
  only Michael enables the timer.
- **ofx changes files since F3:** rename, new folder, trash, delete for good, copy and move. Every
  write in fs/ goes through `ops_allowed` (fs/ops.c), which refuses any path outside
  `OFX_GUI_ROOT` when it is set: `run-script.sh` sets it to the run's scratch dir, the unit tests
  to their mkdtemp root. A guard test aims at a decoy beside that root, never at `/tmp` or a real
  folder, so a mutant that switches the guard off can only reach the decoy. Never start ofx in a
  test on a folder that isn't scratch; `tests/gui/guard3.script` shows the guard refusing there.
- **The trash:** ofx trashes into `$XDG_DATA_HOME/Trash`, or a drive's own `.Trash-$UID` (on his
  `/mnt/sd` that is `/mnt/sd/.Trash-1000`, shared with Nemo). `run-script.sh` points
  `XDG_DATA_HOME` at `<scratch>/data`; the unit tests set it to their root.
- **The clipboard:** ofx sets the system clipboard through `ext-data-control-v1` on Ctrl+C or
  Ctrl+X, in the list and in the edit lines (text, F5.2), which replaces Michael's clipboard. weston has no data-control; the
  `clip` group runs in the test sway (below), whose clipboard is its own, with `wl-copy` and
  `wl-paste` as the other program (a `sh` line's WAYLAND_DISPLAY is the window's, so they reach
  that sway). Never run them in a test without that socket: they would read and replace his. A
  live check against his real clipboard (copy in ofx, paste in Nemo and a terminal) needs his go first.
- The scripts' `sh` lines make and change files: point them at scratch folders only, never his
  home or a work repo.
- **Close All Windows** (F7.1, Ctrl+Q) sends SIGUSR1 to every ofx window in the registry,
  `$XDG_RUNTIME_DIR/ofx/<pid>`: a test with the real registry would close Michael's open ofx
  windows. `run-script.sh` sets `OFX_TEST_WINDOWS` to `<scratch>/windows`, and with the guard on
  (`OFX_GUI_ROOT`) ofx signals no registry outside it; the unit test forks its own children. A
  test that fakes another window registers a `sleep` there with its start time (f7b), never a
  real pid of his.
- **New Window** (F7.1, Ctrl+N) runs `uwsm-app -- <this ofx> <folder>`: through the fake
  `uwsm-app` in a test, so no second window opens.
- **Open With** (F7.3) runs `uwsm-app -- gio launch <app> <files>` and Enter on several marked
  files does too: through the fake `uwsm-app` in a test. The panel reads every installed app
  (the data dirs' `applications/`, read only) and asks the real `gio info` for a file's type
  (it only reads); run-script.sh's `gio` fake hands `info` to the real one, unlogged.
- **`make install` writes `~/.local/share/applications/ofx.desktop`** (MimeType=inode/directory,
  2026-10-26) and runs `update-desktop-database` there: Michael's own folder, so only with his go,
  as every install. Making ofx the folder default is his `gio mime inode/directory ofx.desktop`.
- **Set as default** (F7.3.1) runs `gio mime <type> <app>`, which writes `$XDG_CONFIG_HOME/mimeapps.list`:
  Michael's `~/.config/mimeapps.list` unless that is set. run-script.sh sets it to `<scratch>/config`
  and its `gio` fake logs `mime` and hands it to the real gio, which writes the scratch list; with
  the guard on (`OFX_GUI_ROOT`) ofx refuses a list outside it before gio runs. The unit test uses a
  fake gio. Never run `gio mime` against his own list to test.
- **Bookmarks (F7.5) writes the GTK bookmarks file**, `$XDG_CONFIG_HOME/gtk-3.0/bookmarks`:
  Michael's `~/.config/gtk-3.0/bookmarks`, shared with Nemo and every GTK file chooser, unless that
  is set. run-script.sh points `XDG_CONFIG_HOME` at `<scratch>/config` (f75 makes its own file
  there); the unit test writes under its root; with the guard on (`OFX_GUI_ROOT`) `bm_write`
  refuses a file outside it before making anything. Never add or edit bookmarks in his file to
  test.
- **ofx-fm1 (FileManager1, 2026-10-26) claims a name on the session bus:** run on Michael's bus
  it would take `org.freedesktop.FileManager1` from Nemo for 30 s, and a call would open real ofx
  windows. Never run it outside `make test-fm1` (tests/fm1/run.sh): a private `dbus-run-session`
  whose config lists only the test's service folder, without his `DBUS_SESSION_BUS_ADDRESS`,
  `XDG_RUNTIME_DIR` (where sd-bus would find his bus) or `WAYLAND_DISPLAY`, with fakes for
  `uwsm-app` and ofx (`OFX_FM1_OFX`). It connects with `sd_bus_open`, so a copy the test's bus
  starts uses that bus (`DBUS_STARTER_ADDRESS`). `make install` writes
  `~/.local/share/dbus-1/services/org.freedesktop.FileManager1.service` and reloads his bus's
  config: his go, as every install.
- **A window that stops answering** pops Hyprland's "terminate?" dialog on his screen, even on the
  headless output. The script's `sh` runs under `timeout 5` for that reason; keep everything a
  script waits for short.

## Testing the GUI

Since 2026-10-20 ofx's window tests run in a **private headless weston** (`tools/gui/weston.sh`),
started and stopped by `tools/gui/run-script.sh` for each run: its own Wayland socket
(`ofx-test-<pid>` under `$XDG_RUNTIME_DIR`), never `wayland-1` (Michael's Hyprland). Nothing touches
his Hyprland: no output, no workspace, no dock script, no hyprctl at all.

```sh
tests/gui/run.sh <absolute scratch>          # every script, each in its own weston
tools/gui/run-script.sh <absolute scratch> tests/gui/<script> "$PWD/build/ofx" <folder>   # one
tools/mutation/gui.py <absolute scratch> [--full]   # the GUI mutants (each on its tagged groups)
tests/gui/run.sh <absolute scratch> "$PWD/build/ofx" f62 f6   # some groups only
```

`tools/mutation/run.py` (the unit mutants) runs one worker per core, in two passes: every mutant
under tcc first (a 25 ms build, no sanitizers), then the ones tcc didn't kill, or couldn't build,
under clang with ASan and UBSan as `make test` has them. 609 mutants in 44 s (clang alone 190 s;
gcc, one at a time, about 45 min). Every worker runs the unmutated tests first under both, all at
once, so a test that only fails under load shows before any kill counts. `--clang-only` skips tcc
(and it is skipped without `tcc` on PATH: `sudo pacman -S tcc`). A mutant that doesn't compile tests
nothing: when deleting a statement would leave an `else` without its `if`, empty it instead (`{}`).

The GUI mutants' time is mostly the 86 s GUI suite, not the build. So each runs only the `run.sh`
groups that killed it on a whole-suite run (`tools/mutation/gui-tags.txt`, written by `gui.py`):
164 mutants in 71 min, against about 4 h on the whole suite each; and since 2026-10-26 a mutant stops at its first failure (`OFX_GUI_FAIL_FAST`: the script ends, run.sh skips the group's other scripts), 290 mutants in 881 s against 1591, and with the base in the pool (below) 671 s. Since 2026-10-25 (Michael): a
mutant runs its groups one at a time and stops at the first kill; a new or changed one tries its
hint (a fourth field naming the group, as `'f7'`) and then the rest, and the killing group becomes
its tag; and `--jobs N` (6 by default: on a quiet machine 8 is no faster and 10 or 12 slower, 2026-10-26) runs N at once, each worker in its own copy. The
unmutated suite runs once, its groups the first tasks in the same pool, under the mutants' load (Michael,
2026-10-26; before, in all N workers before any mutant): a script that fails there stops the run and
throws its results away. They build `build/ofx-mut`: clang with ASan and UBSan, Clay's code in
its own object compiled once a run with nanosvg's (`gui/vendor.c`), 1.1 s a mutant against gcc's 3.5 s (Michael,
2026-10-24; tcc has no ASan, so not there). `--full` reruns every mutant past its tags (about
4.5 h before these changes), only before installing the last step of a major milestone, as F6 to F7 (Michael,
2026-10-24); the installs between run the tagged mutants. `gui.py` runs from its scratch copy, scripts included, so editing
`tests/gui` during a run is safe now.

- **Why not a Hyprland headless output any more:** a new output takes a workspace Hyprland picks,
  and that can't be steered (workspace rules with `default` and `persistent` didn't). Three times
  on 2026-10-20 it was one holding Michael's windows (Nemo on 6, Chromium on 2). Never go back to
  `hyprctl output create` for ofx.
- **weston's flags matter** (`weston.sh`): `--fake-seat`, or GLFW 3.4 crashes reading a seat that
  isn't there; `--shell=kiosk` fills the 944x544 logical output, and the window opens at
  `OFX_TEST_SIZE=936x516` (the kiosk shell rejects a larger first frame; GLFW's own decorations
  take the other 8x28). 516 is 490 plus F7.1's menu bar, so under the bar every script has the
  room it was written for. `--width`/`--height` are logical pixels.
- **The test sway** (`tools/gui/sway.sh`, 2026-10-28), for what weston lacks: ext-data-control (the
  `clip` group) and, next, virtual keyboards (`wtype`). `OFX_COMPOSITOR=sway` makes run-script.sh
  use it. Headless (`WLR_BACKENDS=headless`, `WLR_LIBINPUT_NO_DEVICES=1`), `-c` a config written in
  the scratch dir (never `~/.config/sway`), its IPC socket in the scratch dir (`SWAYSOCK`), no
  XWayland. It names its own socket (`wayland-N`, the first free; `wayland-1` is Michael's
  Hyprland), so the config's `exec` writes it down and run-script checks it isn't this session's
  display. The output is 936x516 logical at scale 2: sway draws no decorations, so the tiled
  window is the output, the same size as in weston. weston stays the main suite.
- `run-script.sh` checks the socket is the one weston made before launching, takes the pid from the
  launch script, and stops weston after the run. For an unscripted run (`-`), kill the pid, then
  `tools/gui/weston.sh stop <scratch>`.
- Close a window with SIGTERM (`kill <pid>`; ofx exits through main, so LeakSanitizer runs).
  run-script.sh follows with SIGKILL when a window is stuck on a lock and outlives the SIGTERM.
- `OFX_TEST_LEAK=1` plants a 4 KiB leak: the control that shows LeakSanitizer reports arrive.
- `OFX_GUI_DEBUG=1` logs every key, character and mouse button event.
- `OFX_TEST_CROSS=1` makes a move copy as across drives (a slow move with the delay below).
- `OFX_TEST_ICONS_MAX` shrinks the icon cache (512 slots), so a script can fill it (f62c).
- `OFX_TEST_THUMB_HELD_MAX` shrinks the previews' texture bytes (64 MiB), so a script sees them
  dropped and read back (f64c).
- `OFX_TEST_COPY_DELAY_MS` sleeps per copied block (500 ms at most), so a script sees the progress
  line and can cancel a copy in the middle.
- The script language is at the top of `gui/script.c`. Start every script with `settle`; check
  the `state` line rather than screenshots where you can.

## Vendored code

`vendor/`: Clay (`main` at e6cc369, `clay.h` and its licence) and raylib 6.0 (dbc56a8, `src/` and
`LICENSE`), both as in prdash and uml-viewer, built by our Makefile; nanosvg (`master` at
239e102, `nanosvg.h`, `nanosvgrast.h`, `LICENSE.txt`; zlib licence) for the SVG app icons in Open
With (Michael, 2026-10-26), compiled into gui/main.c. `base/` and `gui/font.c`,
`gui/render.c` are uml-viewer's at cca8e45 (`render.c` gained images). Do not edit vendored files.

## Git

Local only, with no remote yet. Commit and branch freely. Pushing and creating the remote are
Michael's calls.
