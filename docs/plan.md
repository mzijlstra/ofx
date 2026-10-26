# ofx: plan

ofx is Michael's file explorer: a keyboard-first list of one folder at a time, in the Omarchy
theme's colours and icons, written in C on the prdash and uml-viewer stack (arenas, Clay,
raylib on native Wayland). It replaces Nemo, which idles at about 5% CPU, and Nautilus, which
had no "terminal here". The concept, with what is in and out of scope, is the doc "File explorer
concept" (https://claude.ai/code/artifact/8d990266-71ea-4d0e-a109-a8f3a71807ba).

Out for good: drag and drop (Michael doesn't drag files), the "Save as" portal dialogs, network
shares, archives. ("Open with" was here until 2026-10-24, when Michael moved it into F8.)

**Name (2026-10-22, Michael):** fx became **ofx**, Omarchy File eXplorer, for its close ties to
Omarchy's theme: the binary, the folder `tools/ofx`, the window's title and Wayland app id, the
`OFX_*` test variables, the part files' `.ofx-part-` prefix. Review files before it keep their
`fx-*` names. Text below written before then says ofx too.

## Decisions (2026-10-20, Michael)

- Name `ofx`, in `tools/ofx/`, its own local git repo.
- Keys: arrows, Enter and Backspace, and vim-style `h j k l`, `g`/`G` alongside.
- A list view only; an icon grid may come later.

## Milestones

| | What | Exit |
|---|---|---|
| F1 | Browse: one folder as a list (icon, name, size, modified), theme colours and icons, keyboard and mouse navigation, open, terminal here, live refresh | Below |
| F2 | Get around: sidebar (bookmarks, mounts), hidden files, sort, type-to-find, back and forward | |
| F3 | Change files: select, rename, new folder, trash, delete, copy and move with progress, the system clipboard, undo; on scratch folders only in tests | Below |
| F4 | Top bar: back, forward, up; the path as buttons; a location entry toggle; search. Item counts for folders | Below |
| F5 | Tabs: Ctrl+T, Ctrl+W, middle click on a path button or folder | Below |
| F5.1 | The delete question and the name-clash question as a modal panel in the window's middle, the rest darkened | Below |
| F5.2 | The edit lines (rename, location): copy, cut and paste as text, a selection with Shift; the path buttons by keyboard (Ctrl+K) | Below |
| F5.3 | Jobs: up to 4 at once, each a progress row with a throughput line at the bottom of the files; a queue of 32 | Below |
| F5.4 | A theme switch as `omarchy theme set` makes it (the theme folder replaced) followed live, colours and icons; an icon theme that isn't installed falls back | Below |
| F6 | Bottom bar: sidebar as places (Home, bookmarks, Trash; drives with free space), as a tree, or hidden (F9); Trash restore and empty; the status line only; the mode remembered | Below |
| F6.1 | A drive's used space under the pointer; eject for removable drives | |
| F6.2 | Icon view: a grid beside the list, buttons in the top bar, Ctrl+1 and Ctrl+2, remembered | Below |
| F6.3 | Icon sizes (the icon view): a zoom slider at the bottom right, Ctrl+wheel and Ctrl+= / Ctrl+-, remembered | |
| F6.4 | Previews in the icon view: the freedesktop thumbnail cache (shared with Nemo), PNG, JPEG, GIF and WebP made on two threads | |
| F6.5 | Thumbnail cache cleanup: `ofx-clean` on a daily systemd user timer; orphans, 90 days unused, a 1 GiB cap | Below |
| F7 | Menus: File, Edit, View, Go, Bookmarks, Help; New Window as a second process | Below |
| F7.1 | The menu bar and the File menu; New Window (Ctrl+N), Create New Empty File, Close All Windows (Ctrl+Q) | Below |
| F7.1.1 | An empty file opens with the default app for its name's type (gio types it `application/x-zerosize`); F2 selects a name up to its extension; a new name is selected whole | Below |
| F7.2 | Properties: a panel with the file's details and permissions | Below |
| F7.3 | Open With Other Application: a panel listing the apps for the file's type | Below |
| F8 | Right-click menus, with "Open with" (the apps the freedesktop MIME lists name for the file's type) | |

Each milestone ends in a reviewer-2 round, then Michael's go before `make install`.

**Order (Michael, 2026-10-20):** after F3 the priority is a GUI closer to Nemo's (his screenshot of
Nemo: menus, a top bar, tabs, sidebar modes in the bottom bar), in the order F4 to F7. Left out on
his word: Nemo's compact view and its icon size slider.

**Later, wanted eventually, not scheduled:**
- The view (list or icons) per folder, as Nemo keeps it (F6.2 keeps one for all folders).
- Previews of videos and PDFs through the system's thumbnailers (`/usr/share/thumbnailers`), after F6.4.
- Git status per file (`git status --porcelain=v2 -z` off the main thread, a mark per row).
- USB drives (from the concept doc): unmounted drives in the sidebar, a click mounts them with
  `udisksctl mount` (eject came in F6.1).
- A panel with the keys (F6 took the key list out of the status line).
- A key that copies the cursor's path as text (Ctrl+C already puts files on the clipboard, F3).
- Keybindings the user sets (Michael, 2026-10-24: the keys are reaching personal preference).
- Install Nautilus at some point to compare its features with ofx's (not a priority).
- Search through subfolders, as Nemo's search does (the top bar's search opens `/` find for now).
- Optional columns: permissions; type (Michael doesn't want it by default). Date and time format as
  an option; the short form ("14:03", "Oct 20") stays the default.
- Sidebar sections (Nemo's My Computer, Bookmarks, Network): to brainstorm with Michael first.
- Counting in a separate process (reviewer-2, F4 review): a readdir on a hard NFS mount that
  hangs sits in D state, which no thread exit can reap, so ofx's process (its Wayland socket) can
  outlive its window. Only a child process that does the counting decouples a dead mount fully.
- Remember the open tabs across a restart (F5 doesn't).
- A resizable sidebar (the tree's deep levels get little room at 200 px).
- The Trash place's icon full or empty; watching the drives' trashes too.
- Options in `ofx.conf` (Michael, 2026-10-24): previews on or off; the largest image ofx makes a
  preview of (`max_image_size_to_preview`, 10 MiB in F6.4); F6.5's cleanup (on, orphans, the days
  unused, the cap); the menu bar hidden until Alt or F10 (F7, 2026-10-25).
- Properties (F7.2): "apply to everything inside" a folder, its permissions set on the files and
  folders under it (Michael, 2026-10-25: later).
- Cache cleanup (Michael, 2026-10-24): orphans, age and a cap are F6.5. Still later: clearing the
  cache completely, and the cleanup's settings on a settings page.
- The GUI mutants, the next time `gui.py` is worked on (Michael, 2026-10-26): start the slowest
  mutants first. On a quiet machine 46 mutants took about 410 s at 6, 8, 10 or 12 workers alike,
  CPU 42-63%: the time is the unmutated suite at the start plus the slowest mutants at the end,
  not the processor. The order by each tag group's time in the baseline is the first thing to try.
- Open With (Michael, 2026-10-26: not worth it yet, he can't notice the delay): keep the theme's
  app-folder list (0.7 MiB) after the first open, so a later open skips its ~34 ms; the icons
  themselves would still go when the panel closes.
- Not wanted: Nemo's built-in terminal (`t`, a terminal here, covers it).

**Thumbnails, a rule from Nemo (2026-10-20):** Nemo looped at up to 45% CPU on one file
because it checked an x-large thumbnail, found it stale (an older file of the same name), wrote the
new one to large, and checked x-large again, decoding the image several times a second. ofx must
check a thumbnail at the size it writes, and try a file once per change (its mtime), never in a
loop: a failed or stale result is remembered until the file changes.

## F1 design

**Layers, as uml-viewer:** `base/` (types, arenas, strings: uml-viewer's at cca8e45), `fs/`
(the folder, MIME types, icon theme, Omarchy theme, launching: no raylib, unit-tested), `ui/`
(the cursor, scrolling, keys to actions: no raylib, unit-tested), `gui/` (window, drawing, inotify,
the test script). A unity build per binary; no malloc in our code (raylib, GLFW, FreeType,
fontconfig and Mesa allocate for themselves).

**The listing (`fs/list.c`):** `opendir`/`readdir`, then `fstatat` per entry, into an arena that is
reset per folder. Each entry: name, kind (folder, file, symlink to either, broken symlink, other),
size, modified time, mode. Dot-files are left out in F1 (F2 adds the toggle). Order: folders first,
then names in natural order, ignoring case (`file2` before `file10`), as Nemo and GTK sort.
A folder ofx can't read shows the error in the window, not a crash.

**MIME type (`fs/mime.c`):** from file names only, by the shared-mime-info glob tables
(`<data dir>/mime/globs2` for every XDG data dir, weights honoured, literal names before
`*.ext` suffixes before other patterns, case-sensitive matches first), and `generic-icons` for the
fallback icon. No content sniffing in F1: a name with no match is `application/octet-stream`, or
`application/x-executable` when it has an execute bit.

**Icons (`fs/icons.c`):** the freedesktop icon theme lookup. The theme comes from Omarchy's
`icons.theme` (Yaru-magenta here), then its `Inherits` chain from `index.theme`, then `hicolor`.
For an icon name, size and scale: an exact directory match (Fixed, Scalable or Threshold sizes
and Scale), else the closest size, theme by theme; PNG files only (raylib loads PNG; Yaru ships
every size as PNG, so no SVG rasteriser is needed). Names for an entry, most specific first:
the MIME type with `/` as `-`, its generic icon, `text-x-generic` or `application-x-generic`;
folders `folder`, with `user-home`, `user-desktop` and `folder-<xdg dir>` for the XDG user
directories (`~/.config/user-dirs.dirs`). Results cached by name; textures loaded on first draw.

**Theme (`fs/theme.c`):** `$XDG_STATE_HOME/omarchy/current/theme/colors.toml` (default
`~/.local/state/omarchy/current/theme`): background, lighter_background, foreground,
dark_foreground, accent, selection, red. A missing file or key falls back to Tokyo Night (the
current theme's own values). inotify on `current/` and `current/theme/` reloads colours and icons
when the theme changes.

**Launching (`fs/spawn.c`):** `posix_spawnp` with `POSIX_SPAWN_SETSID`, stdin and stdout on
`/dev/null`, children reaped automatically (`SA_NOCLDWAIT`). Open: `uwsm-app -- xdg-open <path>`
(plain `xdg-open` without uwsm-app). Terminal: `uwsm-app -- xdg-terminal-exec --dir=<folder>`, as
`omarchy-launch-terminal` does. **Since 2026-10-24 files open with `uwsm-app -- gio open <path>`**
(Michael): on Hyprland xdg-open is xdg-utils' generic mode, which typed a `.md` by its content
(`text/plain`, nvim, not his Typora for `text/markdown`) and ran a `Terminal=true` app in the
terminal it was called from: from ofx, none, so nothing opened. GIO types by name and runs such an
app through `xdg-terminal-exec`, as Nemo does.

**Keys:** Up/Down and `j`/`k` move; PgUp/PgDn; Home/End and `g`/`G`; Enter, Right and `l` open
(a folder is entered, a file goes to `xdg-open`); Backspace, Left and `h` go to the parent, with
the folder you left selected; `t` opens a terminal in the current folder; `~` goes home; Ctrl+Q
quits. **Mouse:** a click selects, a double click opens, the wheel scrolls one row per notch
(Michael's preference from prdash-tui), the back button goes to the parent.

**Live refresh:** inotify on the open folder only (not recursive), batched 100 ms after the last
event, the selection kept by name (or the row it was on, when that name is gone).

**Idle:** the loop draws only when something changed and otherwise waits in
`PollInputEvents` (uml-viewer's loop, prdash's `docs/gui-findings.md`).

## F5.4 design (2026-10-22)

**Asked (Michael):** when the Omarchy theme changes, ofx changes over to its colours and icons.
That was there since F1 (inotify on `current/` and `current/theme/`), but tested only by an edit
in place. `omarchy-theme-set` builds `current/next-theme`, removes `current/theme` and moves the new
one into its place: `f54` does exactly that on the scratch copy, twice back to back too, then an
edit in place to show the watch moved to the new folder.

**Found:** Omarchy's `vantablack` and `white` themes name `Yaru-gray` and `Yaru-grey`, which no
package ships (upstream Yaru has no grey; omacom/omarchy#7203, #14851 and PRs #13497, #14852,
#14879 are open). ofx then had hicolor alone: no file or folder icons. GTK4 apps crash on those
themes (#14851). **Fallback:** a missing icon theme gives way to its name cut at the last `-`
until one is installed (`Yaru-grey` → `Yaru`), then plain Yaru, then Adwaita (GTK's own, but
mostly SVG, and ofx draws PNGs), then hicolor. No `icons.theme`: the same way.

## F6.5 design (2026-10-25)

**Asked (Michael):** a cleanup of the thumbnail cache, with settings for later (a settings page):
manage the cache (on), delete orphaned previews (on), delete previews not used for a while (90
days), and a size cap that deletes the least recently used first.

**Measured here (2026-10-25, reading only; previews opened with `O_NOATIME`):**
- `~/.cache/thumbnails`: 362 MiB. `normal` 73, `large` 2936, `x-large` 3637, `fail` 1396 (empty
  markers, Nemo's).
- Orphans (the file named by `Thumb::URI` is gone): 4127 of 6646 previews, 258 MiB. For 3387 the
  file's folder is gone too. Most under `~/Documents` (2194), `~/Pictures` (650), `~/Dropbox`
  (331), `~/Downloads` (323). 152 are under `/run/media/mzijlstra`: drives not plugged in, not
  orphans. 2 are stale (another mtime), 70 have no `file://` URI.
- Reading every preview's header takes 0.12 s.
- Access times: `/home` is btrfs with `relatime`, so a read updates a file's atime at most once a
  day; enough for a rule in days. A read by anyone counts. Every preview in the size folders
  (6518) was read at 2026-10-24 12:38:05Z, within one second (reviewer-2): that was this
  session's own plain `open()` of each, measuring the cache for F6.4. So no preview there ages out
  before 2027-01-22. The 1314 entries older than 90 days are all `fail` markers. Opening with `O_NOATIME` leaves the atime alone (checked); a plain
  open by the cleaner would mark every preview it checks as just used.

**Decided (Michael, 2026-10-25):** a systemd user timer, daily; fixed defaults until the settings
page (on, orphans on, 90 days, 1 GiB); it is F6.5.

**The design:**
1. **`ofx-clean`**, a program of its own beside `ofx-probe`: `fs/clean.c` (no raylib, unit-tested)
   and `tools/clean.c` (its main). It opens no window and is safe to run anywhere.
2. **What it looks at:** the files in `<cache>/thumbnails/{normal,large,x-large,xx-large}` and in
   each `fail/<program>/` folder, nothing else: names of 32 hex digits plus `.png`, and ofx's own
   `.ofx-<pid>-<n>.png` left by a crash. Everything is `lstat`ed, nothing followed: a symlink or a
   folder in there is left alone. Each delete is an `unlinkat` in that folder's fd, behind
   `ops_allowed` (so `OFX_GUI_ROOT` confines a test). The cache is `$XDG_CACHE_HOME` or
   `~/.cache`; a relative `XDG_CACHE_HOME` is refused.
3. **The rules, in this order:**
   - **Not used for 90 days:** atime older than 90 days, from `lstat` alone (the `fail` markers go
     this way; Nemo tries such a file once more if it meets it again). ofx's leftover temp files go
     after a day.
   - **Orphans:** the header read with `O_NOATIME | O_NOFOLLOW`, its tEXt chunks parsed as
     `fs/thumb.c` does. A `file://` URI, unescaped, whose `lstat` answers `ENOENT` or `ENOTDIR` is
     an orphan; one whose `Thumb::MTime` differs from the file's is stale; both go. Any other error
     keeps it. Kept, never judged: a path on a network or FUSE mount (by mountinfo, as
     `thumb_local`, with no `stat` that could hang), and a path on a drive not mounted now: under
     `/run/media/<user>/<name>`, `/media/<name>` or `/mnt/<name>` when that folder isn't a mount
     point in mountinfo. A preview whose header can't be parsed, or with another URI scheme, is
     left to the age rule.
   - **The cap, 1 GiB:** if what is left (by `st_size`) is over it, the least recently read (atime)
     go first until it is under.
4. **Output:** one line for the journal: what went by each rule, the MiB freed and left.
   `--dry-run` lists each file with its rule and size and deletes nothing.
5. **The timer:** `ofx-clean.service` (oneshot, `Nice=19`, `IOSchedulingClass=idle`) and
   `ofx-clean.timer` (`OnCalendar=daily`, `Persistent=true`, so a missed day runs at the next
   login, `RandomizedDelaySec=1h`). `make install` copies both to `~/.config/systemd/user` and
   reloads, but doesn't enable the timer: Michael runs `ofx-clean --dry-run` on his cache first,
   reads the list, then enables it (`systemctl --user enable --now ofx-clean.timer`). "Manage the
   cache" is the timer being on until the settings page has the switch.
6. **Defaults:** constants in `fs/clean.c` (orphans on, 90 days, 1 GiB); the settings page and
   `ofx.conf` set them later.

**Tests:** unit tests (`tests/test_f65.c`) on a scratch cache with fixture previews made by
`thumb_put`: fresh, orphan, file in a deleted folder, stale mtime, a path under a drive not
mounted and under a network mount (a fake mountinfo), a `trash://` URI, an unparseable PNG, old
atimes set with `utimensat`, a cap test with a smaller cap, a symlink and a folder in the cache, a
name not of the pattern, and the guard refusing a cache outside the root. Each kept file's atime is
checked unchanged after a run. Unit mutants for every rule. No GUI code changes, so no GUI mutants.
No test reads or writes Michael's cache; the first run on it is his `--dry-run`.

**As built (2026-10-25):**
- `fs/clean.c` (`clean_run`) and `tools/clean.c` (`build/ofx-clean`, and
  `build/release/ofx-clean` for the install); `tools/systemd/ofx-clean.{service,timer}`.
- On a copy of Michael's cache (copied with `O_NOATIME`, his left untouched: 8042 files, no atime
  or mtime changed): 1314 unused (the `fail` markers), 3991 orphans, 8 stale, 243.6 MiB of 362
  would go, 2729 kept (98.0 MiB); 0.16 s for the dry run, 0.25 s for the run, then nothing on a
  second run. Each orphan and stale verdict checked again by a separate script: 0 wrong, none
  under `/run/media`. The stale ones: 6 `fail` markers and 2 previews, all of files with another
  mtime.
- A test found a read past the end in the URI decoder on a cut escape (`%2` at the end); fixed
  before anything ran on real files.
- **reviewer-2 (2026-10-25), MEDIUM:** Michael's `~/Documents`, `~/Pictures`, `~/Downloads`,
  `~/Dropbox`, `~/Desktop` and `~/Videos` are links onto `/mnt/sd`, a removable card. The drive
  rules saw only the `/home/...` path, so with the card out the previews of 3037 files that exist
  would have gone as orphans. Fixed: `clean_real` follows the links (as far as they lead, a
  dangling one included), and the drive rules judge that path. It stops at a network or FUSE mount
  before a `readlink` there could hang, and keeps the preview. Measured on a copy with a mount list
  without `/mnt/sd`: 202 orphans (none on the card, all files that are gone), against 3919 before.
  **LOW:** a link's preview carries its target's MTime and Size, so `stat`, not `lstat`.
  Following links costs time: reviewer-2 measured 664 to 943 ms for the dry run at `9fe78b7`,
  against 162 to 238 ms before, on a loaded machine: each prefix of each path read mountinfo's text
  again (about 66,000 times). Since 2026-10-25 (Michael's call) `clean_mounts` parses it once a run
  into a table, each mount point carrying thumb_local's verdict on it, and `clean_local` and
  `clean_mounted` look paths up there: 663 to 684 ms down to 153 to 164 ms on the same copy (five
  runs each, alternated), with the same lists on both mount lists.
- Unit tests: `tests/test_f65.c`; `make test` went from 2933 checks to 3288 (3312 with the mount table). Mutants: 58 for
  `fs/clean.c` (69 after the review, 77 with the mount table), 0 survived. Two left out as inert or equivalent, each with its reason in
  `mutants.py`.

## F7 design (2026-10-24)

**Asked (Michael):** menus as Nemo has them: File, Edit, View, Go, Bookmarks, Help.

**Parts (Michael, 2026-10-25):** F7.1 the menu bar and the File menu; F7.2 Properties; F7.3 Open
With Other Application; then the Edit, View, Go, Bookmarks and Help menus (F7.4 on, still to
design). Properties first: it is self-contained (statx, rename since F3, chmod through the guard)
and builds the panel parts Open With needs (a text field, checkboxes, a scrolled list); Open With is
mostly the freedesktop MIME plumbing (`.desktop` files, `mimeinfo.cache`, parent types,
`mimeapps.list`), which F8's right-click menu shares, so F7.3 leads into F8.

**Decided for F7.1 (Michael, 2026-10-25):**
- **The menu bar** is always shown, one row above the top bar. Hiding it until Alt or F10 is an
  `ofx.conf` option (later list).
- **Keys:** F10 opens File; Alt+F, Alt+E, Alt+V, Alt+G, Alt+B, Alt+H open their menu; Up/Down
  (`k`/`j`) move, Left/Right go to the next menu, Enter picks, Esc closes; with a menu open, the
  pointer over another title opens that one. Each entry shows its key at the right.
- **The File menu:**

  | Entry | Key | Grayed when |
  |---|---|---|
  | New Tab | Ctrl+T | never |
  | New Window | Ctrl+N | never |
  | Create New Folder | Ctrl+Shift+N | the trash view; a folder ofx can't write to |
  | Create New Empty File | none | as Create New Folder |
  | — | | |
  | Open | Enter | nothing selected |
  | Open With Other Application… | | until F7.3; then nothing selected, or a folder |
  | — | | |
  | Properties… | | until F7.2 |
  | — | | |
  | Close All Windows | Ctrl+Q | never |
  | Close | Ctrl+W | never |

- **Create New Empty File** works like Create New Folder: an empty file "New file" (a number added
  when taken) is made and named in the row; Ctrl+Z removes it.
- **Close** closes the tab, the last one the window, as Ctrl+W does now.
- **Close All Windows takes Ctrl+Q** (Nemo's key for it): every ofx window closes as Ctrl+Q closes
  one today, so a window with jobs running or waiting asks first, in itself. Ctrl+Q in a window
  that is alone does what it did.
- **Finding the other windows: a registry.** Each window writes `$XDG_RUNTIME_DIR/ofx/<pid>` at
  start and removes it on exit; Close All asks each listed pid that is still an ofx (its
  `/proc/<pid>/exe`) to close, by a signal its main loop turns into Ctrl+Q. A leftover entry from
  a crash is removed when found dead. Not a search of every running ofx: `run-script.sh` points
  the registry at the scratch dir, so a test can never close Michael's windows.

**Decided (Michael, 2026-10-24): New Window is a second process.** File → New Window (Ctrl+N) runs
`uwsm-app -- ofx <current folder>`, the way ofx launches other programs. There is no window that
one process controls besides its own: raylib keeps one window and one GL context in global state,
so that would mean patching vendored raylib or leaving it, for no gain here.
- What the processes share already: the clipboard (`ext-data-control-v1`, `fs/clipboard.c`), so
  Ctrl+C in one window and Ctrl+V in another works as it does with Nemo; the thumbnail cache
  (writes renamed into place); the state file (also renamed into place, never half written).
- What it costs: each window is a whole process, about 140 MiB RSS (F6.4's measure: 139 MiB
  started, mostly Mesa and GL). Each has its own job queue, so two windows can run 8 jobs.
- What it gains: a crash or a hang takes down one window only; closing one leaves the other's
  jobs running. Hyprland tiles each window on its own anyway, and SUPER+SHIFT+F works this way.
- **To fix with it:** `state_save` writes every field from the window's own memory, so window B's
  save undoes window A's change to a field B didn't touch. A save re-reads the file and changes
  only the field that changed.
- Tests: the fake `uwsm-app` in `run-script.sh` logs the command; a script checks `ofx <folder>`
  in `spawn.log`. A unit test for the state merge (two saves of different fields keep both).
- F8 reuses it: "Open in New Window" on a folder's right-click menu.

**F7.1 as built (2026-10-25):**
- `ui/ui.c`: the menus as tables (`UI_MENU`, `UI_MENU_ITEM`: label, key, a word for the tests);
  `ui_menu_enabled`, `ui_menu_event` (the keys), `ui_menu_hover` (the pointer), the clicks in
  `ui_handle`. A menu open takes the typed characters (`t` would start a terminal behind it) and
  Tab; any other key closes it and does what it does (Ctrl+T opens a tab). A question up closes
  it. `gui/main.c` draws the bar (26 px, `MENU_H`) and the menu as a floating panel under its
  title; with a menu open, hit tests see only the titles and the entries, and a press anywhere
  else only closes it.
- Two changes from the design above: **New Window is greyed in the trash view** (ofx starts on a
  folder, and Ctrl+N there says so, as other keys do), and the registry tells a window by its
  **start time** (`/proc/<pid>/stat`'s 22nd field, written in its entry), not by
  `/proc/<pid>/exe`: an ofx replaced by `make install` reads "… (deleted)" there, yet is a window
  to close. A pid used again has another start time; an entry being written (empty) is skipped.
- Ctrl+Q: the others get SIGUSR1 (`fs/windows.c`, `win_signal_others`); this window quits at once,
  or asks while its jobs run, in the same frame. SIGUSR1 at a window is its own close (the
  compositor's), asked first with jobs. The menu's entry does the same.
- New Window: `uwsm-app -- <this binary> <folder>` (`/proc/self/exe`; "ofx" from PATH when the
  binary was replaced since), so a build in the repo starts the same build.
- Create New Empty File: `op_mkfile` (`O_EXCL`, "New file (2)" when taken), `UNDO_MKFILE`, whose
  undo removes the file only while it is still empty (a file written to since is kept, said so).
- The state: `state_put` re-reads the file and changes one key, keeping the rest (a key ofx
  doesn't know too); the sidebar, the view and the zoom each save their own.
- Tests: `tests/test_f7.c` (the keys, the clicks, what is greyed, Close All with and without jobs,
  the new file and its undo, `state_put` with two windows' saves, `spawn_window`, the registry with
  forked children: live, a reused pid, a dead one, junk names, the guard). GUI: `f7` (the menu by
  keys, raw GLFW keys and the pointer, New File, New Window, New Tab and Close, a read-only folder,
  the trash view, a question up) and `f7b` (Close All: a registered `sleep` signalled, stale
  entries removed, this window asking with a copy running, SIGUSR1 at it, the menu's entry). The
  test weston output grew to 944x544 and the window to 936x516, so the area under the menu bar
  is what every script had before. `make test` 3478 checks; unit mutants 850, 0 survived (three
  left out with reasons in `mutants.py`).
- reviewer-2's SHIP at `1992f0a` (2026-10-25) had two LOWs, landed after it:
  - **A close lost to a click:** gui/ queues UK_QUIT once (the window's close, another window's
    Close All), and `ui_handle`'s early returns (a click, a menu, the queue-full note) dropped
    it. `ui_handle` now takes it first. So the queue-full note no longer eats it: F5.3's test
    pinned that from when UK_QUIT was Ctrl+Q; it now asks (reviewer-2's recommendation).
  - **The registry reader** took names such as `+pid` and `0pid`, bodies with more after the
    number, and blocked on a FIFO. Now: names as written (digits, no leading 0), a regular file
    opened `O_NONBLOCK`, the body digits and a newline, and the signal through a pidfd taken
    before the start time is checked.
  - `make test` 3511; unit mutants 861, 0 survived (one more left out: `now < 0`, race only).

**F7.1.1 (Michael, 2026-10-25), after trying F7.1:**
- **An empty file wouldn't open:** `gio open test.txt` (empty) said "Failed to find default
  application for content type 'application/x-zerosize'": GIO types every empty file by its
  content, whatever its name (checked with `gio info`), and nothing here opens x-zerosize (Nemo
  has the same). Michael's pick: go by the name, not a mimeapps.list line making every empty file
  text. `fs/apps.c` finds a type's default app as the MIME apps spec has it: the lists
  (`<desktop>-mimeapps.list` before `mimeapps.list`; config home, config dirs, data home, data
  dirs), per type its [Default Applications], then [Added Associations] less [Removed
  Associations], then each `mimeinfo.cache`; then its parents (`mime/subclasses`, text/* under
  text/plain); a list may name a type by an alias (`mime/aliases`); an app is its first desktop
  file found, and isn't there when that is `Hidden=true` (Michael's
  `~/.local/share/applications/java-java-openjdk.desktop` is) or its `TryExec` is missing. An
  empty file opens with `uwsm-app -- gio launch <desktop file> <file>`; no default, `gio open` as
  before; a file with content always `gio open`. Checked against `gio mime` on Michael's own lists
  for 557 types: the same app for 556; the one apart is an unknown text/ type, which ofx gives
  text/plain's app (gio none). F7.3's Open With builds on it.
- **F2** selects a file's name up to its extension (`.tar.gz` as one), a folder's whole; a new
  folder or file opens with its whole name selected.
- reviewer-2's SHIP at `b83d128`: `gio mime` over 1,619 types and four desktops differs on 9, all
  text/* types without text/plain in their chain (ofx gives the text editor, gio none: kept, for
  empty files). LOW, landed: an app must be `Type=Application` (gio skips a Link). For F7.3: gio
  reads the lists per dir (a dir's added associations, then its removed ones, then its cache; a
  removal reaches only what comes after), which differs from `apps.c` when [Removed Associations]
  are used; Michael's lists have none. gui.py names mutants that didn't build or run, and fails.

**F7.2 Properties (Michael, 2026-10-25):** a panel in the window like the Delete panel (one
process, one window), one page; only for showing a file's properties and changing its permissions
(no name field: F2 renames).
- **Opened by** Ctrl+I (Nautilus's key) or File → Properties. (Alt+Enter, Nemo's, opens a
  terminal here instead, as `t` does: Michael's herdr and Omarchy bind Alt+Enter and Super+Enter
  to terminals.)
- **What it shows:** the marked entries, or the cursor's; in an empty folder, that folder. One
  entry: its icon (or preview), name, size in bytes and short (a folder: "12 folders, 340 files,
  1.2 GiB", counted in the background, filling in as it goes, not on a network drive), location
  (the parent, `~` for home), the drive, modified, accessed and created as full date and time
  (`2026-10-25 14:03:12`; created from statx), a link's target. Owner and group, shown. Several:
  "3 folders, 12 files — 845 MiB" (folders counted all the way down) and their common location,
  no permissions. Not shown: the type, free space.
- **Permissions:** read, write and execute for owner, group and others as a 3 by 3 grid of
  checkboxes, and the octal number (special bits in it, kept by a change). A change applies at
  once; Ctrl+Z after closing the panel puts the old mode back. Greyed (shown, not changeable)
  when the file isn't Michael's, in the trash view, and for several entries. A link shows and
  changes its target's (chmod follows links); the guard checks the target's real path, so a link
  in a test's scratch tree can't reach a file outside it.
- **Keys:** the focus starts on the grid (owner's read); arrows and h/j/k/l move it, Space or
  Enter toggles, Esc (or Ctrl+I again) closes. The pointer: a checkbox toggles, Close closes.

**F7.2 as built (2026-10-25):**
- `fs/props.c`: `du_walk` (files, folders and bytes under some roots; links not followed, other
  drives counted not entered, past 256 levels cut; stops when its generation moves; a tick every
  512 entries), `op_chmod` (the 0777 part, special bits kept; the guard on the real path, so a link
  can't reach outside the test tree), `perm_bit`. `fs/undo.c` `UNDO_CHMOD` (the old mode, octal).
- `gui/walk.c`: one walk thread, as counts.c's (a generation per walk, the panel closing stops it,
  never joined when stuck); `gui/main.c` the panel (`props_open`, `props_read`, `props_chmod`,
  `layout_props_panel`); `ui/ui.c` `ui_props_open`, `ui_props_event`, the clicks. A clash or delete
  question over the panel takes its keys; quit and the queue-full note as everywhere.
- Ctrl+I in the rename or location line is the line's (as every key there); the menu closes those
  lines before it opens the panel.
- Tests: `tests/test_f72.c`; GUI `f72` (a file, the keys and the pointer, undo, a folder counted,
  a link changing its target, a link outside the tree refused by the guard and checked unchanged
  by run.sh, root's file and a broken link greyed, several, an empty folder, the menu, Alt+Enter,
  the trash view). Unit mutants 939, 0 survived (four left out with reasons: the other-drive rule,
  which needs a mount; `realpath`'s check; UNDO_CHMOD in undo_what; and F7.1's).

- reviewer-2's SHIP at `b388295` (2026-10-26): LOW, landed: a permission change's undo now
  records the file's device and inode and refuses another file put at that path since (a link to
  another file, in their harness, got that file's mode back). Decisions for Michael: Down from a
  column the short last row lacks goes to the last cell (Nemo's rule, kept from F6.2), so Up and
  Down aren't inverses there; a hard link counts once per name in the panel's sums (du dedupes:
  +0.16% on /usr/share). Checked: du_walk equals `find -xdev` on /usr/share.
- After the review (Michael, 2026-10-26): Nemo's rule kept for Down over a short last row. Hard
  links: both totals, a "Hard links" line ("5 names of 2 files; … counting each once") when there
  are any, a file's own names on its own; each linked file once by device and inode, up to 65,536
  of them (past that, "at most"). A "MIME type" line for one entry: gio's (`gio info`, the content
  read, so an empty file is `application/x-zerosize`, as `gio open` sees it) with its words from
  shared-mime-info; the name's type, said, on a network drive or when gio doesn't answer in 1 s.

**F7.3 Open With Other Application (Michael, 2026-10-26):**
- **The panel:** a filter field on top, then the list in three parts: the default app for the
  file's type; Recommended (others that name the type or a parent type; `NoDisplay=true` apps only
  here); Other (every other app, by name). Each row the app's icon and name. Launched as
  `uwsm-app -- gio launch <desktop file> <files>`.
- **The filter:** any part of the app's displayed name (not its desktop file name), case folded;
  three toggles inside the field's right end as VS Code's: `Aa` match case (Alt+C), `ab` whole word
  (Alt+W), `.*` regular expression (Alt+R; a bad one shows a red border and the error under the
  field). The same three in find (`/`, Ctrl+F), one matcher for both.
- **Keys:** the focus starts in the field. Tab and Shift+Tab move between the field and the list.
  In the field: typing, Left/Right, Home/End and Backspace edit, Down goes to the list, Enter opens
  the highlighted app. In the list: Up/Down and j/k move, Up on the top row goes to the field,
  Home/End, PgUp/PgDn; another letter goes to the field and is typed there. Esc closes. Pointer: a
  click selects a row, a double click opens.
- **Several files:** all of them in one launch. And Enter on several marked files groups them too:
  files with the same default app go in one `gio launch` (one viewer for five images).
- **Folders:** allowed (VS Code and Nemo list `inode/directory`).
- **Set as default** (F7.3.1): "Always use for <type>", the type gio sees (what opening uses; an
  empty file's `application/x-zerosize` is named as such). Written by `gio mime <type> <app>`
  into `~/.config/mimeapps.list`; a type that `hyprland-mimeapps.list` (read first) sets is said,
  not silently shadowed. Ctrl+Z puts the old default back (no old one: ofx removes the line).

**The find bar (Michael, 2026-10-26, after F7.3.1):** find in a bar of its own, over the job rows
and the status bar, as wide as the files (36 px, its room taken from the list), with the field, the
three toggles, "No match" or a bad expression's reason, and a ×. His choices: it stays open until
Esc (the cursor back where the find began) or the × (the cursor kept); Enter gives the keys back
to the list; in the bar Up and Down go to the previous and next match; Ctrl+J and Ctrl+Down go
list, bar, job rows, list. A command typed in the bar gives the keys back and is the list's; the
menu, the edit line and the path buttons take the keys and leave the bar open; a tab change
closes it. `/` or Ctrl+F with the bar open starts a new find from the cursor; the search button
opens and closes it. (`Ui.find_keys`; `ui_find_leave` against `ui_find_close`.) reviewer-2's review
at `91b4d98`: MEDIUM, landed: with no match the cursor went back to where the find began, not
kept inside a list listed again shorter, so Enter opened past its end (ASan in weston); now both
that and Esc clamp it (`ui_find_back`). LOW, landed: a click in the bar's field with the rename
line open left the keys with the line; it closes now.

**The find bar's count and chevrons (Michael, 2026-10-26):** as VS Code's: "2 of 5" beside the
field (the cursor's place among the matches; "5 matches" with the cursor off them; "No match"),
then ⌃ and ⌄ for the previous and next match, round the list as Up and Down, greyed and inert
under two matches, tooltips "Previous match (Up)" and "Next match (Down)". A chevron leaves the
keys where they are. `ui_find_count` scans the listing; gui/ counts again only when the listing,
the text, its toggles or the cursor change (`find_count`), not on a hover's frame. Shift+Enter
stays as Enter.

**ofx as the folder handler (Michael, 2026-10-26, dogfooding):** SUPER+SHIFT+F runs ofx (his
bindings.lua). `make install` adds `ofx.desktop` (`Exec=ofx %f`, `MimeType=inode/directory`), so
ofx can be the default for folders that other programs open (`xdg-open ~`, `gio open`). Started
on a file, ofx opens its folder with the file selected. Not yet: browsers' "Show in folder" asks
`org.freedesktop.FileManager1` on D-Bus, which Nemo provides (nemo.FileManager1.service); that
stays Nemo's until ofx answers it.

**Shift and a move (Michael, 2026-10-26):** in the files, Shift with an arrow, Home, End, or
H/J/K/L selects every entry the cursor passes over, the start included, as a Shift+click does
(ofx's selection only grows that way: going back unmarks nothing; Esc clears). The icon view
marks in the list's order, a row down being the cells between. Not in the sidebar, the find bar
(Shift+Down is its next match, capitals are text), or a question (K keeps all in a clash).

**F7.3.1 as built (2026-10-26):** a box under Open With's list, "Always open <type> files with this
app" (Alt+D or a click), clear at each opening. Enter or Open with it ticked runs
`gio mime -- <type> <app>` (GLib adds the line to [Default Applications] and [Added Associations]
of `$XDG_CONFIG_HOME/mimeapps.list`, the rest of the file as it was: checked on a copy of
Michael's), checks the list says so, then opens. Greyed with the reason for an empty file's type
(`application/x-zerosize`, every empty file's) and for a type a list read before the user's sets
(`hyprland-mimeapps.list`: the change would do nothing). No undo (Michael). A folder's type can be
set: ofx goes into folders itself, other programs would open the new default.
reviewer-2's SHIP at `6c60ef7` (2026-10-26), three notes, landed: plain-text find folds past
ASCII too (towlower in C.UTF-8: é finds É, as the regular expression does); a type is set (and
checked) by its canonical name, not an alias gio info may give (.p12, .p7b); an SVG over 1 MiB isn't
drawn (a 10 MB one took 1.4 s at 48 px on the main thread; real icons are a few KiB). Its checks:
Default and Recommended equal GIO's; GLib's rewrite of a copy adds the two lines only.

**F7.3 as built (2026-10-26):**
- `ui/ui.c`: one matcher (`UiMatcher`, `ui_match`) for find and the filter: case folded or as
  typed, whole words (no letter, digit, `_` or byte past ASCII on either side; with a regular
  expression each match is tried until one stands alone), POSIX extended expressions (`regcomp`;
  a bad one matches nothing and its reason shows in red). Find's toggles live in the find line
  and are kept for the next find and for `n`/`N`. The panel (`ui_ow_*`): its field (a caret,
  Left/Right/Home/End/Backspace/Delete by code point), the list (`UiList`), the keys and pointer
  as decided; Enter with entries marked opens them all (`UiAction.all`).
- `fs/apps.c`: `apps_all` (every app of the data dirs' `applications/`, three folders deep, a
  desktop id's first file the app, unusable ones left out) and `apps_rank` (the type's default,
  then those whose MimeType= names it, a parent or an alias, or that a list's Default or Added
  Associations names, less Removed Associations; then the rest; NoDisplay apps only when
  recommended; each part by name, case folded). The type is the first file's, as gio has it.
- `fs/spawn.c`: `spawn_launch_all`, up to 256 files a launch (more go in further launches).
- `gui/openwith.c`: the panel (`ow_open`, `ow_launch`, `layout_ow_panel`, ten rows, fewer in a
  small window; a part's name on its first row) and `open_marked` (Enter on several: grouped by
  each file's default app for its name's type; those without one through `gio open` each; folders
  left, and with only folders marked Enter goes into the cursor's as before). Shift+Tab is Tab.
- Tests: `tests/test_f73.c`; GUI `f73` (the menu, the filter and toggles, the keys, Shift+Tab, the
  pointer, several files, a folder, Enter on several, find's toggles); `f7` updated (Open With is
  enabled now).
- reviewer-2's SHIP at `24b475a` (2026-10-26): LOW, landed: a whole-word expression now tries
  every span between two word boundaries (the longest match from a start could run past one a
  shorter match stops at: `-*-` in "--_ "); `^` and `$` still mean the name's ends. 0 of 30,000
  random cases disagree with a span oracle, but for `.` on a character past ASCII: ofx runs in the
  C locale (no `setlocale`), so `.` is a byte and case folding is ASCII, as for plain text.
  Decision for Michael: "Recommended" holds the parent types' apps too, which GTK's chooser lists
  apart as "Related" (broader than GTK's on 629 of 1,477 types).
- After Michael's try (2026-10-26): "Related" apart (his pick): Recommended names the type or an
  alias, Related a parent type (a NoDisplay one stays out of it). Regular expressions run in
  C.UTF-8 (`uselocale` around regcomp and regexec only): `.` is a character, case folds é/É;
  plain text still folds ASCII only. Two bugs he found: the list was empty until a key or click
  (it was filtered on the frame after the panel opened, and an idle window draws no other: now at
  once, and f73 checks that frame), and every app had the generic icon (the theme's Apps folders
  weren't read; now they are, +6 ms at start, with pixmaps/ and an Icon= PNG path). An SVG-only
  icon (Omawrite, Omacalc, Foliate) still gets the generic one: ofx draws PNG.
- Then (Michael): the apps' icons only while the panel is up, and SVG too. The theme's app folders
  are read when it opens (`icon_load_apps`, into its arena: about 34 ms, most of it the themes'
  index.theme files read again; start-up is back to about 51 ms), each app's texture when its row
  is first drawn, all of them dropped when it closes (`ow_drop`). SVG icons (theme, pixmaps/ or a
  path) are drawn with nanosvg, vendored; only for the apps, the files' icons stay PNG.

## F6.4 design (2026-10-24)

**Asked (Michael):** previews in the icon view, from the freedesktop thumbnail cache shared with
Nemo, PNG and JPEG made on a thread.

**Measured here (2026-10-24):**
- `~/.cache/thumbnails`: `normal` 15 files, `large` 2918 (85 MiB), `x-large` 3637 (270 MiB),
  `fail/gnome-thumbnail-factory` 1396. Nemo writes PNGs with `Thumb::URI` and `Thumb::MTime`
  (seconds) and `Software`, as the spec has it. Many are of PDFs and slides, which ofx can't make.
- Nemo here: `show-image-thumbnails 'local-only'`, `thumbnail-limit` 1 MiB, so Nemo makes no
  preview for most photos.
- Images under `~` and `/mnt/sd`: 22399 PNG, 8501 JPEG, 4063 GIF, 677 WebP, 5 HEIC (SVGs left
  out: mostly icons in repos).
- One photo, to a 256 px preview, one core: stb_image (raylib's) decodes in 60 to 160 ms, then
  13 to 86 ms to shrink. libjpeg-turbo (a system library here, 3.2.0) decodes straight at 1/2 to
  1/8 size in 14 to 39 ms, 3 to 6 times less. Writing the 256 px PNG: 4 to 8 ms, 50 to 120 KiB.
- The output: DP-3, 1920x1200, scale 1. The sizes 32 to 128 draw from `normal` (128 px), 256
  from `large` (256); scale 2 would take `large` and `x-large` (512).

**Decided (Michael, 2026-10-24):** points 1, 2 and 5 as proposed; 4 with a 10 MiB limit, not 50;
the rest as proposed ("sounds good").

1. **What gets a preview:** ofx makes PNG, JPEG, GIF (the first frame) and WebP; any other file
   shows a preview when the cache already holds a fresh one (Nemo's PDFs and slides), else its
   icon. Videos and PDFs made by the system's thumbnailers stay on the later list.
2. **The cache:** read and written by the spec: `$XDG_CACHE_HOME/thumbnails/<size>/<md5 of the
   file's URI>.png`, fresh when `Thumb::MTime` equals the file's mtime (and `Thumb::Size` its size
   when present). Written with those, `Thumb::Size` and `Software ofx`, through a temporary file
   renamed into place, so Nemo never reads half a file. A file ofx can't decode gets
   `fail/ofx/<md5>.png` (the spec's failure entry, per program). Files in the cache itself never get
   previews.
3. **The size, one per drawn size (the rule from Nemo's loop):** the drawn pixels pick the one
   directory (`normal` up to 128, `large` to 256, `x-large` to 512); ofx checks only there, makes
   it there when missing or stale, and tries a file once per mtime: a failure or a stale result it
   can't replace is remembered until the file changes.
4. **Which files:** local drives only (not NFS, CIFS, SSHFS or other FUSE mounts: their reads can
   hang), and images up to 10 MiB (Michael; 50 was proposed. A 100 MP JPEG takes about a second).
5. **The decoder:** libjpeg-turbo for JPEG (system library, scaled decode); raylib's stb_image for
   PNG and GIF (libpng and giflib since, as built below); libwebp (system) for WebP. The thumbnail PNG written through stb_image_write's
   zlib, with our own tEXt chunks (stb writes none).
6. **Threads:** two workers, so a folder of photos fills in seconds while the window stays at
   60 fps; the queue holds only the cells on screen and a page ahead, newest first, and is emptied
   when another folder, zoom or view comes. Idle stays at 0 CPU: a finished preview posts one empty
   event, as the timer does.
7. **Held:** each preview a texture at its drawn size (as the icons since F6.3), at most 64 MiB of
   them, the least recently drawn dropped first; at 256 px that is about 256 previews, at 48 px
   thousands.
8. **The look:** the preview fitted in the icon's square, its shape kept, no frame; the file's icon
   until the preview comes. The list view keeps its icons (24 px is too small to tell photos apart).
9. **On and off:** always on in F6.4; a switch comes with `ofx.conf` (later).

**Tests:** `XDG_CACHE_HOME` pointed at the scratch dir by `run-script.sh` and the unit tests, so no
run reads or writes Michael's cache; fixture images (a PNG, a JPEG, a GIF, a WebP, a broken one, a
stale cache entry) made in the scratch dir. The thumbnail name checked against a URI that needs
escaping (spaces, `#`, UTF-8), since Nemo must find what ofx writes.

**As built (2026-10-24):**
- `fs/thumb.c` (raylib-free, unit-tested): MD5, the URI as GLib escapes it (letters, digits and
  `-._~!$&'()*+,=@:/` kept, the rest `%XX` in upper case, as `gio info` printed), checked against a
  name Nemo wrote in `large/`. Freshness: `Thumb::URI`, `Thumb::MTime`, and `Thumb::Size` when
  there (Nemo writes none). ofx writes all three and `Software ofx`, mode 0600 in folders of 0700,
  through `.ofx-<pid>-<n>.png` renamed into place, behind the test guard (`ops_allowed`).
- One directory per drawn size: `normal` to 128 device pixels, `large` to 256, `x-large` to 512,
  `xx-large` beyond. ofx reads and writes only there: Nemo's PDF preview in `large` shows at 256
  (scale 1), not at 48. A failure is a 1 by 1 PNG in `fail/ofx/` with the file's text, one for every
  size; an empty file fails too. A file that changed since its listing (`TG_CHANGED`) isn't made: its
  next listing has the new mtime.
- JPEG: libjpeg-turbo at the smallest of its scales that keeps the longer side at least the
  directory's size, then stb's resize, then turned by the EXIF orientation (all 8). WebP: libwebp
  scales as it decodes (an animated one fails). PNG: libpng's simplified API (the cache's previews
  too); GIF: giflib, the first frame only, on its screen, its transparent colour clear, interlaced
  rows put back; both decoded whole, refusing more than 40 MP. Resizes are stb_image_resize2's
  linear, as raylib's: the sRGB one's tables trip clang's UBSan.
- **The decoders are the system's (Michael, 2026-10-24):** the first build used a static copy of
  stb_image for PNG and GIF; stb makes no claim to handle hostile files and has had many
  memory-safety CVEs, and a preview decodes whatever is in a folder (a download). libjpeg-turbo,
  libwebp, libpng and giflib are fuzzed on OSS-Fuzz and patched by Arch's updates. A sandboxed
  decoder process, as Nemo's glycin, would go further: not now.
- Each worker has a 192 MiB arena (a 40 MP PNG is 160 MiB decoded); past its first 16 MiB the
  pages go back to the system (`madvise`) after a preview that used them.
- Not made on network or FUSE drives: `thumb_local` reads mountinfo for the folder's mount (nfs,
  nfs4, cifs, smb3, smbfs, 9p, ceph, afs, glusterfs, davfs, lustre, fuse, fuse.*; fuseblk is a
  local disk). Previews another program made there still show.
- `gui/thumbs.c`: two workers, a 64-request queue that each frame empties and fills again with
  what it draws, then a page ahead. Answers are kept by `<px>:<path>` with the mtime (8192 entries,
  all dropped when full): a file is asked again only when its mtime changes. Textures at most
  64 MiB, the least recently drawn dropped first (never one drawn in the last frame, nor one just
  loaded); a dropped one is asked again only for a cell on screen, never for the page ahead,
  which would load it, drop it and ask again each frame.
- Not in the trash view (its entries aren't at their paths), nor for broken links.
- Measured, release build, the test weston (scale 2, previews from `normal`): 100 photos of 1 to
  6 MiB, the first screen and a page ahead (36) in 0.47 s; the longest frame 0.3 ms. RSS 139 MiB
  before, 154 MiB after 52 previews made (the high-water mark the same).
- Found on the way: a PNG without a preview had no icon (F6.2's empty `photo.png`):
  `generic-icons` names none for `image/png`, and the fallback was `application-x-generic`, which
  Yaru lacks. Now the spec's `<media>-x-generic` (`image-x-generic`), then
  `application-x-generic`.

## F6.3 design (2026-10-24)

**Asked (Michael):** icon sizes: a zoom slider at the bottom right, Ctrl+wheel and Ctrl+= / Ctrl+-,
remembered. Decided (Michael, 2026-10-24: each Q took the proposal):

- **Levels:** 32, 48, 64, 96, 128 and 256 px, 48 the default (F6.2's). Yaru has PNGs at 32, 48 and
  256 (each also @2x); 64, 96 and 128 are the 256 scaled down (one texture per size and icon,
  cached as now under `"<px>:<key>"`). The cell grows with the icon: as wide as the icon plus 64 px,
  112 at least; two lines of name under it as now.
- **The slider:** in the footer's right end, about 120 px: a minus, a track with a notch per level
  and a knob, a plus. A click on the track or a drag snaps to the nearest level; the minus and plus
  step one. A tooltip says "Zoom: Ctrl+= Ctrl+- Ctrl+0" (F6.2's tooltips). The status text keeps the
  rest of the footer.
- **Keys:** Ctrl+= (and Ctrl++, Ctrl+keypad +) one level up, Ctrl+- one down, Ctrl+0 back to 48
  (Nemo's). Ctrl+wheel a level a notch, over the files; the plain wheel still scrolls.
- **Keeping your place:** after a zoom the cursor's cell stays in view, as near its old row on
  screen as the new rows allow.
- **Remembered:** `zoom=<px>` in the state file beside `view=`, one size for every folder (per
  folder: later, with the view).
- **The list view doesn't zoom:** the slider is hidden there and the keys do nothing; its rows stay
  24 px (a zooming list, with its own levels, on the later list if wanted).

**As built (2026-10-24):**
- A cell is the icon plus 64 px wide (112 at least) and the icon plus 56 high: at 936 px beside
  the sidebar, 6 across at 32 and 48, 5 at 64, 4 at 96, 3 at 128, 2 at 256.
- Icons bigger than drawn are made the drawn size as they load (raylib's `ImageResize`): Yaru's
  256 and 256@2x are Scalable from 64 to 256, so 64 px on a scale-2 output loaded a 512 px file,
  four times too big; the GPU's bilinear shrink left it grainy and held 1 MiB an icon.
- The icon lookup takes, when nothing matches the size, a directory at least as big before a
  closer smaller one (as GTK does): a theme with a fixed 256 only is scaled down, never its 48@2x up.
- The slider: a minus, a track of 84 px with a notch a size, the knob in the accent, a plus; a
  press on the track or a drag goes to the nearest size; tooltips on all three, above them.
  The status text gives up their room in the icon view.
- Ctrl+wheel is a size a notch, however many notches a frame brings.
- A zoom or a view change that finds the icon cache full empties it once, before the next frame,
  and what is on screen fills it again (reviewer-2: zooming within one folder filled it, and icons
  went blank until another folder was entered). Once, so a frame that alone needs more doesn't
  empty it every frame. The slots then hold about what is on screen, not every size seen.
- A zoom drag ends when the pointer leaves the window: Wayland's leave releases no button, and a
  compositor grab mid-drag would keep the knob following the pointer (reviewer-2).
- Keeping the cursor's place: `ui_refit` puts its row where it was on screen (its last row when
  fewer fit); the plain resize keeps the least scroll, as before.

## F6.2 design (2026-10-23)

**Asked (Michael):** an icon view, then a zoom slider (F6.3), then previews (F6.4), each its own
step: F6.4 is the biggest, and the plain icon view gets used before previews change it.
Decisions:

- **Switching:** a list button and an icon button in the top bar, beside the location toggle and
  search, as Nemo has them; Ctrl+1 the icons, Ctrl+2 the list (Nemo's keys). Swapped after trying it
  (Michael, 2026-10-24): Ctrl+1 the list, Ctrl+2 the icons, in the buttons' order.
- **Remembered:** one view for every folder, in `$XDG_STATE_HOME/ofx/state` beside the sidebar's
  mode (per folder: later).
- **Names:** under each icon, wrapped to two lines and cut with "…"; the selected one's in full.
- **Built the list's way:** the grid is the same listing, sort, selection, find, tabs and actions;
  only the layout and the cursor's moves differ. Left/Right (`h`/`l`) move a cell, Up/Down
  (`k`/`j`) a row, PgUp/PgDn a page of rows, Home/End the first and last; Enter opens, Backspace
  goes up (Left no longer does, in the grid). Size and date aren't shown; the status line has them.
- **Icon size:** 48 px at first (Yaru's PNGs: 16, 22, 24, 32, 48 and 256, each also @2x); F6.3
  adds the others, scaling the 256 down between 48 and 256.

**As built (2026-10-23):**
- A cell is 112 by 104 px: the icon, then two lines of name (`ui_wrap`: after a space, `-`, `_` or
  `.` where one fits, else between characters; the second line cut with "…"). The cursor's cell
  keeps its size; its name, up to 8 lines, floats over the cells below on the selection colour;
  a press there is the cell's underneath (reviewer-2: else a double click there opened the
  cursor's entry). Columns follow the width beside the sidebar; the scroll moves
  a row at a time, the wheel a row a notch.
- Down onto a last row that ends before the cursor's column goes to the last entry; Up in the
  first row and Down in the last stay put, as in Nemo. PgUp/PgDn keep the column.
- The buttons are drawn (three lines, four squares): Yaru has its view icons as SVG only.
- Rename's edit line sits under the icon, as wide as the name (one to two cells).
- No accent outline on the cursor's name (Michael, 2026-10-24): its background shows it, the same
  colour as its cell's (it was the full selection colour, brighter).
- The hover highlight follows the pointer where it is, not where it last moved: none once it has
  left the window, and hit again after each frame, so a folder entered or a zoom under a still
  pointer lights the cell now under it.
- No column headers in the icon view; `s`/`S` still sort.
- **Keys, after trying it (Michael, 2026-10-24):** Left/Right and `h`/`l` no longer open or go up,
  in the list or the grid; Enter and Backspace do (the tree keeps them, to close and open). Ctrl
  and an arrow move the keys to that part of the window, beside the letters: Ctrl+Left the sidebar
  (as Tab), Ctrl+Up the path buttons (Ctrl+K), Ctrl+Down the job rows (Ctrl+J), Ctrl+Right the
  location line (Ctrl+L). Tab or Ctrl+Left with the sidebar hidden shows it as places. Ctrl+S is
  F9. Ctrl+H stays hidden files (Nemo's).
- **Tooltips (Michael, 2026-10-24):** the icon-only buttons say what they do and their key after
  600 ms under the pointer (GTK's wait); gone when it moves off, and after a click or key until it
  leaves the button. The window sleeps meanwhile: a timer thread wakes it (idle stays at 0 CPU).
  Path buttons, Restore and Empty have their words already.
- The icon cache (512 textures, both sizes) is emptied when full as another folder is entered,
  rather than leaving icons blank until a theme change (reviewer-2).
- Found on the way, not F6.2's: an empty `photo.png` shows no icon, in the list too (fixed in
  F6.4: `image-x-generic`).

**Thumbnails, F6.4 (noted now):** read and written in `~/.cache/thumbnails` by the freedesktop
spec (Nemo fills it here already: `normal`, `large`, `x-large`, `fail`), PNG and JPEG decoded off
the main thread, a cap on textures held; the rule from Nemo's loop below holds.

## F6 design (2026-10-22)

**Asked (Michael):** the sidebar's modes in the part of the bottom bar under the sidebar: one bar
across the window, its left part the sidebar's, its right part the status. Decisions:

- **Places mode, two sections.** *Places*: Home at the top and Trash at the bottom, both always
  there and not removable, the GTK bookmarks (shared with Nemo) between them; the XDG user dirs
  (Documents, Downloads) show only when bookmarked. *Drives*: `/` and each mounted drive (`sd`
  here, a USB drive when one is mounted), each with a thin bar of how full it is and its free space
  in numbers. Drives come and go live: ofx watches the mount list (`/proc/self/mountinfo`).
- **Tree mode:** from `/`, opened down to the current folder.
- **Hidden mode:** no sidebar.
- **Switching:** three small icon buttons in the bar's sidebar part (places, tree, hidden); F9
  cycles through them.
- **Trash:** opening it lists the trashed files; Restore puts the marked ones back where they came
  from (from their `.trashinfo`), Empty Trash deletes all of it for good after a panel asks.
- **The status part:** the status message only; the key list goes (the keys get a panel of their
  own later). With one folder selected: `"adir" selected (12 items)`; several: `3 items selected`;
  else the folder's count and its drive's free space, `14 items, 87 GB free`.
- **Remembered across a restart:** the sidebar's mode now, the files' view (list or icons)
  later. Options Michael sets (a date format, columns) go in `$XDG_CONFIG_HOME/ofx/ofx.conf`, which
  ofx only reads; what ofx remembers by itself (the mode, later the tabs) in
  `$XDG_STATE_HOME/ofx/state`, which ofx writes. The XDG split; Michael agreed (2026-10-22).

**As built (2026-10-23), the calls made along the way:**
- The headings *Places* and *Drives* are rows of their own; the cursor steps over them, a click
  on one does nothing. A drive's free space is on the right of its row, the fullness bar under
  its name, red from 90% full. The mounts come live from `/proc/self/mountinfo`'s POLLPRI
  (proc(5)); a drive's space is read again with every listing (no timer: idle stays idle).
- The tree opens the folders above the one shown, not that folder itself; folders opened by hand
  stay open (up to 128). Left/Right (`h`/`l`) in the sidebar close and open a folder, or move up to
  its folder and down into it; a click on the ▸/▾ mark opens or closes, a click on the name goes
  there. 10 px a level; deep names are cut with "…" (the sidebar is 200 px, not resizable yet).
- The status part: a message (an error, "Copied 3 items") stays until the next key or click, then
  the status line comes back. A selected file shows its size the way a folder shows its count
  (`"a.txt" selected (4.2 KiB)`); hidden files left out are counted, `14 items (2 hidden), 87 GiB
  free`. The prompts' one-line hints (find, rename, location, path buttons, jobs) stay; the key
  list goes.
- The trash view is `trash:///`, a listing of every trash (the home one and each drive's
  `.Trash/$uid` and `.Trash-$uid`): an item by its first name, the folder it came from beside it,
  the Modified column as Deleted. Restore (the button, or `r`) makes a missing folder again and
  never replaces a file there now (it says so). Delete there is for good, asked first, with the
  `.trashinfo`. Empty Trash deletes each trash's `files/` and `info/` as one job; the next trash
  makes them again. A file opens from there, a folder doesn't; rename, paste, a new folder, copy,
  cut and a terminal say they don't work there. Only the home trash is watched (a drive's trash
  changes by ofx reload the view).
- `ofx.conf`: nothing reads it yet (no options so far).

**After reviewer-2's F6 round (2026-10-23):**
- A drive's trash can't send Restore off its drive (a stick's `.trashinfo` can say anything): a
  Path with a `..` part, or an absolute one elsewhere, is left out; Restore resolves the target's
  folder, symlinks followed, and refuses one off the drive. Listed only: the user's own
  `.Trash-$uid` and `.Trash/$uid` in a sticky `.Trash`, no symlinks (as `trash_for` writes).
- Delete for good in the trash view deletes an item's `.trashinfo` only when the item went.
- Empty Trash empties `files/` and `info/` (and the home trash's `directorysizes`), the folders
  kept, so a watcher on them (gvfs's) keeps working; more than 4096 items go as several jobs.
- The drives' space is read on a thread of its own (`gui/space.c`): a hung mount or a disk spinning
  up never stalls the window.
- An error in the status part stays until a key or click at least a second after it.

**F6.1 (Michael, 2026-10-23), after trying F6:**
- A drive's row shows its free space in the colour of the bar's free part; with the pointer over
  it, its used space in the colour of the used part (one number at a time: the row is 200 px).
- A removable drive (`/run/media`, `/media`) gets an eject button in its icon's place while the
  pointer is over the row: `udisksctl unmount -b <device>`, then `udisksctl power-off`; a busy
  drive says why in the status part; never while a job of ofx's uses it. Tests use a fake
  `udisksctl` that logs.
- ofx runs from `/` (chdir after resolving its folder), so a launch from inside a stick doesn't
  keep it busy; eject's udisksctl gets 60 s, then it and what it started are killed (reviewer-2).
- Fixed on the way: a click on a mode button (or a tree mark) drew no frame until the next event,
  so the switch showed only when the pointer moved (places looked fast: the space thread's answer
  drew one).

## F5.2 design (2026-10-21)

**Asked (Michael):** copy and paste in the location line and the rename line (he leans on it);
Ctrl+K to the path buttons from the keyboard. Decisions: Shift+arrows select part of the text;
Shift+Enter on a path button opens it in a background tab, as a middle click does.

**The edit lines** (rename, new folder, location) gain a selection: an anchor and the caret.
Shift+Left/Right/Home/End extend it, Ctrl+A selects all (the location line still opens all
selected); typing or Backspace/Delete replaces what is selected. Ctrl+C copies the selection as
text (nothing selected: nothing copied), Ctrl+X cuts it, Ctrl+V pastes text at the caret over the
selection: the first line only, and in a name line the characters a name can't hold left out
(`/`, controls). A text copy replaces whatever is on the clipboard, files included, as any
program's copy does; a paste after a copy of files gives their paths.

*Built (2026-10-21):* the text goes through ofx's own data-control connection (F3's), offered as
`text/plain` only, not through GLFW's clipboard as planned: GLFW reads another program's text with
no time limit (a hung program would hang the window), and while the main thread waited on GLFW's
own source through data-control, nothing could answer it. GLFW's clipboard is the fallback where
there is no data-control (weston, the tests).

**The path buttons by keyboard:** Ctrl+K, from the list or from the location line (which it closes),
puts the keyboard on the path buttons, the rightmost shown marked. Left and Right move (the window
scrolls to keep it in view), Home and End go to the ends, Enter goes to that folder, Shift+Enter
opens it in a background tab; Esc, Tab, or a click on the list give the keyboard back to the list.
*Added (Michael, after trying it):* a file's path in the location line (pasted, typed) opens its
folder with the cursor on the file, as Nemo does; a hidden one says `.` shows hidden files.
*Built:* any click gives it back, and so does any other key, which then does what it does in the
list (Down moves the cursor); `h` and `l` move as Left and Right do (Michael, after trying it); the marked button has an accent outline and the footer names the
keys.

## F5.3 design (2026-10-21)

**Asked (Michael):** the progress of a copy or move where it can't be missed, without covering
anything that matters: a strip at the bottom of the files area (as the tab bar is at its top, never
over the sidebar), a row per job, up to 4 at once, the rest queued (32 at most), and a modal panel
when the queue is full. Each row a throughput line as Windows draws it: x the progress, y the
speed, squashed to the fastest seen so far, no axes; only the percentage and the current speed as
numbers. Decisions: quitting with jobs running or waiting asks in a panel; delete for good gets a
row too; a job on files another running job uses waits in the queue until that one ends.

**Jobs:** up to 4 workers at once, each with its own `Job` and clash state; the rest wait in a
queue of 32 (FIFO). A job "uses" its sources and its targets' folders; a job whose paths overlap a
running job's (the same path, or one under the other) waits, even when a worker is free. A 33rd job
opens a panel: "No more jobs can be queued right now. Wait until one finishes." (OK). Clashes from
two jobs at once take turns in the clash panel, which names the job asking. Undo undoes the job
that finished last. The "a job is still running" refusals go.

**The rows,** about 40 px each, over the bottom of the files: the throughput line across the row
(1 device pixel), the done part tinted in the accent; on it, what the job is ("Copying 12 items to
bdir") and the percentage on the left, the speed on the right, a × that cancels it (with its part
file cleaned up, as Esc cancels now). Under them "5 waiting" when the queue holds any. A job without
bytes to measure (delete for good, a move within a drive) is a plain bar by items. Speed is
smoothed over about 2 s; the line keeps 300 points, one per 1/300 of the job.

**By keyboard (Michael, 2026-10-22):** Ctrl+J takes the keyboard down to the job rows, the top
row's × marked; Up and Down (and `k`, `j`) move between the rows' ×. Enter presses the marked ×.
Esc, Tab or a click give the keyboard back to the list. A ×, by Enter or by the mouse, first asks
in a panel: "Cancel copying 12 items to bdir?", Keep going and Cancel job.

**Quitting** with jobs running or waiting: a panel, "2 jobs are running and 3 are waiting. Quit
and cancel them?", Cancel and Quit.

*Built (2026-10-22):* a waiting job starts when it overlaps no running job and no waiting job
before it, so a later job on other files doesn't queue behind a held one, and two on the same
files keep their order. Esc no longer cancels a job (the row's × does, after asking); the panels'
Enter is their yes (Cancel job, Quit), as the delete panel's. The window's close button asks as
Ctrl+Q does. The speed is read over at least half a second (a first block from the page cache
would set the line's scale for good). The line's full height is the fastest point of the last 30 s, a fifth over it (Michael, after
a USB 2 stick: a burst into the page cache at the start pressed the rest to the floor when scaled
to the fastest, and still did at the 90th percentile while the burst was most of the points). Each job record holds its items and paths (36 records, about
5 MiB each, static: only a used one's pages are touched).

## F5.1 design (2026-10-21)

**Asked (Michael):** the delete-for-good question front and centre instead of in the status bar:
a modal panel in the middle of the window, the rest dimmed or blurred, so it can't be missed; the
name-clash question of a copy or move the same.

**The delete panel:** "Delete 3 items for good?", the names it will delete (the targets taken
when it opened, F5's fix: up to 8, then "and 12 more"), "This can't be undone.", and Cancel and
Delete buttons, Delete the default (Enter, `y`); `n` and Esc cancel. **The clash panel:**
"big.bin already exists in bdir", Skip, Replace (Merge for a folder onto a folder), Keep both and
Cancel, and a "Do the same for the rest" box (a click, or the capitals as before). Clicks outside
the panel do nothing; the keys are the prompts' as before.

**Backdrop:** the window darkened by a see-through layer, still readable. Michael tried both looks
live and picked this over the blur (the frame scaled down and back up), which then went
(2026-10-21).

## F5 design (2026-10-21)

**Decisions (Michael):** Ctrl+W on the last tab closes ofx; a middle click opens its tab in the
background; sort order and hidden files belong to the window, not to a tab; tabs are not
remembered across a restart (on the later list).

**Per tab:** the folder, the cursor (by name) and the scroll, the history, the selection (by
name), the find line's last text, the path buttons. **The window's:** sort, hidden files, the
sidebar, the job, the clipboard, undo, the theme. Only the tab on screen holds a listing and the
inotify watch; switching re-lists its folder (the listing arenas stay two, however many tabs) and
puts its cursor, scroll and selection back by name. A tab whose folder is gone goes to the nearest
folder above that is there, as a reload does. At most 32 tabs.

**The tab bar,** below the top bar, only with two tabs or more (Nemo): the tabs share the width,
each the folder's name, cut to fit, and a ×; the one on screen marked in the accent. A click on a
tab shows it; a middle click or its × closes it.

**Keys and mouse:** Ctrl+T a new tab on the current folder, after the current one, shown;
Ctrl+W closes the current tab (the one to its right shows, or to its left at the end; the last
closes ofx); Ctrl+Tab / Ctrl+Shift+Tab and Ctrl+PgDn / Ctrl+PgUp the next and previous (wrapping);
Alt+1 to Alt+9 a tab by place (Alt+9 the last); Ctrl+Shift+PgUp / PgDn move the current tab left
or right. A middle click on a path button, a folder row or a sidebar place opens it in a new tab in
the background.

**A job across tabs:** a copy, move or delete runs on whatever tab is shown; its progress and its
prompts stay in the footer, and a tab showing a folder it changed lists again when it is shown.

## F5 exit criteria

1. `make test`: the tab list in ui/ (open after the current one, close and who shows next, the
   last tab, move, next/previous wrapping, Alt+N, the 32 limit) and the per-tab state kept by name;
   a mutation run over the new ui/ code.
2. GUI scripts: Ctrl+T, Ctrl+W (and the last tab quitting), switching by key, by click and by
   Alt+N; each tab back on its folder, cursor, scroll, selection and history; sort and hidden
   shared; middle clicks on a path button, a row and a place open background tabs; a tab whose
   folder is gone; the tab bar only from two tabs; a copy that keeps running across a switch.
3. F1-F4 scripts still pass; idle about 0%; LeakSanitizer reports nothing.

## F4 design (2026-10-20)

**Decisions (Michael):** search stays the `/` find for now; the path buttons keep the deeper
folders after going up, as Nemo's do; folders show their item count in the Size column, more
telling than a byte size; no icon or list view buttons until there is an icon view.

**The top bar** replaces F1's header row: back, forward and up on the left, then the path buttons,
then (right) the location entry toggle and search. The item count moves to the footer's right end
("51 items, 2 selected"). Buttons that can't act (back with no history, up at `/`) are dimmed and
ignore clicks; every button lights up under the pointer.

**Arrows and button glyphs:** the icon theme has these only as symbolic SVGs (Yaru, Adwaita), and
ofx has no SVG rasteriser, so they are drawn as text glyphs from the UI font in the theme's colours
(← → ↑ ‹ › from Liberation Sans, ✎ from Noto Sans Symbols 2 through fontconfig's fallback), as
Nemo's symbolic icons are monochrome too. Search uses the theme's own PNG (`edit-find`, which
Yaru draws as a magnifier over an "A").

**Path buttons:** under `$HOME` the first button is the home folder (its icon and name) and the
folders above it are left out, as Nemo does; elsewhere the first is `/`. A click goes there.
The bar remembers the deepest path it showed: going up keeps the deeper buttons (the current one
marked), going to a folder off that path starts it anew. When they don't fit, the oldest go off the
left behind a ‹ button and the deeper ones off the right behind ›, and the current one is always
shown. Middle click is kept for F5 (a new tab). Alt+Up goes up, Alt+Left and Alt+Right as now.

**Location entry:** Ctrl+L or the toggle button swaps the path buttons for an edit line holding the
current path (F3's edit line), all of it selected as browsers and Nemo do, so typing replaces
it and a move of the caret keeps it (built so, 2026-10-20). Enter goes there (`~` expanded, a relative path taken from the current folder); a path that isn't a
folder keeps the line open with the reason. Esc, or the toggle again, goes back to the buttons.
No Tab completion in F4.

**Search** opens the find line, as `/` does.

**Item counts:** a folder's Size cell reads "14 items" ("1 item"; "?" when it can't be read). The
count is the folder's entries, dot-files only when hidden files are shown. Counting reads each
folder, which can be slow on a big or slow drive, so it happens off the main thread for the rows on
screen, cached by name per listing and dropped on reload; the cell is blank until it is known.
Sorting by size keeps folders by name (counts aren't sizes).

## F4 exit criteria

1. `make test`: the path buttons (the segments, the remembered deeper path, home shortening, the
   overflow window), the location entry's path parsing (`~`, relative, `..`), the counts (hidden
   on and off, unreadable), the dimmed buttons; a mutation run over the new fs/ and ui/ code.
2. GUI scripts: each button by click (`point-ui`), dimmed buttons ignore clicks, path buttons after
   going up and after going elsewhere, overflow on a deep scratch path, Ctrl+L and the toggle, Enter
   on a good and a bad path, Esc; counts in the Size column, live after a file is added.
3. F1-F3 scripts still pass; idle is still about 0%; LeakSanitizer reports nothing.

## F3 design (2026-10-20)

**Decisions (Michael):** Shift+Delete deletes for good, after a confirm that defaults to yes; Ctrl+C and Ctrl+X put the
paths on the system clipboard as well, so a paste in Nemo or a terminal works; Ctrl+Z undoes the
last action; Ctrl+Shift+N makes a new folder.

**Selection:** Space toggles the entry under the cursor and moves down, Ctrl+A selects all, Esc
clears; a Ctrl+click toggles, a Shift+click selects the range from the cursor. Selected rows get
the theme's selection colour, the cursor keeps its own mark. An action works on the selection, or
on the cursor's entry when nothing is selected. A reload keeps the selection by name.

**Keys:** F2 rename; Ctrl+Shift+N new folder; Delete to the trash; Shift+Delete for good;
Ctrl+C, Ctrl+X, Ctrl+V; Ctrl+Z undo.

**Rename and new folder:** an edit line on the row, with a caret (Left, Right, Home, End,
Backspace, Delete). Enter renames with `renameat2(RENAME_NOREPLACE)`, so nothing is overwritten;
Esc cancels. A name that exists, is empty, or holds a `/` keeps the line open with the reason in
the status line. A new folder is made at once as "New folder" (" (2)" and up when taken, as Nemo
does) and its name opens in the line: Esc keeps "New folder", and undo removes it whatever it is
called by then (built so, 2026-10-20: the row exists to edit in).

**Trash (`fs/trash.c`), the freedesktop Trash spec 1.0:** a file on the home drive goes to
`$XDG_DATA_HOME/Trash` (default `~/.local/share/Trash`); on another drive (`/mnt/sd` is one) to
`$topdir/.Trash/$uid` when `$topdir/.Trash` is a real folder with the sticky bit, else
`$topdir/.Trash-$uid` (made 0700), never across drives. The `.trashinfo` (Path, percent-encoded;
absolute in the home trash, relative to the top dir in the others; DeletionDate in local time) is
created with `O_EXCL` first, which also picks the free name (`name`, `name.2`, …); then the file
is renamed into `files/`. Nemo's trash view and `gio trash --list` show what ofx trashed.

**Delete for good:** the footer asks "Delete 3 items for good? This can't be undone. Y/n";
Enter or `y` goes ahead (whoever presses Shift+Delete means it: Michael, 2026-10-20), `n` or Esc
cancels. Folders are removed depth first with `unlinkat`, never following a symlink.

**Copy and move (`fs/copy.c`):** Ctrl+C or Ctrl+X remembers the paths and the verb; Ctrl+V into
the open folder starts a job on a worker thread with its own arena (no malloc).
- A move on the same drive is one `renameat2(RENAME_NOREPLACE)` per entry. Across drives it is a
  copy, then the source is deleted once that entry copied in full.
- A copy counts the files and bytes first, then copies each file with `copy_file_range` (falling
  back to `read`/`write` in 1 MiB blocks) into `.ofx-part-<name>` beside the target, and renames it
  into place at the end, so a cancel or a crash never leaves a half file under the real name.
  Modes and times are kept (`fchmod`, `futimens`); symlinks are copied as links, never followed;
  sockets and devices are skipped with a note.
- The footer shows "Copying 3 of 12 files, 1.2 of 4.0 GiB · Esc cancels" while the window stays
  usable. One job at a time: a second paste while one runs is refused in the status line.
- Pasting into the folder the files are in makes copies named " (copy)", " (copy 2)".
- Pasting a folder into itself or below itself is refused.

**Name clashes:** the job stops at a clash and asks in the footer: `s` skip, `r` replace (a folder
"replace" merges, asking again inside), `k` keep both (" (2)"), with Shift for "all the rest";
Esc cancels the job. A replaced file is written to the part file first and renamed over the old
one, so the old file survives until the new one is complete.

**System clipboard (`gui/clipboard.c`):** GLFW only offers `text/plain`, which Nemo can't paste
as files. ofx opens its own second Wayland connection and binds `ext-data-control-v1`
(Hyprland has it; wayland-protocols 1.49 ships the XML), which needs no input serial. On Ctrl+C
or Ctrl+X it offers `x-special/gnome-copied-files` (`copy` or `cut`, then `file://` URIs: what
Nemo reads), `text/uri-list`, and `text/plain;charset=utf-8` / `text/plain` (the paths, one per
line, for a terminal). Ctrl+V takes ofx's own copy when it still owns the selection, else reads
`x-special/gnome-copied-files` or `text/uri-list` from whoever does, so files copied in Nemo
paste in ofx too. Without the protocol (weston, in the tests) the clipboard stays inside ofx and the
status line says so once.

**Undo (Ctrl+Z, one level: the last action only):**
- rename: renamed back; new folder: removed if still empty;
- trash: restored from the trash (the file renamed back, its `.trashinfo` removed);
- move: moved back; copy: the copies go to the trash (never deleted for good);
- delete for good: can't be undone, and the confirm says so.
Undo checks first that things are as the action left them (the new name exists, the old one is
free); if not it does nothing and says why. The status line names what was undone.

**Test guard:** with `OFX_GUI_ROOT` set (the GUI tests), every write in fs/ (rename, mkdir, trash,
delete, copy, move, undo) refuses a path outside that folder, checked on the resolved path, and
logs the refusal; `XDG_DATA_HOME` points at scratch so the home trash is scratch too.

## F3 exit criteria

1. `make test` covers the trash (info file, name picking, home and top-dir trash chosen by
   `st_dev`, restore), delete for good (depth first, symlinks not followed), copy (part file,
   modes and times, symlinks, cancel leaves nothing, cross-drive move through an injected
   "other drive"), clashes (each answer, "all the rest", folder merge), paste into itself refused,
   " (copy)" names, the clipboard payloads both ways, undo of each action and its refusals, and the
   guard; a mutation run over the new fs/ and ui/ code with every survivor killed or shown inert.
2. GUI scripts in weston on scratch trees, with a scratch `XDG_DATA_HOME`: select by keys and by
   Ctrl/Shift click; rename and new folder (commit, clash, Esc); trash then undo; Shift+Delete
   with `n`, Esc, Enter and `y`; copy and cut-paste with the progress line (slowed by a test-only delay),
   Esc cancels with no part file left; a clash answered each way; the guard refusing a path
   outside the scratch folder.
3. One live clipboard check on Hyprland, with Michael's go since it replaces his clipboard: copy in
   ofx, paste in Nemo and in a terminal; copy in Nemo, paste in ofx.
   Done 2026-10-20 by Michael: all three pasted as expected, and four rounds of Ctrl+C in ofx, a
   copy elsewhere, Ctrl+C in ofx again (the cancelled-source path reviewer-2's fix touched) held.
4. F1 and F2 scripts still pass; idle is still about 0%; LeakSanitizer reports nothing.

## F2 design (2026-10-20)

**Decisions (Michael):** find opens with `/` or Ctrl+F; hidden files toggle with `.` or Ctrl+H; the
mouse's back and forward buttons walk the history (Backspace stays "parent"); sorting by clickable
column headers (Name, Size, Modified; a second click reverses), with `s` (next column) and `S`
(reverse) on the keyboard.

**Input becomes keys plus text:** gui/ passes the keys without text (arrows, Enter, Backspace,
Esc, Tab, and the chords Ctrl+F, Ctrl+H, Alt+Left, Alt+Right) and the typed characters apart;
ui/ decides what a character means, since the same `j` moves the cursor or goes into the find line.

**Sidebar (`fs/places.c`, raylib-free):** Home, then the GTK bookmarks
(`~/.config/gtk-3.0/bookmarks`, shared with Nemo: `file://` URIs, percent-decoded, an optional
label, else the folder's name), then the mounts from `/proc/self/mountinfo`: `/`, and anything
mounted under `/mnt`, `/media` or `/run/media` (the btrfs subvolumes `/home`, `/var/log` and the
like are left out). Duplicates by path are dropped. Icons: `user-home`, the XDG folders' own, and
`drive-harddisk` / `drive-removable-media` (the icon lookup reads the Devices context too). The
entry holding the current folder (the longest path prefix) is marked. Tab moves the keyboard
between the sidebar and the list; a click goes there.

**History:** each folder change pushes (folder, selected name) on the back stack and clears
forward; back and forward restore the selection. 64 deep. Alt+Left/Right and the mouse buttons.

**Hidden files:** a toggle; the listing keeps dot-files out unless it is on (the count still shows).

**Sort:** name (natural, as F1), size, or modified, each either way; folders stay first. Ties
fall back to the name, so the order is total. A reload keeps the sort; the setting is not saved.

**Find:** a line in the footer. Each character typed moves the cursor to the first entry from
the cursor on whose name contains the text, ignoring case (wrapping); Backspace deletes; Enter
keeps the cursor and closes; Esc puts the cursor back and closes. After closing, `n` and `N` find
the next and previous match of the last text.

## F2 exit criteria

1. `make test` covers places (bookmark and mountinfo parsing on fixtures), history, sort, find and
   the new key routing; a mutation run over fs/ and ui/ with every survivor killed or shown inert.
2. GUI scripts: hidden toggle, each sort and its reverse by key and by header click, find (type,
   n/N, Esc restores, Enter keeps), back/forward by key and by mouse buttons (raw), the sidebar by
   Tab and click, against a scratch bookmarks file and a scratch HOME.
3. F1's scripts still pass; idle is still about 0%.

## F1 exit criteria

1. `make test`: the fs/ and ui/ unit tests pass under ASan and UBSan; a mutation run over fs/ and
   ui/ with every survivor killed or shown inert.
2. GUI scripts on a headless output (`tools/gui/run-script.sh`), with fakes for `uwsm-app`,
   `xdg-open` and `xdg-terminal-exec` first on PATH and a scratch `XDG_STATE_HOME`:
   - browse a scratch tree: enter, parent (selection restored), Home/End, PgUp/PgDn, wheel;
   - open and terminal: the fake logs the exact arguments;
   - live refresh: a file created, deleted and renamed in the open folder shows within 300 ms;
   - theme switch: rewriting the scratch `colors.toml` recolours the running window;
   - icons at scale 2 come from the `@2x` directories (state line names the file).
3. A folder of 10,000 entries lists and scrolls; the slowest frame is recorded.
4. Idle: 60 s with ofx open and untouched, its CPU time and wakeups against Nemo's on the same
   folder.
5. LeakSanitizer reports nothing with `gui/lsan.supp`.
