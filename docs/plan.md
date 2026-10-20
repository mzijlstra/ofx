# fx: plan

fx is Michael's file explorer: a keyboard-first list of one folder at a time, in the Omarchy
theme's colours and icons, written in C on the prdash and uml-viewer stack (arenas, Clay,
raylib on native Wayland). It replaces Nemo, which idles at about 5% CPU, and Nautilus, which
had no "terminal here". The concept, with what is in and out of scope, is the doc "File explorer
concept" (https://claude.ai/code/artifact/8d990266-71ea-4d0e-a109-a8f3a71807ba).

Out for good: drag and drop (Michael doesn't drag files), the "Save as" portal dialogs, network
shares, archives, "open with" menus.

## Decisions (2026-10-20, Michael)

- Name `fx`, in `tools/fx/`, its own local git repo.
- Keys: arrows, Enter and Backspace, and vim-style `h j k l`, `g`/`G` alongside.
- A list view only; an icon grid may come later.

## Milestones

| | What | Exit |
|---|---|---|
| F1 | Browse: one folder as a list (icon, name, size, modified), theme colours and icons, keyboard and mouse navigation, open, terminal here, live refresh | Below |
| F2 | Get around: sidebar (bookmarks, mounts), hidden files, sort, type-to-find, back and forward | |
| F3 | Change files: select, rename, new folder, trash, delete, copy and move with progress, the system clipboard, undo; on scratch folders only in tests | Below |
| F4 | Extras: thumbnails, clipboard copy, git status per file | |

Each milestone ends in a reviewer-2 round, then Michael's go before `make install`.

**F4 thumbnails, a rule from Nemo (2026-10-20):** Nemo looped at up to 45% CPU on one file
because it checked an x-large thumbnail, found it stale (an older file of the same name), wrote the
new one to large, and checked x-large again, decoding the image several times a second. fx must
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
A folder fx can't read shows the error in the window, not a crash.

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
`omarchy-launch-terminal` does.

**Keys:** Up/Down and `j`/`k` move; PgUp/PgDn; Home/End and `g`/`G`; Enter, Right and `l` open
(a folder is entered, a file goes to `xdg-open`); Backspace, Left and `h` go to the parent, with
the folder you left selected; `t` opens a terminal in the current folder; `~` goes home; Ctrl+Q
quits. **Mouse:** a click selects, a double click opens, the wheel scrolls one row per notch
(Michael's preference from prdash-tui), the back button goes to the parent.

**Live refresh:** inotify on the open folder only (not recursive), batched 100 ms after the last
event, the selection kept by name (or the row it was on, when that name is gone).

**Idle:** the loop draws only when something changed and otherwise waits in
`PollInputEvents` (uml-viewer's loop, prdash's `docs/gui-findings.md`).

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
is renamed into `files/`. Nemo's trash view and `gio trash --list` show what fx trashed.

**Delete for good:** the footer asks "Delete 3 items for good? This can't be undone. Y/n";
Enter or `y` goes ahead (whoever presses Shift+Delete means it: Michael, 2026-10-20), `n` or Esc
cancels. Folders are removed depth first with `unlinkat`, never following a symlink.

**Copy and move (`fs/copy.c`):** Ctrl+C or Ctrl+X remembers the paths and the verb; Ctrl+V into
the open folder starts a job on a worker thread with its own arena (no malloc).
- A move on the same drive is one `renameat2(RENAME_NOREPLACE)` per entry. Across drives it is a
  copy, then the source is deleted once that entry copied in full.
- A copy counts the files and bytes first, then copies each file with `copy_file_range` (falling
  back to `read`/`write` in 1 MiB blocks) into `.fx-part-<name>` beside the target, and renames it
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
as files. fx opens its own second Wayland connection and binds `ext-data-control-v1`
(Hyprland has it; wayland-protocols 1.49 ships the XML), which needs no input serial. On Ctrl+C
or Ctrl+X it offers `x-special/gnome-copied-files` (`copy` or `cut`, then `file://` URIs: what
Nemo reads), `text/uri-list`, and `text/plain;charset=utf-8` / `text/plain` (the paths, one per
line, for a terminal). Ctrl+V takes fx's own copy when it still owns the selection, else reads
`x-special/gnome-copied-files` or `text/uri-list` from whoever does, so files copied in Nemo
paste in fx too. Without the protocol (weston, in the tests) the clipboard stays inside fx and the
status line says so once.

**Undo (Ctrl+Z, one level: the last action only):**
- rename: renamed back; new folder: removed if still empty;
- trash: restored from the trash (the file renamed back, its `.trashinfo` removed);
- move: moved back; copy: the copies go to the trash (never deleted for good);
- delete for good: can't be undone, and the confirm says so.
Undo checks first that things are as the action left them (the new name exists, the old one is
free); if not it does nothing and says why. The status line names what was undone.

**Test guard:** with `FX_GUI_ROOT` set (the GUI tests), every write in fs/ (rename, mkdir, trash,
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
   fx, paste in Nemo and in a terminal; copy in Nemo, paste in fx.
   Done 2026-10-20 by Michael: all three pasted as expected, and four rounds of Ctrl+C in fx, a
   copy elsewhere, Ctrl+C in fx again (the cancelled-source path reviewer-2's fix touched) held.
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
4. Idle: 60 s with fx open and untouched, its CPU time and wakeups against Nemo's on the same
   folder.
5. LeakSanitizer reports nothing with `gui/lsan.supp`.
