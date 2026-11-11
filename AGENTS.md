# ofx: notes for coding agents

ofx is a file explorer for [Omarchy](https://omarchy.org): one folder at a time as a list, in the
Omarchy theme's colours and icons, in C (arenas, static memory, no malloc in our code; Clay;
raylib on native Wayland). `docs/plan.md` has the milestones, the decisions and each milestone's
exit criteria. This file holds what the code can't tell you, above all what a test run could do to
the desktop of the person running it.

## What a test run could do to your desktop

ofx is a real file manager: it launches programs, changes files, and reads and writes the desktop's
shared state. Every test runs against fakes and scratch folders. Keep it that way.

- **ofx launches programs.** Enter on a file runs `uwsm-app -- gio open <file>`, `t` runs
  `uwsm-app -- xdg-terminal-exec --dir=<folder>`. A test that reaches them with the real programs
  on PATH opens real windows. Every GUI run goes through `tools/gui/run-script.sh`, which puts
  logging fakes for them (and `xdg-open`) first on PATH; they write `<scratch>/spawn.log`. The unit
  tests use their own fakes in a mkdtemp folder.
- **ofx opens a window** (it links raylib): never from a plain shell, only in the test compositor
  through `run-script.sh` (below). Started with your session's `WAYLAND_DISPLAY`, it opens on your
  screen. `build/ofx-probe` and `build/test` link no raylib and are safe to run anywhere.
- **The theme:** ofx reads `$XDG_STATE_HOME/omarchy/current/theme`. `run-script.sh` points it at a
  scratch copy (`<scratch>/state`), so a theme test edits the copy. Never run `omarchy-theme-set`
  (or anything else that changes the real theme) to test a switch.
- **The sidebar** reads the GTK bookmarks (shared with Nemo and every GTK file chooser) and the XDG
  user dirs under `$XDG_CONFIG_HOME`; `run-script.sh` points that at `<scratch>/config`. ofx writes
  the bookmarks (F7.5), never the user dirs.
- **The mount list** (F6): ofx polls `/proc/self/mountinfo` and reads each drive's trash
  (`<drive>/.Trash-$UID`) in the trash view. A GUI test that opens the trash view or counts drives
  sets `OFX_TEST_MOUNTINFO` to a scratch file (run-script passes it on), so it sees only `/`. With
  the real mount list, Empty Trash in a test would aim at a real drive's trash (the guard refuses
  it, but don't rely on that). HOME is the run's own too: `OFX_TEST_HOME`, else `<scratch>/home`.
  run-script's trap stops the window and the compositor when a run is interrupted, and
  `tools/mutation/gui.py` runs run.sh in a session of its own so an interrupt reaches it.
- **Eject** (F6.1) runs `udisksctl unmount` and `power-off` on a removable drive. `run-script.sh`
  puts a logging fake first on PATH (it fails the step named in `<scratch>/udisks-fail`); the unit
  test makes its own.
- **Mount** lists unmounted removable drives from `/sys/class/block` and `/run/udev/data`, and a
  click runs `udisksctl mount -b <dev>`. `run-script.sh` sets `OFX_TEST_SYSBLOCK` and
  `OFX_TEST_UDEV` to scratch folders (empty unless the run makes its own, as the `usb` group does);
  its udisksctl fake "mounts" under `<scratch>/media` by writing the test mount list. With the guard
  on (`OFX_GUI_ROOT`), ofx reads neither real folder without them. Never test a mount against the
  real ones.
- **The state file** (F6, F11): ofx writes the sidebar's mode and width, the View toggles, Show
  Hidden, and per folder its view, icon size, sort and cursor to `$XDG_STATE_HOME/ofx/state`;
  `run-script.sh` points that at `<scratch>/state`. Its housekeeping `stat`s the folders it holds.
- **The thumbnail cache** (F6.4): ofx reads and writes previews in `$XDG_CACHE_HOME/thumbnails`,
  shared with Nemo and Nautilus. `run-script.sh` points `XDG_CACHE_HOME` at `<scratch>/cache`, and
  the unit tests give thumb_get a cache in their root. Never delete or rewrite anything in a real
  cache to test.
- **Thumbnailers** (F12 step 3): with `preview-thumbnailers` on, the icon view runs the programs the
  data dirs' `thumbnailers/*.thumbnailer` name on the files it shows, writing their PNG into the
  cache. With the guard on, ofx reads only `OFX_TEST_THUMBNAILERS`'s files, and runs only a program
  inside the guard's root (`ops_program_allowed`). bubblewrap (`/usr/bin/bwrap`) does run for real
  in the tests: it sees only `/usr` (read only), the test's file and its own run folder. The fakes
  that hang sleep 19.<pid> s (f12) or 3.<pid> s (unit), each run's own, as pgrep sees every run.
- **git status** (F12 step 5): in a folder inside a repository, ofx runs `git --no-optional-locks -c
  core.fsmonitor=false status --porcelain=v2 -z --ignored --branch -- .` there (and `git
  for-each-ref` for the branch menu), in bubblewrap (fs/sandbox.c): the repository, its git config
  files and `/usr` read only. The menu reads HEAD's reflog itself, read only.
  **Picking a branch writes:** `git switch <branch>`, with the repository writable and its hooks
  run. With the guard on, ofx switches only a repository inside `OFX_GUI_ROOT` with a test's own git
  (a wrapper among the fakes), on scratch repositories made with `GIT_CONFIG_GLOBAL=/dev/null`, HOME
  the run's. Never pick a branch in a test window on a real repository.
- **ofx-clean deletes previews** (F6.5) in `$XDG_CACHE_HOME/thumbnails`: the real cache unless that
  is set. **View → Clear Previews…** runs `ofx-clean --all`: every preview in that cache. Under the
  guard the window runs only an ofx-clean inside the run's folder (f12b puts a copy among the
  fakes), whose own guard keeps it in the scratch cache. To try it on a real cache, copy it with
  `O_NOATIME` (a plain `cp` resets the access times the 90-day rule reads) and point
  `XDG_CACHE_HOME` and `OFX_GUI_ROOT` at the copy. `tests/clean/run.sh` (in `make test`) points
  `HOME`, `XDG_CACHE_HOME` and `XDG_CONFIG_HOME` into its own scratch folder. `make install` writes
  `~/.config/systemd/user/ofx-clean.{service,timer}` and runs `systemctl --user daemon-reload`; the
  user enables the timer.
- **ofx changes files since F3:** rename, new folder, trash, delete for good, copy and move. Every
  write in fs/ goes through `ops_allowed` (fs/guard.c), which refuses any path outside
  `OFX_GUI_ROOT` when it is set: `run-script.sh` sets it to the run's scratch dir, the unit tests to
  their mkdtemp root. A guard test aims at a decoy beside that root, never at `/tmp` or a real
  folder, so a mutant that switches the guard off can only reach the decoy. Never start ofx in a
  test on a folder that isn't scratch; `tests/gui/guard3.script` shows the guard refusing there.
- **The trash:** ofx trashes into `$XDG_DATA_HOME/Trash`, or a drive's own `.Trash-$UID`.
  `run-script.sh` points `XDG_DATA_HOME` at `<scratch>/data`; the unit tests set it to their root.
- **The clipboard:** ofx sets the system clipboard through `ext-data-control-v1` (Ctrl+C, Ctrl+X,
  Copy Path). weston has no data-control, so the `clip` group runs in the test sway (below), whose
  clipboard is its own, with `wl-copy` and `wl-paste` as the other program. Never run them in a test
  without that socket: they would read and replace your real clipboard.
- The scripts' `sh` lines make and change files: point them at scratch folders only.
- **Close All Windows** (F7.1, Ctrl+Q) sends SIGUSR1 to every ofx window in the registry,
  `$XDG_RUNTIME_DIR/ofx/<pid>`. `run-script.sh` sets `OFX_TEST_WINDOWS` to `<scratch>/windows`, and
  with the guard on ofx signals no registry outside it. A test that fakes another window registers
  a `sleep` there with its start time (f7b), never a real pid.
- **New Window** (Ctrl+N) runs `uwsm-app -- <this ofx> <folder>`, and **Open With** (F7.3) runs
  `uwsm-app -- gio launch <app> <files>`: through the fake `uwsm-app` in a test. Open With reads
  every installed app (read only) and asks the real `gio info` for a file's type; run-script's
  `gio` fake hands `info` to the real one.
- **`make install`** writes `~/.local/share/applications/ofx.desktop` (MimeType=inode/directory)
  and runs `update-desktop-database` there, and the FileManager1 D-Bus service file (below). Making
  ofx the folder default is the user's `gio mime inode/directory ofx.desktop`.
- **Set as default** (F7.3.1) runs `gio mime <type> <app>`, which writes
  `$XDG_CONFIG_HOME/mimeapps.list`. run-script points that at `<scratch>/config`; with the guard on
  ofx refuses a list outside it before gio runs. Never run `gio mime` against a real list to test.
- **Bookmarks** (F7.5) writes `$XDG_CONFIG_HOME/gtk-3.0/bookmarks`; with the guard on `bm_write`
  refuses a file outside it. Never add or edit real bookmarks to test.
- **ofx-fm1 (FileManager1) claims a name on the session bus:** run on a real bus it would take
  `org.freedesktop.FileManager1` from the file manager there for 30 s, and a call would open real
  ofx windows. Never run it outside `make test-fm1` (tests/fm1/run.sh): a private
  `dbus-run-session` that lists only the test's service folder, without the real
  `DBUS_SESSION_BUS_ADDRESS`, `XDG_RUNTIME_DIR` or `WAYLAND_DISPLAY`, with fakes for `uwsm-app` and
  ofx (`OFX_FM1_OFX`).
- **Open at Line** (F9 step 4) starts Omarchy's default editor on a file, through `uwsm-app` (the
  fake in a test). It reads `$XDG_STATE_HOME/omarchy/defaults/editor` and ofx.conf's editor lines,
  all under the scratch dir in a test.
- **ofx.conf** (F10): every GUI start writes `$XDG_CONFIG_HOME/ofx/ofx.conf` when it isn't there,
  and Edit → Preferences… opens it in the editor. So any raylib binary started without run-script's
  env writes the real file. run-script points `XDG_CONFIG_HOME` at `<scratch>/config`.
- **The tabs across a restart** (F11 step 3): with `restore-tabs` on, "Bring back" starts
  `ofx --restore <id>` through `uwsm-app` (the fake in a test). f11j stands in for a second window
  with a `sleep 20` that ignores SIGUSR1, in the run's own registry; f11l and run.sh end it. f11n
  kills its own window (a crash), so run-script reports that run unfinished; run.sh checks the
  state file instead.
- **Network shares** (F16): `make test-net` (tests/net/run.sh) builds a Samba image
  (`localhost/ofx-test-smb:<hash of its files>`) with your podman and runs one container,
  `ofx-test-<pid>`, published on 127.0.0.1 only and removed by the script's trap. It pauses and
  unpauses that container to make a dead server. It mounts the shares through a gvfs of its own:
  `dbus-run-session` with a bus config listing only gvfsd, `XDG_RUNTIME_DIR` and `HOME` under
  `build/net-test`, so gvfsd-fuse mounts there. Never run `gio mount` in a test on the session's
  bus: the share would appear in your file managers and `/run/user/$UID/gvfs`, and a password
  could reach your keyring (the default session config starts a gnome-keyring). podman runs with
  `env -i` and only the real `HOME` and `XDG_RUNTIME_DIR` (tests/net/lib.sh): with the scratch
  ones it starts a stray pause process (`catatonit`) of its own, and under run-script the XDG
  folders are the run's, where podman would put its storage. Never touch a container that isn't
  the run's. The GUI group `net` uses the same library: the share mounted under `<T>/rt` for the
  window's run, `<T>/pause` and `<T>/unpause` for its script.
  **NFS** (F16 step 6) needs a server on port 111, which takes root:
  `sudo tests/net/nfs-server.sh start` runs one (`ofx-test-nfs`, nfs-ganesha, 127.0.0.1 only,
  its export a tmpfs) until `stop`. The NFS checks (test-net's, the net group's netn) skip
  without it. They write only their own files there, and remove them.
  **SFTP** (F16 step 7c): a rootless sshd container (`ofx-test-sftp-<pid>`, tests/net/sftp). gvfs's
  ssh reads the user's own `~/.ssh` (OpenSSH takes the home from the user database, not HOME), so
  the private session's `ssh` is lib.sh's wrapper: the test's own config, known_hosts and keys.
  Never run an SFTP test without it: a new host would be written to your known_hosts. The net group's tree is a
  short folder of its own, `/tmp/ofx-gnet-*` (`<base>/net` links to it), removed once the group
  passes and kept, named, when it fails: remove it once read.
- **A far folder** (F16: a network share, any FUSE mount, by the mount list) is read by `ofx-list`,
  a child (gui/far.c, fs/farlist.c), never by ofx: a read there can block for 90 s, and a thread
  blocked on it would hold the whole process. Its actions (rename, New Folder/File, Trash, Make
  Link, Permissions, undo, a paste's checks, Properties) run in `ofx-list --do` (fs/fardo.c), which
  ofx waits for 1.5 s at most; its writes go through the same guard. A path reaching a far drive
  through local links is judged by `far_real` (fs/list.c), which never looks past the far mount.
  run-script copies the `ofx-list` beside the binary among the fakes (the guard runs no other). The
  `f16` group stands a scratch folder in for a share through its mount list; `net` uses a real one.
- **Connect to Server** (F16 step 4) runs `gio mount <address>` and `gio info <address>`, which on
  your desktop mount a share through your session's gvfs, and hands gio a password on its input
  (`nfs://` too, step 6). run-script's gio fake only logs `gio mount` and `gio info <address>`,
  unless the run gives `OFX_TEST_GIO`: the `net` group's, a gio on that run's own gvfs session (its
  bus checked first). Never test it with the real gio on the session bus, and never put a real
  password in a test.
- **The Network section** (F16 step 5) lists the shares in every `fuse.gvfsd-fuse` mount of the mount
  list (the watch thread, every 2 s, slowing to 30 s while none is mounted: step 7d), and its unmount
  button runs `gio mount -u <share folder>`. With `OFX_TEST_MOUNTINFO` a test's stand-in folder is
  listed instead (f16h); the gio fake only logs the unmount. On your desktop that button unmounts your real share.
- **Servers on the network** (F16 step 7b; since step 7e only when Browse Network asks): ofx runs
  `avahi-browse`, which asks your LAN. Nothing runs it on its own. run-script puts a logging stand-in
  first on PATH that runs `OFX_TEST_AVAHI` when given, else finds nothing; under the guard
  (`OFX_GUI_ROOT`) it doesn't run at all without `OFX_TEST_AVAHI`.
- **A window that stops answering** makes the compositor ask whether to terminate it, on your
  screen. The script's `sh` runs under `timeout 5` for that reason; keep everything a script waits
  for short.

## Testing the GUI

ofx's window tests run in a **private headless weston** (`tools/gui/weston.sh`), started and stopped
by `tools/gui/run-script.sh` for each run: its own Wayland socket (`ofx-test-<pid>` under
`$XDG_RUNTIME_DIR`), never the desktop's. Don't test on a headless output of your own compositor
instead: Hyprland gives a new output a workspace of its choosing, which can be one holding your
windows.

```sh
tests/gui/run.sh <absolute scratch>          # every script, each in its own weston
tests/gui/run.sh -j 6 <absolute scratch>     # the same, 6 groups at once
tools/gui/run-script.sh <absolute scratch> tests/gui/<script> "$PWD/build/ofx" <folder>   # one
tools/mutation/gui.py <absolute scratch>     # the GUI mutants (each on its tagged groups first)
tests/gui/run.sh <absolute scratch> "$PWD/build/ofx" f62 f6   # some groups only
tools/theme-shots.sh <absolute scratch> "$PWD/build/ofx" <out>   # one screenshot per installed theme
```

A run's scratch tree holds about 28,000 inodes: remove it once read (`/tmp` is often a tmpfs).

`tools/mutation/run.py` (the unit mutants) runs one worker per core, in two passes: every mutant
under tcc first (a 25 ms build, no sanitizers), then the ones tcc didn't kill, or couldn't build,
under clang with ASan and UBSan as `make test` has them. Every worker runs the unmutated tests first
under both, so a test that only fails under load shows before any kill counts. `--clang-only` skips
tcc (and it is skipped without `tcc` on PATH). Each run is the fast tests first, then the slow ones
only if those pass (`OFX_TEST_GROUP=fast|slow`); a new test that runs real git or bubblewrap goes in
the slow group (the `if (slow)` calls in tests/test.c's main). A mutant that doesn't compile tests
nothing: when deleting a statement would leave an `else` without its `if`, empty it instead (`{}`).

The GUI mutants' time is mostly the GUI suite, not the build. So each runs only the `run.sh` groups
that killed it on an earlier run (`tools/mutation/gui-tags.txt`, written by `gui.py`), one at a time,
stopping at the first kill (`OFX_GUI_FAIL_FAST`); one its tags don't kill tries its hint (a fourth
field naming the group, as `'f7'`) and then the rest, and the killing group becomes its tag.
`--jobs N` (6 by default) runs N at once, each worker in its own copy. The unmutated suite runs once,
its groups the first tasks in the same pool, under the mutants' load: a script that fails there
stops the run. They build `build/ofx-mut` with clang, ASan and UBSan. `gui.py` runs from its scratch
copy, scripts included, so editing `tests/gui` during a run is safe.

- **weston's flags matter** (`weston.sh`): `--fake-seat`, or GLFW 3.4 crashes reading a seat that
  isn't there; `--shell=kiosk` fills the 944x544 logical output, and the window opens at
  `OFX_TEST_SIZE=936x516` (the kiosk shell rejects a larger first frame; GLFW's own decorations
  take the other 8x28). 516 is 490 plus the menu bar, so under the bar every script has the room it
  was written for.
- **The test sway** (`tools/gui/sway.sh`), for what weston lacks: ext-data-control (the `clip`
  group), virtual keyboards (`keys`) and pointers (`dnd`). `OFX_COMPOSITOR=sway` makes
  run-script.sh use it. Headless (`WLR_BACKENDS=headless`, `WLR_LIBINPUT_NO_DEVICES=1`), `-c` a
  config written in the scratch dir (never `~/.config/sway`), its IPC socket in a folder of its own
  under /tmp (a long scratch path is past a socket's 107 bytes), no XWayland. It names its own
  socket (`wayland-N`, the first free), so the config's `exec` writes it down and run-script checks
  it isn't the session's own display. The output is 936x516 logical at scale 2.
- **Typed keys** (`keys`): `vkbd` (`tools/gui/vkbd.c`) types through a virtual keyboard with a US
  keymap and real evdev keycodes (not wtype: it sends made-up keycodes, and GLFW names keys by
  keycode). A script says `user-keys` first and runs `sh vkbd … &` in the background, as the window
  answers nothing while a `sh` line waits.
- **Drag and drop with other programs** (F14, `dnd`): `vptr` (a virtual pointer through
  wlr-virtual-pointer-unstable-v1) moves the headless sway's pointer, and `dndpeer` is the other
  program, a target or a source window beside ofx; vptr's x and y are the output's logical pixels
  (two windows side by side at 468 each). A drag out is a scripted drag (`press-here`, `move-to`
  past the window's edge) while `vptr … press wait …` holds the compositor's button.
- **vkbd, vptr and dndpeer share one guard** (`tools/gui/testsway.h`): each refuses unless
  WAYLAND_DISPLAY is the test sway's (`$OFX_GUI_ROOT/sway.socket`, its pid a sway). Run against
  your own compositor, vkbd would type into your focused window, vptr move your pointer, and
  dndpeer drop into your windows. A drag out of a real ofx reaches your programs as any drag does;
  test that only by hand.
- The window runs without the session's bus address, with `GIO_USE_VFS=local`, and with
  `XDG_RUNTIME_DIR` the run's own (`<scratch>/xdg-run`), the display named by its absolute path: a
  program with file capabilities (gvfsd-nfs) ignores a bus address and looks for the bus at
  `$XDG_RUNTIME_DIR/bus`. vkbd, vptr and dndpeer aren't built by plain `make`: run.sh's keys and
  dnd groups fail when one is older than its sources (`make build/vkbd build/vptr build/dndpeer`).
- `run-script.sh` checks the socket is the one weston made before launching, takes the pid from the
  launch script, and stops weston after the run. For an unscripted run (`-`), kill the pid, then
  `tools/gui/weston.sh stop <scratch>`.
- Close a window with SIGTERM (`kill <pid>`; ofx exits through main, so LeakSanitizer runs).
  run-script.sh follows with SIGKILL when a window outlives it.
- Test switches: `OFX_TEST_LEAK=1` plants a 4 KiB leak (the control that shows LeakSanitizer
  reports arrive); `OFX_GUI_DEBUG=1` logs every key, character and mouse button event;
  `OFX_TEST_CROSS=1` makes a move copy as across drives; `OFX_TEST_ICONS_MAX`,
  `OFX_TEST_FOLDERS_MEM` and `OFX_TEST_THUMB_HELD_MAX` shrink the icon cache, the remembered
  folders' room and the previews' texture bytes so a script can fill them;
  `OFX_TEST_COPY_DELAY_MS` sleeps per copied block (500 ms at most).
- The script language is at the top of `gui/script.c`. Start every script with `settle`; check the
  `state` line rather than screenshots where you can.

## Where the code is

One unity build per binary: gui/main.c includes base/, fs/ (fs/fs.c) and ui/ (ui/ui.c), then its own
files in order. ui/ is raylib-free and unit-tested (tests/test.c includes it); a function in fs/ or
ui/ that nothing calls makes the test build warn.

- `fs/`: the file system side. `fs/paths.c` holds the path helpers every other file uses
  (path_within, path_below, path_put, path_parent, xdg_dir, mkdir_p, the URI and mountinfo codecs);
  `fs/guard.c` the test guard (`ops_allowed`, `ops_program_allowed`) and the PATH lookups it checks,
  next, so every file after it has them; `fs/theme.c` the theme, and its colours fitted to read on
  their backgrounds (`theme_fit` for the window's frame, `theme_fit_list` for the files area);
  `fs/search.c` the search's address and walk; `fs/editors.c` Open at Line's recipes; `fs/conf.c`
  ofx.conf; `fs/places.c` the sidebar's places; `fs/clean.c` ofx-clean's rules; `fs/sandbox.c`
  bubblewrap's runs; `fs/thumbers.c` the system's thumbnailers; `fs/git.c` git status;
  `fs/farlist.c` what `ofx-list` says about a far folder (F16), `fs/fardo.c` the actions it does
  there (`--do`); `fs/connect.c` Connect to Server's address and what `gio mount` says (gui/connect.c
  runs it); `fs/state.c` the state file (its lock, `state_rewrite`), `fs/folders.c` its
  per-folder lines, `fs/session.c` its window lines.
- `ui/ui.c`: the types (Ui, UiInput, UiAction, UiFocus, UiOnTop) and tables, then `keys.c` (the
  keys: each key's own meaning, the commands, the bindings, the event a key sends; F15), `cursor.c`,
  `match.c`, `find.c`, `edit.c`, `prompts.c`, `menus.c`, `panels.c`, `handle.c` (ui_handle),
  `helpers.c`. An event carries the key itself, which text fields, panels, menus and questions
  read, and its command (`UiEvent.cmd`), which the files and the sidebar read.
- `gui/main.c`: memory, the app state, signals, main(); then `folder.c`, `actions.c`, `icons.c`,
  `files.c` (the list and sidebar drawn), `topbar.c`, `panels.c`, `footer.c`, `frame.c`, `input.c`
  (it includes `glfwkeys.c`, GLFW's keys as ui/'s, which the unit tests take in too, and `drag.c`), `dnd.c` (drags with other programs on the patched GLFW), `clipboard.c`,
  `prefs.c`, `eject.c`, `remember.c`, `restore.c`; the worker threads and the panels' own files.

## At the end of each major milestone

A code-health review of the whole tree's structure before the milestone is released, then the
cleanup it calls for. One step's review doesn't see the drift features leave between them
(copy-paste, flags that can contradict, dead code the tests keep alive). The areas, one reviewer
each: gui/main.c and its files; ui/; fs/ with base/ (every write through `ops_allowed`); the worker
threads, tests and tooling.

## Vendored code

`vendor/`: Clay (`main` at e6cc369, `clay.h` and its licence), raylib 6.0 (dbc56a8, `src/` and
`LICENSE`), nanosvg (`master` at 239e102; zlib licence) for the SVG app icons in Open With. Do not
edit vendored files, with one exception: raylib's GLFW carries `vendor/patches/glfw-dnd.patch`
(F14), since Hyprland sends drags only to GLFW's own data device: version 3, the offers' version 3
events, and `glfwSetWaylandDragHook` handing its drags to gui/dnd.c. A raylib update reapplies it
(`git apply vendor/patches/glfw-dnd.patch`).

`base/`, `gui/font.c` and `gui/render.c` came from uml-viewer (the author's C take on Uncle Bob's
Clojure uml-viewer, 0BSD); `render.c` has gained images since. They are ofx's own now: edit them
as any other file.

## Commits

Conventional Commits (`type(scope): subject`, imperative, lowercase, no trailing period), with a
body that says why.
