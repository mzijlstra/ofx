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
| `/`, Ctrl+F | find: type to jump, Enter keeps, Esc goes back; then `n`/`N` again |
| `.`, Ctrl+H | hidden files on and off |
| `s`, `S` | sort by the next column (name, size, modified), reverse |
| Tab | the keys to the places in the sidebar and back |
| `t` | a terminal in this folder (`xdg-terminal-exec --dir`) |
| `~` | home |
| Ctrl+Q | quit |

Mouse: a click selects, a double click opens, the wheel scrolls a row per notch, a click on a
column header sorts by it (again: reversed), a click on a place goes there, and the back and
forward buttons walk the history. The list refreshes by itself when the folder changes, and the
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

`docs/plan.md` has the plan and what is next.

## Testing

`make test` runs the unit tests. `tools/mutation/run.py` mutates `fs/` and `ui/` one site at a
time against them. GUI scripts run on a Hyprland headless output, never on the desktop: see
`CLAUDE.md` and `tests/gui/run.sh`. `build/fx-probe DIR` prints a folder as fx sees it (types and
icon files), without a window.
