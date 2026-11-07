#!/usr/bin/env python3
"""How the drive usage line's colours read under every Omarchy theme (2026-11-07), and what the
proposed theme-derived pick gives instead. Read-only: it reads /usr/share/omarchy/themes/*/colors.toml
(and ~/.config/omarchy/themes/*/colors.toml) and prints; nothing is written.

    tools/theme-contrast.py           the current colours, then the proposed pick

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
def push(c, fg, bgs, target):   # toward the foreground until it reads: the hue kept, the lightness moved
    for k in range(11):
        x = mix(c, fg, k / 10)
        if worst(x, bgs) >= target: return x, k / 10
    return mix(c, fg, 1), 1.0

print('== now: used = accent, warning = red (90% full), free = dark_foreground 75% toward lighter_background')
print(f"{'theme':18} {'used':>5} {'warn':>5} {'free':>5} {'dE':>6}")
for n, t in themes:
    if any(k not in t for k in need): print(f'{n:18} missing {[k for k in need if k not in t]}'); continue
    bgs = [t['lighter_background'], t['background'], t['selection']]
    free = mix(t['dark_foreground'], t['lighter_background'], 0.75)
    a, r, fr, d = worst(t['accent'], bgs), worst(t['red'], bgs), cr(free, t['lighter_background']), dE(t['accent'], t['red'])
    flag = ' '.join(x for x, c in (('ACC', a < 3), ('RED', r < 3), ('FREE', fr < 1.3), ('SAME', d < 20)) if c)
    print(f'{n:18} {a:5.2f} {r:5.2f} {fr:5.2f} {d:6.1f}  {flag}')

print('\n== proposed: warning = the best-reading of red/bright_red/orange, used = accent unless it is too like it,')
print('   each pushed toward the foreground to 3:1; the track set to 1.35:1 (+k: how far it was pushed)')
print(f"{'theme':18} {'used':>5} {'warn':>5} {'dE':>6} {'track':>5}  used from / warning from")
for n, t in themes:
    if any(k not in t for k in need): continue
    bgs = [t['lighter_background'], t['background'], t['selection']]; fg = t['foreground']
    warm = [k for k in ('red', 'bright_red', 'orange') if k in t]
    wk = max(warm, key=lambda k: worst(t[k], bgs)); warn, wt = push(t[wk], fg, bgs, 3)
    cands = [k for k in ('blue', 'cyan', 'green', 'magenta', 'yellow', 'bright_blue', 'bright_cyan') if k in t]
    uk = 'accent' if dE(t['accent'], warn) >= 30 else max(cands, key=lambda k: (dE(t[k], warn) >= 30, worst(t[k], bgs)))
    used, ut = push(t[uk], fg, bgs, 3)
    bgl, fgd = t['lighter_background'], t['dark_foreground']
    track = next((mix(bgl, fgd, k / 20) for k in range(21) if cr(mix(bgl, fgd, k / 20), bgl) >= 1.35), fgd)
    flag = ' '.join(x for x, c in (('SAME', dE(used, warn) < 20), ('TRACK', cr(track, bgl) < 1.3)) if c)
    print(f"{n:18} {worst(used, bgs):5.2f} {worst(warn, bgs):5.2f} {dE(used, warn):6.1f} {cr(track, bgl):5.2f}  "
          f"{uk}{'+%.1f' % ut if ut else ''} / {wk}{'+%.1f' % wt if wt else ''}  {flag}")
