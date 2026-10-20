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
| F3 | Change files: rename, trash, copy and move with progress, on scratch folders only in tests | |
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
