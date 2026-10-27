# ofx

A keyboard-first file explorer for Omarchy: one folder at a time as a list, in the current
theme's colours and icons. C with arenas, Clay for layout, raylib on native Wayland. It idles at
about 0% CPU: it draws only when something changes.

## Build and run

```sh
make            # build/ofx, debug with ASan and UBSan
make release    # build/release/ofx
make test       # unit tests for fs/ and ui/
make test-fm1   # ofx-fm1 end to end, on a private D-Bus session
make install    # ofx, ofx-clean and ofx-fm1 into ~/.local/bin, the cleanup's timer into ~/.config/systemd/user
ofx [--select] [PATH]   # the current folder by default; on a file (or with --select), its folder, it selected
```

Needs a C11 compiler, `wayland-scanner`, and the pkg-config packages wayland-client,
wayland-cursor, wayland-egl, xkbcommon, fontconfig and freetype2, and libjpeg-turbo, libwebp,
libpng, giflib and zlib for the previews, and libsystemd (sd-bus) for ofx-fm1. raylib, Clay and
nanosvg are vendored.

`ofx-fm1` answers `org.freedesktop.FileManager1` on the session bus, so a browser's "Show in
folder" opens ofx (the file selected) instead of Nemo. The bus starts it when asked (`make install`
puts its service file in `~/.local/share/dbus-1/services`), and it exits when idle.

## Use

| Key | |
|---|---|
| Up/Down, `j`/`k` | move; past the first or the last, round to the other end (the icon view: Left/Right round the ends, Up/Down in the column) |
| PgUp/PgDn | a page |
| Home/End, `g`/`G` | first, last |
| Shift+Enter | Open With Other Application: the apps for the selected files (or the cursor's); on a path button it opens a background tab, elsewhere it is Enter |
| Enter | open: a folder is entered, a file goes to `gio open` (Nemo's way: the type from its name; a terminal app such as nvim in a terminal); an empty file, which gio can't type, to the default app for its name's type (`gio launch`); several selected files, each default app once with all of its files (folders among them stay) |
| Backspace | up to the parent, the folder you left selected |
| Alt+Left, Alt+Right | back and forward |
| Alt+Up | up to the parent |
| Ctrl+L, Ctrl+Right | type a location: all of it selected, so typing replaces it; `~` is home, a relative path starts here; a file's path opens its folder with the file selected |
| Ctrl+K, Ctrl+Up | the keys to the path buttons: Left/Right (`h`/`l`), Home/End move, Enter goes there, Shift+Enter opens it in a background tab, Esc back to the list |
| `/`, Ctrl+F | find, in a bar over the job rows: type to jump (the bar counts the matches: "2 of 5"), Up/Down or its chevrons the previous and next match, Enter keeps (the bar stays, the keys go back to the list), Esc closes it and goes back, its × closes it; then `n`/`N` again. Alt+C match case, Alt+W whole words, Alt+R a regular expression (the toggles in the bar too; kept for the next find) |
| `.`, Ctrl+H | hidden files on and off |
| Ctrl+P, Ctrl+G | View → Show Permissions, View → Show Group |
| `s`, `S` | sort by the next column (name, size, modified), reverse |
| Tab, Ctrl+Left | the keys to the sidebar (a hidden one shows as places); Tab again back; in the tree, Left/Right (`h`/`l`) close and open a folder (or move up and down a level), Enter goes there |
| F9, Ctrl+S | the sidebar's next mode: places, tree, hidden |
| Ctrl+1, Ctrl+2 | the files as the list, as icons (also the two buttons at the right of the top bar; remembered) |
| Ctrl+=, Ctrl+-, Ctrl+0 | in the icon view, the icons a size bigger, smaller, back to 48 px (also Ctrl+wheel, and the slider at the right of the bottom bar; remembered) |
| `t`, Alt+Enter | a terminal in this folder (`xdg-terminal-exec --dir`) |
| Ctrl+I | Properties: size or a folder's contents (counted all the way down), location, drive, dates, owner, and the permissions as a grid: arrows (`h`/`j`/`k`/`l`) move, Space or Enter changes one at once (Ctrl+Z puts it back), Esc closes |
| `~`, Alt+Home | home |
| Alt+End | the trash view |
| Ctrl+D | bookmark the folder shown (in the GTK bookmarks file, shared with Nemo); the Bookmarks menu (Alt+B) lists the first 8 and goes there |
| Ctrl+B | Edit Bookmarks: every line of the file; Up/Down (`k`/`j`) move, Enter goes there, Delete removes, F2 renames, Alt+Up/Alt+Down move it, Esc closes |
| Space, Ctrl+A | select the entry (and move on), select all; Esc clears |
| Shift and a move | the arrows, Home/End or `H`/`J`/`K`/`L` with Shift: everything the cursor passes over joins the selection (as Shift+click), in the list and the icon view |
| F2 | rename, in the row, the name up to its extension selected (a folder's whole name): Enter renames, Esc cancels |
| In the rename and location lines | Shift+Left/Right/Home/End select, Ctrl+A all; Ctrl+C, Ctrl+X, Ctrl+V copy, cut and paste text (the first line; a name leaves `/` out) |
| Ctrl+Shift+N | a new folder, "New folder", named in the row |
| Delete | to the trash (the one Nemo shows); in the trash view, for good after asking |
| `r` | in the trash view: restore the selected entries where they came from |
| Shift+Delete | delete for good, after "Y/n" (Enter or `y` deletes) |
| Ctrl+C, Ctrl+X, Ctrl+V | copy, cut, paste; Nemo and terminals paste what ofx copied, ofx what Nemo copied |
| Ctrl+Z | undo the last rename, new folder, new file (while it is still empty), trash, copy or move |
| Ctrl+T, Ctrl+W | a new tab on this folder, close the tab (the last one closes the window) |
| Ctrl+N | a new window on this folder: another ofx, through `uwsm-app` |
| Ctrl+Tab, Ctrl+Shift+Tab | the next and previous tab (Ctrl+PgDn, Ctrl+PgUp too) |
| Alt+1 … Alt+9 | a tab by place, Alt+9 the last |
| Ctrl+Shift+PgUp, Ctrl+Shift+PgDn | move the tab left or right |
| Ctrl+J, Ctrl+Down | the keys to the find bar when it is open, then to the job rows' ×: Up/Down (`k`/`j`) move, Enter asks to cancel that job; Ctrl+J again or Esc back to the list |
| Ctrl+Q | close every ofx window (each asks first while its jobs run or wait) |
| F1, Ctrl+F1 | Keyboard Shortcuts: every key by where it works, type to filter, Esc closes (also Help, Alt+H) |
| Help → About | the name, the build's version (`ofx --version` too: the last commit's date and hash, `+dirty` with changes), the license; Esc, Enter or Close close it |
| F10, Alt+F, Alt+E, Alt+V, Alt+G, Alt+B, Alt+H | the File, Edit, View, Go, Bookmarks and Help menus: Up/Down (`k`/`j`) move, Left/Right (`h`/`l`) the next menu, Enter picks, Esc closes; also by the pointer. Each entry does what its key does; what can't be done now is greyed, the current sidebar, sort and view marked |
| Right click, Shift+F10, Menu key | the right-click menu: an entry's (an unmarked one becomes the selection; a marked one keeps the marks), or on empty space the folder shown's (New Folder, Paste, Select All, Terminal Here, Properties of the folder). By the keys it opens under the cursor's entry. On one folder it also opens it in a new window or a terminal, pastes into it, bookmarks it; on a link, Follow Link goes to what it points at. On a sidebar row or a path button, that folder's menu: open it (a tab, a window, a terminal), Cut, Copy (not a place), Paste Into Folder, Add Bookmark, Properties; a bookmark's Rename…, Move Up, Move Down, Remove; a drive's Eject; Trash's Empty Trash…. On a tab: New Tab, Move Tab Left and Right, Close, Close Other Tabs |
| View → Show Menu | the menu bar shown or hidden, remembered; hidden, F10 and Alt+F, E, V, G, B, H still open its menus (the bar shows while one is open), and a note says so as it hides |
| View → Show Colors | the list in `ls`'s (eza's) colours from the theme: permissions r, w, x; your owner and group; size, date; names by kind. On unless turned off, remembered |
| View → Show Permissions, Ctrl+P | the list's Owner and Permissions columns (`rwxr-xr-x`, a link's target's), on or off together, remembered, given way (the status line says so) while Name would have under 150 px; a link also shows where it leads after its → |
| View → Show Group, Ctrl+G | the Group column between them, off at first (as eza), remembered; greyed while Show Permissions is off |
| Ctrl+M | Make Link: "Link to <name>" beside each selected entry, pointing at its full path; Ctrl+Z removes them |
| Ctrl+Shift+A | Select Items Matching: the find bar as "Select", the same field and toggles; Enter makes the selection every match and closes it, Esc leaves the selection as it was |
| Ctrl+Shift+I | invert the selection |
| Ctrl+R | list the folder again (for a drive that sends no change events) |
| File, Open With Other Application… | the apps for the selected files (or the cursor's): the default, the recommended ones (they name the type), then the rest. Type to filter (any part of the name; Alt+C, Alt+W, Alt+R as in find), Tab or Down to the list, Up/Down (`k`/`j`) there, Enter opens all the files with it in one `gio launch`, Esc closes. Alt+D (or the box under the list): make it the default for the type too (`gio mime`, in `~/.config/mimeapps.list`; not for an empty file's type, nor for one `hyprland-mimeapps.list` sets) |

The File menu also has Create New Empty File ("New file", named in the row, as a new folder is).
Edit also has Duplicate (a copy beside each, "name (copy)", as a paste here makes; Ctrl+Z removes
it). View has the sidebar's modes and the sort column as entries of their own; Go has Trash.

An action works on the selected entries, or on the cursor's when none is selected. Copies, moves
and deletes for good run in the background, up to four at once, each with a row under the files:
a throughput line, how far it is and its speed, and a × that cancels it after asking (a cancelled
file never appears half-written). Up to 32 more wait their turn; a job on files another one uses
waits for it. Ctrl+Q with jobs running asks first. When a name is taken it asks: `s` skip, `r` replace (a folder: merge), `k`
keep both; the capitals answer for all the rest.

The top bar: back, forward and up, then the path as buttons (they keep the deeper folders after
you go up, and scroll with ‹ › when the path is long), then the location toggle and search.
Folders show how many items they hold in the Size column.

Tabs: each keeps its own folder, cursor, selection and history; sort and hidden files are the
window's. A middle click on a folder, a place or a path button opens it in a tab behind the
current one; a middle click on a tab, or its ×, closes it.

Mouse: a click puts the cursor on a row, Ctrl+click and Shift+click select, a double click opens,
the wheel scrolls a row per notch, a click on a column header sorts by it (again: reversed), a
click on a place goes there, and the back and forward buttons walk the history. The list refreshes by itself when the folder changes, and the
colours and icons follow a theme switch.

The icon-only buttons (back, forward, up, location, search, the views, the sidebar's modes, eject,
a tab's ×, the zoom's minus, plus and slider) show what they do and their key when the pointer rests on them.

The icon view (Ctrl+2): each entry an icon over its name (48 px at first; 32, 64, 96, 128 or 256
with the slider at the right of the bottom bar, Ctrl+= and Ctrl+-, or Ctrl+wheel; the list doesn't
zoom), wrapped to two lines and cut with
"…"; the cursor's name shows in full, over the cells below. Left/Right (`h`/`l`) move a cell and run
on across the rows (in the list they do nothing), Up/Down (`k`/`j`) a row, PgUp/PgDn a page of rows,
Home/End the first and last; Enter opens and Backspace goes up. Sort, find, the selection, rename,
tabs and the rest work as in the list; size and date are in the status line. The view is one for
every folder, remembered beside the sidebar's mode, and so is the size.

Previews, in the icon view: PNG, JPEG (turned as its EXIF says), GIF and WebP files show their
picture in the icon's place, its shape kept, made on two threads while the window stays live. They
are kept in the shared freedesktop cache, `~/.cache/thumbnails` (`normal` up to 128 device pixels,
`large` to 256, `x-large` to 512), so ofx shows the previews Nemo made (PDFs, slides) and Nemo the
ones ofx made. A file over 10 MiB, or on a network or FUSE drive, gets none made; one that won't
decode gets an entry in `fail/ofx/` and isn't tried again until it changes. The list view keeps its
icons.

The cache's cleanup, `ofx-clean` (nothing else cleans it, Nemo neither): a preview not read for 90
days goes; so does one whose file is gone or has changed (kept: files on a network drive or on a
drive not mounted now); then, while the cache is over 1 GiB, the least recently read go. It reads
the previews without changing their access times. `ofx-clean --dry-run` lists what would go and
deletes nothing. `make install` puts in a daily systemd user timer but leaves it off; after a look
at the dry run:

```sh
systemctl --user enable --now ofx-clean.timer
journalctl --user -u ofx-clean   # one line a day: what went, and the MiB freed and kept
```

The sidebar, in one of three modes, picked with the three buttons at the left of the bottom bar
or F9 (remembered in `~/.local/state/ofx/state`):
- **places**: under *Places*, Home, your GTK bookmarks (`~/.config/gtk-3.0/bookmarks`, the ones
  Nemo shows) and Trash; under *Drives*, `/` and what is mounted under `/mnt`, `/media` and
  `/run/media`, each with its free space and a bar of how full it is (with the pointer over it,
  its used space). A drive mounted or unmounted shows at once; a removable one shows an eject
  button in its icon's place under the pointer (`udisksctl unmount`, then `power-off`).
- **tree**: the folders from `/`, opened down to the one shown.
- **hidden**: no sidebar.

The bottom bar's status part: what just happened, until the next key or click; else the selected
entry (`"adir" selected (12 items)`), how many are selected, or the folder's count and its drive's
free space.

The trash view (Trash in the sidebar): every trash, the home one and each drive's, as one list;
each entry with the folder it came from and when it was trashed. Restore (or `r`) puts the
selected entries back, making their folder again if it is gone; Delete deletes them for good;
Empty Trash deletes all of it after asking. Rename, paste and a new folder don't work there.

## Where things come from

- Colours: `~/.local/state/omarchy/current/theme/colors.toml` (`$XDG_STATE_HOME`), Tokyo Night for
  anything missing.
- Icons: the theme in `icons.theme` next to it (Yaru-magenta here), its parents, then hicolor; PNG
  only, at the window's scale.
- MIME types: shared-mime-info's `globs2` by file name.
- Programs: through `uwsm-app` when it is there, as Omarchy launches apps.
- The trash: `~/.local/share/Trash` (`$XDG_DATA_HOME`) for the home drive, a drive's own
  `.Trash-$UID` (or `.Trash/$UID`) for others, by the freedesktop spec.
- The clipboard: `ext-data-control-v1` (Hyprland has it), as `x-special/gnome-copied-files`,
  `text/uri-list` and plain paths. Without it the clipboard stays inside ofx.

`docs/plan.md` has the plan and what is next.

## License

MIT (`LICENSE`). The vendored raylib, Clay and nanosvg are zlib-licensed (their own files under
`vendor/`).

## Testing

`make test` runs the unit tests. `tools/mutation/run.py` mutates `fs/` and `ui/` one site at a
time against them, `tools/mutation/gui.py` the window against the GUI scripts. GUI scripts run in
a private headless weston, never on the desktop: see `CLAUDE.md` and `tests/gui/run.sh`.
`build/ofx-probe DIR` prints a folder as ofx sees it (types and icon files), without a window.
`build/ofx-clean` is the cache's cleanup: it deletes in `~/.cache/thumbnails`, so try it with
`--dry-run`, or with `XDG_CACHE_HOME` on a copy.
