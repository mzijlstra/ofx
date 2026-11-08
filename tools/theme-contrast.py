#!/usr/bin/env python3
"""How the themes' own colours read under every Omarchy theme (2026-11-07), as the themes give them,
before ofx fits them (fs/theme.c's theme_fit and theme_fit_list do that, tested in
tests/test_theme_fit.c). Read-only: it reads /usr/share/omarchy/themes/*/colors.toml (and
~/.config/omarchy/themes/*/colors.toml) and prints; nothing is written.

    tools/theme-contrast.py           the drive usage line's colours
    tools/theme-contrast.py --all     every colour ofx draws text or a bar in

Contrast is WCAG's ratio against the sidebar's three backgrounds (lighter_background: a row;
background: hover; selection: the cursor), the worst of the three; dE is CIE76 between the used
colour and the warning. Flags: ACC/RED under 3:1, FREE the track under 1.3, SAME a dE under 20.
"""
import glob, math, os, re

def hexrgb(h): h = h.strip().strip('"').lstrip('#'); return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
def lin(c): c = c / 255; return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
def lum(rgb): r, g, b = map(lin, rgb); return 0.2126 * r + 0.7152 * g + 0.0722 * b
def cr(a, b): la, lb = lum(a), lum(b); return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)
def mix(a, b, t): return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))
def lab(rgb):
    r, g, b = map(lin, rgb)
    x, y, z = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047, 0.2126 * r + 0.7152 * g + 0.0722 * b, (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    f = lambda t: t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    return (116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z)))
def dE(a, b): return math.dist(lab(a), lab(b))

def load(f):
    t = {}
    for line in open(f):
        m = re.match(r'\s*(\w+)\s*=\s*"?(#[0-9a-fA-F]{6})', line)
        if m: t[m.group(1)] = hexrgb(m.group(2))
    return t

files = sorted(glob.glob('/usr/share/omarchy/themes/*/colors.toml') + glob.glob(os.path.expanduser('~/.config/omarchy/themes/*/colors.toml')))
themes = [(f.split('/')[-2], load(f)) for f in files]
need = ('lighter_background', 'background', 'selection', 'accent', 'red', 'dark_foreground', 'foreground')

def worst(c, bgs): return min(cr(c, b) for b in bgs)

print('== now: used = accent, warning = red (90% full), free = 75% dark_foreground, 25% lighter_background')
print(f"{'theme':18} {'used':>5} {'warn':>5} {'free':>5} {'dE':>6}")
for n, t in themes:
    if any(k not in t for k in need): print(f'{n:18} missing {[k for k in need if k not in t]}'); continue
    bgs = [t['lighter_background'], t['background'], t['selection']]
    free = mix(t['dark_foreground'], t['lighter_background'], 0.25)   # drive_free_color: 75% dark_foreground (C's mix is t of a)
    a, r, fr, d = worst(t['accent'], bgs), worst(t['red'], bgs), cr(free, t['lighter_background']), dE(t['accent'], t['red'])
    flag = ' '.join(x for x, c in (('ACC', a < 3), ('RED', r < 3), ('FREE', fr < 1.3), ('SAME', d < 20)) if c)
    print(f'{n:18} {a:5.2f} {r:5.2f} {fr:5.2f} {d:6.1f}  {flag}')


# ---- every colour ofx draws text or a bar in (--all, 2026-11-07) ------------------------------------
# Each role: (what, where in gui/, its colour, the backgrounds it is drawn on, the ratio it needs).
# Text needs 3:1 here (WCAG's large-text and non-text line: ofx's text is small, so 4.5 is listed as
# well); the drive bar's track 1.3 against its row (a shape, only to be seen). The terminal colours
# (blue, cyan, ...) are fs/theme.c's: the theme's normal ones.
import sys
def roles(t):
    g = lambda k: t[k]
    bg, bgl, sel, acc, fg, fgd = g('background'), g('lighter_background'), g('selection'), g('accent'), g('foreground'), g('dark_foreground')
    marked = mix(acc, bg, 0.72)                 # mix(t->accent, t->bg, 0.28f): 28% accent
    rows, cols, side, foot = [bg, bgl], [bg, bgl, sel, marked], [bgl, bg, sel], [bgl, bg]
    free = mix(fgd, bgl, 0.25)                  # drive_free_color: 75% dark_foreground
    r = [('name: folder (blue)', g('blue'), rows), ('name: link (cyan)', g('cyan'), rows),
         ('name: build/README (yellow)', g('yellow'), rows), ('name: executable (green)', g('green'), rows),
         ('name: image/video (magenta)', g('magenta'), rows), ('name: archive, broken link (red)', g('red'), rows),
         ('name: plain (fg)', fg, rows + [sel]), ('name: marked (accent)', acc, [marked]),
         ('column: size (green)', g('green'), cols), ('column: date (blue)', g('blue'), cols),
         ('column: owner, git M (yellow)', g('yellow'), cols), ('column: git R (cyan)', g('cyan'), cols),
         ('column: git C, perm w (red)', g('red'), cols), ('column: perm other (magenta)', g('magenta'), cols),
         ('column: dimmed (dark_fg)', fgd, cols),
         ('sidebar: name (fg)', fg, side), ('sidebar: dim (dark_fg)', fgd, side), ('sidebar: here (accent)', acc, side),
         ('sidebar: free number', free, side), ('drive: used (accent)', acc, side), ('drive: 90% (red)', g('red'), side),
         ('footer: text (fg)', fg, foot), ('footer: dim (dark_fg)', fgd, foot), ('footer: status bad (red)', g('red'), foot),
         ('footer: accent', acc, foot), ('list: error (red)', g('red'), [bg]),
         ('badge: op label (bg on accent)', bg, [acc]),
         ('job/spring: fill (accent) vs track', acc, [mix(acc, bgl, 0.8)])]
    return r

if '--all' in sys.argv:
    res = {}
    for n, t in themes:
        if any(k not in t for k in need + ('blue', 'cyan', 'yellow', 'green', 'magenta')): continue
        for what, c, bgs in roles(t): res.setdefault(what, []).append((n, worst(c, bgs)))
        bgl = t['lighter_background']
        res.setdefault('drive: track (free) vs row', []).append((n, cr(mix(t['dark_foreground'], bgl, 0.25), bgl)))
    print(f'\n== every role, worst over its backgrounds: themes under 3:1 (the track: under 1.3); [n under 4.5]')
    for what, xs in res.items():
        lim = 1.3 if 'track (free)' in what else 3
        bad = sorted((v, n) for n, v in xs if v < lim); aa = sum(v < 4.5 for _, v in xs)
        print(f"{what:34} {len(bad):2}/{len(xs)} [{aa:2}]  " + ' '.join(f'{n} {v:.2f}' for v, n in bad))
