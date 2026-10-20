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
| `t` | a terminal in this folder (`xdg-terminal-exec --dir`) |
| `~` | home |
| Ctrl+Q | quit |

Mouse: a click selects, a double click opens, the wheel scrolls a row per notch, the back button
goes up. The list refreshes by itself when the folder changes, and the colours and icons follow
a theme switch. Dot-files are hidden (F2 adds a toggle).

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
