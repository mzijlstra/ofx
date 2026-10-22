# ofx: plan

ofx is Michael's file explorer: a keyboard-first list of one folder at a time, in the Omarchy
theme's colours and icons, written in C on the prdash and uml-viewer stack (arenas, Clay,
raylib on native Wayland). It replaces Nemo, which idles at about 5% CPU, and Nautilus, which
had no "terminal here". The concept, with what is in and out of scope, is the doc "File explorer
concept" (https://claude.ai/code/artifact/8d990266-71ea-4d0e-a109-a8f3a71807ba).

Out for good: drag and drop (Michael doesn't drag files), the "Save as" portal dialogs, network
shares, archives, "open with" menus.

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
| F6 | Bottom bar: sidebar as places (Home, bookmarks, Trash; drives with free space), as a tree, or hidden (F9); Trash restore and empty; the status line only; the mode remembered | Below |
| F7 | Menus: File, Edit, View, Go, Bookmarks, Help | |

Each milestone ends in a reviewer-2 round, then Michael's go before `make install`.

**Order (Michael, 2026-10-20):** after F3 the priority is a GUI closer to Nemo's (his screenshot of
Nemo: menus, a top bar, tabs, sidebar modes in the bottom bar), in the order F4 to F7. Left out on
his word: Nemo's compact view and its icon size slider.

**Later, wanted eventually, not scheduled:**
- Icon view (the top bar's icon and list buttons), and thumbnails, shown only in the icon view.
- Git status per file (`git status --porcelain=v2 -z` off the main thread, a mark per row).
- USB drives (from the concept doc): unmounted drives in the sidebar, a click mounts them with
  `udisksctl mount`, an eject button unmounts.
- A key that copies the cursor's path as text (Ctrl+C already puts files on the clipboard, F3).
- Install Nautilus at some point to compare its features with ofx's (not a priority).
- Search through subfolders, as Nemo's search does (the top bar's search opens `/` find for now).
- Optional columns: permissions; type (Michael doesn't want it by default). Date and time format as
  an option; the short form ("14:03", "Oct 20") stays the default.
- Sidebar sections (Nemo's My Computer, Bookmarks, Network): to brainstorm with Michael first.
- Counting in a separate process (reviewer-2, F4 review): a readdir on a hard NFS mount that
  hangs sits in D state, which no thread exit can reap, so ofx's process (its Wayland socket) can
  outlive its window. Only a child process that does the counting decouples a dead mount fully.
- Remember the open tabs across a restart (F5 doesn't).
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
