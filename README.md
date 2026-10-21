# fx

A keyboard-first file explorer for Omarchy: one folder at a time as a list, in the current
theme's colours and icons. C with arenas, Clay for layout, raylib on native Wayland. It idles at
about 0% CPU: it draws only when something changes.

## Build and run

```sh
make            # build/fx, debug with ASan and UBSan
make release    # build/release/fx
make test       # unit tests for fs/ and ui/
make install    # build/release/fx into ~/.local/bin
fx [FOLDER]     # the current folder by default
```

Needs a C11 compiler, `wayland-scanner`, and the pkg-config packages wayland-client,
wayland-cursor, wayland-egl, xkbcommon, fontconfig and freetype2. raylib and Clay are vendored.

## Use

| Key | |
|---|---|
| Up/Down, `j`/`k` | move |
| PgUp/PgDn | a page |
| Home/End, `g`/`G` | first, last |
| Enter, Right, `l` | open: a folder is entered, a file goes to `xdg-open` |
| Backspace, Left, `h` | up to the parent, the folder you left selected |
| Alt+Left, Alt+Right | back and forward |
| Alt+Up | up to the parent |
| Ctrl+L | type a location: all of it selected, so typing replaces it; `~` is home, a relative path starts here |
| Ctrl+K | the keys to the path buttons: Left/Right, Home/End move, Enter goes there, Shift+Enter opens it in a background tab, Esc back to the list |
| `/`, Ctrl+F | find: type to jump, Enter keeps, Esc goes back; then `n`/`N` again |
| `.`, Ctrl+H | hidden files on and off |
| `s`, `S` | sort by the next column (name, size, modified), reverse |
| Tab | the keys to the places in the sidebar and back |
| `t` | a terminal in this folder (`xdg-terminal-exec --dir`) |
| `~` | home |
| Space, Ctrl+A | select the entry (and move on), select all; Esc clears |
| F2 | rename, in the row: Enter renames, Esc cancels |
| In the rename and location lines | Shift+Left/Right/Home/End select, Ctrl+A all; Ctrl+C, Ctrl+X, Ctrl+V copy, cut and paste text (the first line; a name leaves `/` out) |
| Ctrl+Shift+N | a new folder, "New folder", named in the row |
| Delete | to the trash (the one Nemo shows) |
| Shift+Delete | delete for good, after "Y/n" (Enter or `y` deletes) |
| Ctrl+C, Ctrl+X, Ctrl+V | copy, cut, paste; Nemo and terminals paste what fx copied, fx what Nemo copied |
| Ctrl+Z | undo the last rename, new folder, trash, copy or move |
| Ctrl+T, Ctrl+W | a new tab on this folder, close the tab (the last one closes fx) |
| Ctrl+Tab, Ctrl+Shift+Tab | the next and previous tab (Ctrl+PgDn, Ctrl+PgUp too) |
| Alt+1 … Alt+9 | a tab by place, Alt+9 the last |
| Ctrl+Shift+PgUp, Ctrl+Shift+PgDn | move the tab left or right |
| Ctrl+Q | quit |

An action works on the selected entries, or on the cursor's when none is selected. A copy or move
runs in the background with its progress in the footer (Esc cancels; a cancelled file never
appears half-written). When a name is taken it asks: `s` skip, `r` replace (a folder: merge), `k`
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

The sidebar: Home, your GTK bookmarks (`~/.config/gtk-3.0/bookmarks`, the ones Nemo shows), `/`
and what is mounted under `/mnt`, `/media` and `/run/media`.

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
  `text/uri-list` and plain paths. Without it the clipboard stays inside fx.

`docs/plan.md` has the plan and what is next.

## Testing

`make test` runs the unit tests. `tools/mutation/run.py` mutates `fs/` and `ui/` one site at a
time against them, `tools/mutation/gui.py` the window against the GUI scripts. GUI scripts run in
a private headless weston, never on the desktop: see `CLAUDE.md` and `tests/gui/run.sh`.
`build/fx-probe DIR` prints a folder as fx sees it (types and icon files), without a window.
