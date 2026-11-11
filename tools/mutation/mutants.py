# F1's mutants: (file, old, new), `old` exactly once in the file. tools/mutation/run.py runs them.
L, M, I, T, P, U = 'fs/list.c', 'fs/mime.c', 'fs/icons.c', 'fs/theme.c', 'fs/spawn.c', 'ui/ui.c'
KY, GK = 'ui/keys.c', 'gui/glfwkeys.c'   # (F15)
MUTANTS = [
    # ---- natural order, listing
    (L, "return k == K_DIR || k == K_LINK_DIR;", "return k == K_DIR;"),
    (L, "return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;", "return c;"),
    (L, "while (i0 < a.len && a.p[i0] == '0') i0++;", ""),
    (L, "if (i1 - i0 != j1 - j0) return i1 - i0 < j1 - j0 ? -1 : 1;", ""),
    (L, "if (i1 - i0 != j1 - j0) return i1 - i0 < j1 - j0 ? -1 : 1;", "if (i1 - i0 != j1 - j0) return i1 - i0 > j1 - j0 ? -1 : 1;"),
    (L, "if (c) return c < 0 ? -1 : 1;", "if (c) return c > 0 ? -1 : 1;"),
    (L, "if (i1 - i != j1 - j) return i1 - i < j1 - j ? -1 : 1;", ""),
    (L, "if (i1 - i != j1 - j) return i1 - i < j1 - j ? -1 : 1;", "if (i1 - i != j1 - j) return i1 - i > j1 - j ? -1 : 1;"),
    (L, "if (x != y) return x < y ? -1 : 1;", "if (x != y) return x > y ? -1 : 1;"),
    (L, "if (i < a.len || j < b.len) return i < a.len ? 1 : -1;", "if (i < a.len || j < b.len) return i < a.len ? -1 : 1;"),
    (L, "return c < 0 ? -1 : c > 0 ? 1 : a.len < b.len ? -1 : a.len > b.len ? 1 : 0;", "return 0;"),
    (L, "if (kind_is_dir(a->kind) != kind_is_dir(b->kind)) return kind_is_dir(a->kind) ? -1 : 1;", ""),
    (L, "if (fstatat(dirfd, name, &tg, 0) != 0) return K_LINK_BROKEN;", "if (fstatat(dirfd, name, &tg, 0) != 0) return K_OTHER;"),
    (L, "return S_ISDIR(tg.st_mode) ? K_LINK_DIR : K_LINK_FILE;", "return K_LINK_FILE;"),
    (L, "return S_ISDIR(st->st_mode) ? K_DIR : S_ISREG(st->st_mode) ? K_FILE : K_OTHER;", "return S_ISDIR(st->st_mode) ? K_DIR : K_FILE;"),
    (L, "            out->hidden++;\n", ""),
    (L, "        if (nm[0] == '.') {", "        if (nm[0] == '.' && nm[1] == 0) {"),
    (L, "memcpy(grown, e, (size_t)n * sizeof *e);", ""),
    (L, ".size = kind_is_dir(k) || k == K_LINK_BROKEN ? 0 : (i64)st.st_size,", ".size = (i64)st.st_size,"),
    (L, ".mtime = k == K_LINK_BROKEN ? 0 : (i64)st.st_mtim.tv_sec,", ".mtime = (i64)st.st_mtim.tv_sec,"),
    (L, "    fs_sort(out, sort);\n", ""),
    (L, "if (str_eq(l->e[i].name, name) && str_eq(l->e[i].dir, dir)) return i;", "if (str_eq(l->e[i].name, name) && str_eq(l->e[i].dir, dir)) return i + 1;"),
    # ---- MIME
    (M, "for (isize i = 0; i < s.len; i++) p[i] = (char)fold((unsigned char)s.p[i]);", "for (isize i = 0; i < s.len; i++) p[i] = s.p[i];"),
    (M, "if (g.p[i] == '*' || g.p[i] == '?' || g.p[i] == '[') return false;", "if (g.p[i] == '*' || g.p[i] == '?') return false;"),
    (M, "if (g.len < 3 || g.p[0] != '*' || g.p[1] != '.') return false;", "if (g.len < 3 || g.p[0] != '*') return false;"),
    (M, "if (!glob_is_literal(rest)) return false;", ""),
    (M, "if (a->weight != b->weight) return a->weight > b->weight;", "if (a->weight != b->weight) return a->weight < b->weight;"),
    (M, "if (a->glob.len != b->glob.len) return a->glob.len > b->glob.len;", ""),
    (M, "if (i_folded != j_folded) return !i_folded;", ""),
    (M, "return a->order < b->order;", "return a->order > b->order;"),
    (M, "if (!glob_better(db, i, have, false, false)) return;", ""),
    (M, "if (m->keys[s].p && str_eq(m->keys[s], key)) m->vals[s] = (int)i;", ""),
    (M, "if (line.len == 0 || line.p[0] == '#' || db->n == MIME_GLOBS_MAX) continue;", "if (line.len == 0 || db->n == MIME_GLOBS_MAX) continue;"),
    (M, "if (type.len == 0 || glob.len == 0) continue;", "if (glob.len == 0) continue;"),
    (M, "cs |= str_eq(str_cut(flags, fat, e), S(\"cs\"));", ""),
    (M, "if (glob_is_literal(glob)) map_best(db, cs ? &db->literal_cs : &db->literal_ci, cs ? glob : lower(a, glob), i);", "if (glob_is_literal(glob)) map_best(db, &db->literal_cs, glob, i);"),
    (M, "if (strmap_get(m, type, &have)) continue;", ""),
    (M, "if (dh && dh[0]) out[n++] = arena_cstr(a, str_from_c(dh));", "if (0) {}"),
    (M, "if (e > at) out[n++] = arena_cstr(a, str_cut(dirs, at, e));", "out[n++] = arena_cstr(a, str_cut(dirs, at, e));"),
    (M, "if (best < 0) best = map_get(&db->literal_ci, low);", ""),
    (M, "if (cs >= 0 && ci >= 0) best = glob_better(db, cs, ci, false, true) ? cs : ci;", "if (cs >= 0 && ci >= 0) best = ci;"),
    (M, "if (fnmatch(pat, g->cs ? cname : clow, 0) != 0) continue;", "if (fnmatch(pat, cname, 0) != 0) continue;"),
    (M, "if (best < 0 || glob_better(db, i, best, folded, best_folded)) best = i, best_folded = folded;", "if (best < 0) best = i, best_folded = folded;"),
    (M, "if (e->kind == K_LINK_BROKEN) return S(\"inode/symlink\");", ""),
    (M, "return (e->mode & 0111) ? S(\"application/x-executable\") : S(\"application/octet-stream\");", "return S(\"application/octet-stream\");"),
    (M, "if (strmap_get(&db->icon, t, &i)) names[n++] = db->icon_of[i];", ""),
    (M, "if (*p == '/') *p = '-';", ""),
    (M, "if (strmap_get(&db->generic, t, &i)) names[n++] = db->icon_of[i];", ""),
    (M, "        if (up == 0) names[n++] = str_from_c(arena_sprintf(a, \"%.*s-x-generic\", STR_ARG(media)));", "        if (up == 0) names[n++] = S(\"text-x-generic\");"),
    # ---- icons
    (I, "in = line.len >= 2 && line.p[line.len - 1] == ']' && str_eq(str_cut(line, 1, line.len - 1), section);", "in = str_eq(str_cut(line, 1, line.len - 1), section);"),
    ('fs/icons.c', '    i64 n = v.len > 9 ? -1 :', '    i64 n = v.len > 10 ? -1 :'),
    ('fs/icons.c', '    return n < 0 ? dflt : (int)n;', '    return (int)n;'),
    (I, "if (item->len) return true;", "return true;"),
    (I, "if (home && home[0]) out[n++] = arena_sprintf(a, \"%s/.icons\", home);", ""),
    (I, "if ((!png && !is_svg) || s.len == 4) continue;", "if (s.len == 4) continue;"),
    (I, "memcpy(grown, names, (size_t)n * sizeof *names), memcpy(kgrown, kind, (size_t)n);", "memcpy(kgrown, kind, (size_t)n);"),
    (I, "return context.len == 0 || str_eq(context, S(\"Places\")) || str_eq(context, S(\"MimeTypes\")) ||", "return true || str_eq(context, S(\"Places\")) || str_eq(context, S(\"MimeTypes\")) ||"),
    (I, "if (depth > 8 || db->n == ICON_THEMES_MAX || icon_has_theme(db, name)) return;", "if (depth > 8 || db->n == ICON_THEMES_MAX) return;"),
    (I, "for (int list = 0; list < 2; list++) {", "for (int list = 0; list < 1; list++) {"),
    (I, ".scale = ini_int(index, sub, S(\"Scale\"), 1),", ".scale = 1,"),
    (I, ".min = ini_int(index, sub, S(\"MinSize\"), size),", ".min = size,"),
    (I, ".max = ini_int(index, sub, S(\"MaxSize\"), size),", ".max = size,"),
    (I, ".threshold = ini_int(index, sub, S(\"Threshold\"), 2),", ".threshold = 0,"),
    (I, ".type = str_eq(type, S(\"Fixed\")) ? IT_FIXED : str_eq(type, S(\"Scalable\")) ? IT_SCALABLE : IT_THRESHOLD,", ".type = str_eq(type, S(\"Fixed\")) ? IT_FIXED : IT_THRESHOLD,"),
    (I, "for (isize at = 0; next_item(inherits, &at, &parent);) icon_add_theme(a, db, bases, nb, parent, depth + 1, apps);", ""),
    (I, "icon_add_theme(a, db, bases, nb, S(\"hicolor\"), 0, apps);", ""),
    (I, "if (d->scale != scale) return false;", ""),
    (I, "case IT_SCALABLE: return d->min <= size && size <= d->max;", "case IT_SCALABLE: return d->min <= size;"),
    (I, "default: return d->size - d->threshold <= size && size <= d->size + d->threshold;", "default: return d->size == size;"),
    (I, "case IT_FIXED: lo = hi = d->size * d->scale; break;", "case IT_FIXED: lo = hi = d->size; break;"),
    (I, "return want < lo ? lo - want : want > hi ? want - hi : 0;", "return want < lo ? lo - want : 0;"),
    (I, "if (icon_dir_matches(&t->dirs[i], size, scale) && strmap_get(&t->dirs[i].names, name, &have)) return &t->dirs[i];", "if (0 && icon_dir_matches(&t->dirs[i], size, scale) && strmap_get(&t->dirs[i].names, name, &have)) return &t->dirs[i];"),
    (I, "(best_short == short_ && dist < best_d)) best", "(best_short == short_ && dist > best_d)) best"),
    (I, "    for (isize k = 0; k < nnames; k++)\n        for (isize t = 0; t < db->n; t++) {", "    for (isize t = 0; t < db->n; t++)\n        for (isize k = 0; k < nnames; k++) {"),
    # ---- theme
    (T, "if (v.len >= 2 && (v.p[0] == '\"' || v.p[0] == '\\'') && v.p[v.len - 1] == v.p[0]) v = str_cut(v, 1, v.len - 1);", "if (v.len >= 2 && (v.p[0] == '\"' || v.p[0] == '\\'')) v = str_cut(v, 1, v.len - 1);"),
    (T, "if (v.len != 7 || v.p[0] != '#') return false;", "if (v.len != 7) return false;"),
    (T, "*out = (Rgb){ (u8)(d[0] * 16 + d[1]), (u8)(d[2] * 16 + d[3]), (u8)(d[4] * 16 + d[5]) };", "*out = (Rgb){ (u8)(d[0] * 16 + d[1]), (u8)(d[2] * 16 + d[3]), (u8)(d[4] * 16) };"),
    (T, "if (line.len && line.p[0] == '[') break;", ""),
    (T, "if (close >= 0) val = str_cut(val, 0, close + 2);", ""),
    (T, "t->ncolors++;", ""),
    (T, "if (icons.len < (isize)sizeof t.icons && str_find(icons, '/') < 0 && str_find(icons, '\\n') < 0)", "if (icons.len < (isize)sizeof t.icons)"),
    ('fs/places.c', 'if (val.len >= 2 && val.p[0] == \'"\' && val.p[val.len - 1] == \'"\') val = str_cut(val, 1, val.len - 1);', ''),
    ('fs/places.c', "while (val.len > 1 && val.p[val.len - 1] == '/') val.len--;", ""),
    ('fs/places.c', 'else if (val.len && val.p[0] == \'/\') snprintf(path, sizeof path, "%.*s", STR_ARG(val));', ''),
    ('fs/places.c', "if (strcmp(path, home) == 0 || u->n == COUNT(u->d)) continue;", "if (u->n == COUNT(u->d)) continue;"),
    # ---- spawn
    ('fs/guard.c', '    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;', '    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, F_OK) == 0;'),
    ('fs/spawn.c', '    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK);', '    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);'),
    (P, "if (on_path(\"uwsm-app\")) argv[k++] = \"uwsm-app\", argv[k++] = \"--\";", ""),
    (P, "snprintf(arg, sizeof arg, \"--dir=%s\", dir);", "snprintf(arg, sizeof arg, \"%s\", dir);"),
    # ---- ui
    ('ui/cursor.c', "    return all > rows ? (all - rows) * c : 0;", "    return all > rows ? (all - rows + 1) * c : 0;"),
    ('ui/cursor.c', "    if (row < top) top = row;\n", ""),
    ('ui/cursor.c', "    if (row >= top + rows) top = row - rows + 1;\n", ""),
    ('ui/cursor.c', "l->sel = n > 0 ? ui_clamp(sel, 0, n - 1) : 0;", "l->sel = ui_clamp(sel, 0, n - 1);"),
    ('ui/cursor.c', "    l->rows = rows;\n    l->top = ui_top_at(l, l->top);\n    ui_show_sel(l);", "    l->rows = rows;"),
    ('ui/cursor.c', "    if (l->n == 0) return;\n    l->sel", "    l->sel"),
    ('ui/handle.c', "isize page = m->rows > 1 ? m->rows - 1 : 1;", "isize page = m->rows;"),
    ('ui/handle.c', "    if (!prompt && in->wheel) ui_scroll(l, in->wheel);", "    if (!prompt && in->wheel) ui_scroll(l, -in->wheel);"),
    ('ui/handle.c', "    if (!prompt && in->wheel) ui_scroll(l, in->wheel);", "    if (in->wheel) ui_scroll(l, in->wheel);"),
    ('ui/handle.c', "    if (!prompt && in->click >= 0 && l->top + in->click < l->n) {", "    if (!prompt && in->click >= 0) {"),
    ('ui/handle.c', "if (!prompt && in->dclick >= 0 && l->top + in->dclick < l->n) {", "if (!prompt && in->dclick >= 0) {"),
    ('ui/handle.c', '    if (!prompt && in->backs > 0) return *out = (UiAction){ UA_BACK, 0, false }, true;\n', ''),
    ('ui/handle.c', 'if (files) return *out = (UiAction){ UA_OPEN, l->sel, u->marked > 0 }, true;', 'if (u->focus != UI_FOCUS_SIDE) return *out = (UiAction){ UA_OPEN, l->sel, u->marked > 0 }, true;'),
    ('ui/helpers.c', "if (p->row == row && row >= 0 && now - p->at <= UI_DCLICK_MS) {", "if (p->row == row && now - p->at <= UI_DCLICK_MS) {"),
    ('ui/helpers.c', "if (p->row == row && row >= 0 && now - p->at <= UI_DCLICK_MS) {", "if (p->row == row && row >= 0 && now - p->at < UI_DCLICK_MS) {"),
    ('ui/helpers.c', "        p->row = -1;\n        return 2;", "        return 2;"),
    ('ui/helpers.c', "return q < m ? q : m;", "return q;"),
    ('ui/helpers.c', "if (w + a > room) break;\n            w += a, *from = j;", "if (w + a >= room) break;\n            w += a, *from = j;"),
    ('ui/helpers.c', "while (j > 0 && ((unsigned char)s.p[j] & 0xC0) == 0x80) j--;", ""),
    ('ui/helpers.c', "if (w + a > room) break;\n        w += a, *to = i;", "if (w + a >= room) break;\n        w += a, *to = i;"),
    ('ui/helpers.c', "if (ui_width(s, adv) <= width) return true;", "if (ui_width(s, adv) < width) return true;"),
    ('ui/helpers.c', "if (v + 0.5 >= 1024 && u < COUNT(UNITS) - 1) v /= 1024, u++;", ""),
    ('ui/helpers.c', "if (v < 9.95) snprintf(buf, cap, \"%.1f %s\", v, UNITS[u]);", "if (v < 10) snprintf(buf, cap, \"%.1f %s\", v, UNITS[u]);"),
    ('ui/helpers.c', "if (size < 1024) {", "if (size <= 1024) {"),
    ('ui/helpers.c', "if (tm.tm_year == tn.tm_year && tm.tm_yday == tn.tm_yday) strftime(buf, cap, \"%H:%M\", &tm);", "if (tm.tm_yday == tn.tm_yday) strftime(buf, cap, \"%H:%M\", &tm);"),
    ('ui/helpers.c', "else if (tm.tm_year == tn.tm_year) strftime(buf, cap, \"%b %e\", &tm);", ""),
]

PL = 'fs/places.c'
MUTANTS += [
    # ---- F2: sort, hidden
    (L, "if (s->key == SORT_SIZE && a->size != b->size) c = a->size < b->size ? -1 : 1;", "if (s->key == SORT_SIZE && a->size != b->size) c = a->size > b->size ? -1 : 1;"),
    (L, "else if (s->key == SORT_MTIME && a->mtime != b->mtime) c = a->mtime < b->mtime ? -1 : 1;", ""),
    (L, "if (c == 0) c = natural_cmp(a->name, b->name);", ""),
    (L, "return s->reverse ? -c : c;", "return c;"),
    (L, "            if (!show_hidden) continue;", ""),
    (L, "        if (strcmp(nm, \".\") == 0 || strcmp(nm, \"..\") == 0) continue;\n        if (nm[0] == '.') {", "        if (nm[0] == '.') {"),
    # ---- F2: places
    (PL, "if (pl->n == PLACES_MAX || !path[0] || places_has(pl, kind, path)) return;", "if (pl->n == PLACES_MAX || !path[0]) return;"),
    (PL, "if (label.len == 0) label = strcmp(path, \"/\") == 0 ? S(\"/\") : path_base(str_from_c(path));", ""),
    (PL, "for (size_t n = strlen(path); n > 1 && path[n - 1] == '/';) path[--n] = 0;", ""),
    (PL, "if (home && home[0]) places_add(pl, PL_HOME, home, S(\"Home\"));", ""),
    (PL, "    case PL_UNMOUNTED: names[n++] = S(\"drive-removable-media\"), names[n++] = S(\"drive-harddisk\"); break;", "    case PL_UNMOUNTED: names[n++] = S(\"drive-harddisk\"); break;"),
    # ---- F2: find
    ('ui/find.c', "isize i = ((from + dir * k) % l->n + l->n) % l->n;", "isize i = (from + dir * k) % l->n;"),
    ('ui/find.c', "u->find_miss = text.len > 0 && i < 0;", "u->find_miss = i < 0;"),
    ('ui/find.c', "u->list.sel = i >= 0 ? i : ui_find_back(u);", "u->list.sel = i >= 0 ? i : u->list.sel;"),
    ('ui/find.c', '    ui_unfocus(u, UI_FOCUS_FIND);\n    if (u->find.len) memcpy(u->last, u->find.b, (size_t)u->find.len), u->last_len = u->find.len;', '    ui_unfocus(u, UI_FOCUS_FIND);'),
    ('ui/ui.c', '    if (cp < 0x20 || cp == 0x7F || cp > 0x10FFFF) return 0;', '    if (cp < 0x20) return 0;'),
    ('ui/find.c', '    isize i = ui_find_at(&u->list, names, &u->find_m, u->list.sel + dir, dir);', '    isize i = ui_find_at(&u->list, names, &u->find_m, u->list.sel, dir);'),
    ('ui/handle.c', "        u->list.sel = ui_find_back(u), ui_show_sel(&u->list);\n        return true;", "        return true;"),
    ('ui/handle.c', "    default: ui_find_leave(u); return false;   // a command", "    default: ui_find_leave(u); return true;   // a command"),
    # ---- F2: input
    ('ui/handle.c', "return s.key == k ? (Sort){ k, !s.reverse } : (Sort){ k, false };", "return (Sort){ k, !s.reverse };"),
    ('ui/handle.c', "if (!prompt && in->header >= 0 && in->header < SORT_KEYS) {", "if (!prompt && in->header >= 0) {"),
    ('ui/handle.c', "if (!prompt && in->side_click >= 0 && u->side.top + in->side_click < u->side.n) {", "if (!prompt && in->side_click >= 0) {"),
    ('ui/handle.c', '        l->sel = row, ui_unfocus(u, UI_FOCUS_SIDE), ui_unfocus(u, UI_FOCUS_FIND);   // (the find bar', '        l->sel = row, ui_unfocus(u, UI_FOCUS_FIND);   // (the find bar'),
    ('ui/handle.c', '    if (!prompt && in->forwards > 0) return *out = (UiAction){ UA_FORWARD, 0, false }, true;\n', ''),
    ('ui/handle.c', 'UiList *m = u->focus == UI_FOCUS_SIDE ? &u->side : l;', 'UiList *m = l;'),
    ('ui/handle.c', '    case UK_SORT_NEXT: u->sort = (Sort){ (SortKey)((u->sort.key + 1) % SORT_KEYS), false };', '    case UK_SORT_NEXT: u->sort = (Sort){ (SortKey)((u->sort.key + 1) % SORT_KEYS), u->sort.reverse };'),
    ('ui/handle.c', '        u->focus = u->side.n > 0 ? UI_FOCUS_SIDE : UI_FOCUS_LIST;', '        u->focus = UI_FOCUS_SIDE;'),
    ('ui/handle.c', '        else ui_unfocus(u, UI_FOCUS_SIDE);\n', ''),
    ('ui/handle.c', '        if (u->focus == UI_FOCUS_SIDE && u->side.n > 0) return *out = (UiAction){ UA_PLACE, u->side.sel, false }, true;', '        if (u->focus == UI_FOCUS_SIDE) return *out = (UiAction){ UA_PLACE, u->side.sel, false }, true;'),
    ('ui/find.c', 'u->find_miss = false, u->find_select = false, UI_TEXT_CLEAR(u->find)', 'u->find_miss = false, UI_TEXT_CLEAR(u->find)'),
    # ---- F2: history
    ('ui/helpers.c', "if (*n == UI_HISTORY_MAX) memmove(stack, stack + 1, sizeof *stack * (UI_HISTORY_MAX - 1)), (*n)--;", "if (*n == UI_HISTORY_MAX) (*n)--;"),
    ('ui/helpers.c', "    h->nfwd = 0;\n", ""),
    ('ui/helpers.c', "    hist_put(other, no, cur, sel);\n    return true;", "    return true;"),
]
MUTANTS += [
    # ---- F2: procfs files read to their end
    ('fs/paths.c', '    } else if (S_ISREG(st.st_mode) && st.st_size == 0) {', '    } else if (0) {'),
    ('fs/paths.c', '        bool whole = avail > 1 && got == 0;', '        bool whole = true;'),
    ('fs/paths.c', '        arena_commit(a, whole ? n + 1 : 0);', '        arena_commit(a, n + 1);'),
]
MUTANTS += [
    ('ui/helpers.c', "    if (*no > 0) (*no)--;", "    (void)no;"),
]
# ---- F3
O, T, C, J, N = 'fs/ops.c', 'fs/trash.c', 'fs/clip.c', 'fs/job.c', 'fs/undo.c'
MUTANTS += [
    # the guard
    ('fs/guard.c', "    if (!ops_root[0]) return true;\n    char r[PATH_MAX];", "    return true;\n    char r[PATH_MAX];"),
    ('fs/guard.c', '        snprintf(ops_root, sizeof ops_root, "/nonexistent/ofx-guard");', "        ops_root[0] = 0;"),
    ('fs/guard.c', '        if (str_eq(str_cut(rest, i, e), S(".."))) return false;', ""),
    # names
    (O, "    if (str_find(name, '/') >= 0)", "    if (0)"),
    (O, 'if (str_eq(name, S(".")) || str_eq(name, S("..")))', 'if (str_eq(name, S(".")))'),
    (O, "    if (name.len > NAME_MAX)", "    if (name.len > NAME_MAX + 1)"),
    (O, "isize dot = dir ? -1 : str_find_last(name, '.');", "isize dot = str_find_last(name, '.');"),
    (O, "    if (dot <= 0) snprintf", "    if (dot < 0) snprintf"),
    (O, "    if (!fresh && !exists_at(dir, name))", "    if (!exists_at(dir, name))"),
    (O, "|| lstat(p, &st) == 0;   // too long", "|| stat(p, &st) == 0;   // too long"),
    (O, "for (int k = copy ? 1 : 2;", "for (int k = 2;"),
    (O, "    if (renameat2(AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE) == 0) return true;", "    if (renameat(AT_FDCWD, from, AT_FDCWD, to) == 0) return true;"),
    ('fs/ops.c', '        if (!ops_allowed(p, err)) return false;\n        int fd = -1;', '        int fd = -1;'),
    # delete for good
    # Inert, kept out: one pass instead of passes until nothing is left. Linux readdir on tmpfs,
    # ext4 and btrfs skips nothing after unlinks, so no test can tell; the passes are for file
    # systems that do (POSIX leaves it unspecified).
    (O, "    if (depth > TREE_DEPTH_MAX) return err_set(err, \"%s: folders nested too deep\", shown), false;", ""),
    (O, "    if (lstat(path, &st) != 0) return err_set(err, \"%s: %s\", path, strerror(errno)), false;\n    if (S_ISDIR(st.st_mode)) {\n        int fd", "    if (stat(path, &st) != 0) return err_set(err, \"%s: %s\", path, strerror(errno)), false;\n    if (S_ISDIR(st.st_mode)) {\n        int fd"),
    (O, "    if (!ops_allowed(path, err)) return false;\n    struct stat st;\n    if (lstat(path", "    struct stat st;\n    if (lstat(path"),
    (O, "    (*count)++;\n    return true;\n}", "    return true;\n}"),
    # the trash
    (T, "        *slash = 0;\n    }\n}", "        return false;\n    }\n}"),
    (T, '|| stat(up, &st) != 0 || st.st_dev != dev) return;', '|| stat(up, &st) != 0) return;'),
    (T, "dev_of(dir, &fd) && hd == fd) {", "dev_of(dir, &fd) && hd != fd) {"),
    (T, "S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX)) {", "S_ISDIR(st.st_mode)) {"),
    (T, "    if (lstat(admin, &st) == 0", "    if (stat(admin, &st) == 0"),
    (T, "    if (lstat(out, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != uid)", "    if (stat(out, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != uid)"),
    ('fs/trash.c', 'O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL, 0600);\n        if (fd < 0) {', 'O_WRONLY | O_CLOEXEC | O_CREAT | O_TRUNC, 0600);\n        if (fd < 0) {'),
    (T, "        if (lstat(t->file, &ex) == 0) continue;", ""),
    (T, "    unlink(t->info);\n    return true;", "    return true;"),
    (T, "    if (renameat2(AT_FDCWD, t->file, AT_FDCWD, t->from, RENAME_NOREPLACE) != 0) {", "    if (renameat(AT_FDCWD, t->file, AT_FDCWD, t->from) != 0) {"),
    (T, "    if (!ops_allowed(path, err)) return false;\n    if (!ops_resolve", "    if (!ops_resolve"),
    (T, "    if (!ops_allowed(files, err) || !mkdir_private", "    if (!mkdir_private"),
    # the clipboard payloads
    (C, 'PUT("%s", c->cut ? "cut" : "copy");', 'PUT("%s", "copy");'),
    ('fs/clip.c', 'else PUT("%s\\r\\n", uri);', 'else PUT("%s\\n", uri);'),
    (C, "                c->cut = true;\n", ""),
    (C, "            if (str_eq(line, S(\"copy\"))) continue;\n            return false;", "            continue;"),
    (C, "    if (!ok) n = 0;", ""),
    (C, "        if (i && n + 1 < avail) out[n++] = '\\n';\n", ''),
    (C, '        isize k = quoted ? shell_quote(', '        isize k = false ? shell_quote('),
    # jobs
    ('fs/job.c', '                fchmod(dfd, st->st_mode & 07777);\n                futimens(dfd, (struct timespec[2]){ st->st_atim, st->st_mtim });', '                futimens(dfd, (struct timespec[2]){ st->st_atim, st->st_mtim });'),
    ('fs/job.c', '                fchmod(dfd, st->st_mode & 07777);\n                futimens(dfd, (struct timespec[2]){ st->st_atim, st->st_mtim });', '                fchmod(dfd, st->st_mode & 07777);'),
    (J, "    if (fresh) snprintf(part, sizeof part, \"%s\", dname);\n    else part_name(part, sizeof part, dname);\n    bool ok = true;", "    snprintf(part, sizeof part, \"%s\", dname);\n    bool ok = true;"),
    (J, '        if (!ok && dfd >= 0) unlinkat(ddir, part, 0);', ""),
    (J, "int r = replace ? renameat(ddir, part, ddir, dname) : renameat2(ddir, part, ddir, dname, RENAME_NOREPLACE);", "int r = renameat2(ddir, part, ddir, dname, RENAME_NOREPLACE);"),
    ('fs/job.c', '        *dname = keep;\n', ''),
    ('fs/job.c', '    if (a != ANS_SKIP && dir != S_ISDIR(dt->st_mode))', '    if (0)'),
    ('fs/job.c', '    else return dir ? (*merge = true) : (*replace = true), 1;', '    else return (*replace = true), 1;'),
    ('fs/job.c', '    if (ok) {\n        fchmod(dfd, st->st_mode & 07777);\n        futimens(dfd, (struct timespec[2]){ st->st_atim, st->st_mtim });\n    }\n    if (dfd >= 0) close(dfd);', '    if (ok) {\n    }\n    if (dfd >= 0) close(dfd);'),
    ('fs/job.c', '    if (ok && !fresh && renameat2(ddir, part, ddir, dname, RENAME_NOREPLACE) != 0) {', '    if (0) {'),
    ('fs/job.c', '    if (!ok && !fresh) part_remove(ddir, part);', ''),
    (J, "        if (S_ISREG(st.st_mode)) atomic_fetch_add(&j->bytes_total, (long long)st.st_size);", "        atomic_fetch_add(&j->bytes_total, (long long)st.st_size);"),
    (J, "if (strcmp(de->d_name, \".\") && strcmp(de->d_name, \"..\")) job_count(j, fd, de->d_name, depth + 1);", "(void)de;"),
    (J, "        atomic_fetch_add(&j->bytes_done, (long long)k);\n", ""),
    (J, "    else if (all) *rest = a;", ""),
    (J, "    if (*rest) return *rest;\n", ""),
    (J, "    if (!j->ask) return ANS_SKIP;", "    if (!j->ask) return ANS_REPLACE;"),
    (J, "    if (a == ANS_CANCEL) atomic_store(&j->cancel, true);", "    if (a == ANS_CANCEL) {}"),
    ('fs/job.c', '    if (renameat2(sdir, sname, ddir, dname, RENAME_NOREPLACE) == 0) {\n        if (depth == 0) atomic_fetch_add', '    if (renameat(sdir, sname, ddir, dname) == 0) {\n        if (depth == 0) atomic_fetch_add'),
    (J, "    if (ok) unlinkat(sdir, sname, AT_REMOVEDIR);", ""),
    (J, "    if (j->kind == JOB_MOVE && it->out == OUT_NEW && !whole) it->out = OUT_MERGED;", ""),
    (J, "    if (whole && j->kind == JOB_MOVE && !same_drive) {", "    if (j->kind == JOB_MOVE && !same_drive) {"),
    (J, "            atomic_fetch_add(&j->files_done, n);\n", ""),
    (J, "if (!ops_allowed(it->dst, &err) || (j->kind == JOB_MOVE && !ops_allowed(it->src, &err))) {", "if (!ops_allowed(it->dst, &err)) {"),
    (J, "    atomic_store(&j->phase, JP_DONE);\n", ""),
    (J, "    bool scan = j->kind != JOB_MOVE;", "    bool scan = true;"),
    (J, "    if (ok) atomic_fetch_add(&j->files_done, 1);\n    job_tick(j);\n    return ok;", "    job_tick(j);\n    return ok;"),
    (J, "    snprintf(it->final, sizeof it->final, \"%s\", j->top_name);", ""),
    # undo
    (N, '        if (!present(it->to)) return err_set(err, "%s is gone: can\'t undo", it->to), false;', ""),
    (N, '            if (lstat(it->to, &st) != 0 || !S_ISDIR(st.st_mode)) return', '            if (0) return'),
    (N, "        if ((u->kind == UNDO_RENAME || u->kind == UNDO_TRASH || u->kind == UNDO_MOVE) && present(it->from))", "        if (0)"),
    (N, "    undo_clear(u);\n    return ok;", "    return ok;"),
    (N, 'dev_of(from_dir, &a) && dev_of(u->it[i].to, &b) && a != b) return true;', 'dev_of(from_dir, &a) && dev_of(u->it[i].to, &b) && a == b) return true;'),
    (N, "    if (u->n == 1 || u->kind == UNDO_RENAME || u->kind == UNDO_MKDIR || u->kind == UNDO_MKFILE || u->kind == UNDO_CHMOD)", "    if (u->kind == UNDO_RENAME || u->kind == UNDO_MKDIR || u->kind == UNDO_MKFILE || u->kind == UNDO_CHMOD)"),
    (N, "        if (one) (*done)++;", "        if (one) {}"),
    (N, "            one = trash_put(it->to, &t, &e);   // copies go to the trash, never for good", "            isize k = 0;\n            one = delete_tree(it->to, &k, &e), (void)t;"),
    # ui: the selection, the edit line, the prompts
    ('ui/edit.c', "    u->marked += on ? 1 : -1;", "    u->marked++;"),
    ('ui/edit.c', "i >= u->list.n || (bool)u->mark[i] == on) return;", "i >= u->list.n) return;"),
    ('ui/edit.c', "    if (from > to) {\n        isize t = from;", "    if (0) {\n        isize t = from;"),
    ('ui/handle.c', "ui_mark(u, l->sel, !ui_marked(u, l->sel)), ui_move(l, 1);", "ui_mark(u, l->sel, !ui_marked(u, l->sel));"),
    ('ui/handle.c', '        if (u->marked) ui_mark_clear(u);\n        else ui_unfocus(u, UI_FOCUS_SIDE);', '        ui_unfocus(u, UI_FOCUS_SIDE);'),
    ('ui/handle.c', '        if (shift && u->shift_add) ui_mark_range(u, l->sel, row);', '        if (shift && u->shift_add) ui_mark(u, row, true);'),
    ('ui/handle.c', '        if (shift && u->shift_add) ui_mark_range(u, l->sel, row);', '        if (shift) ui_mark_range(u, l->sel, row);'),
    ('ui/handle.c', '        else if (shift) ui_range(u, l->sel, row, ctrl);', '        else if (shift) ui_range(u, l->sel, row, false);'),
    ('ui/handle.c', '        else if (shift) ui_range(u, l->sel, row, ctrl);', '        else if (shift) ui_range(u, l->sel, row, true);'),
    ('ui/handle.c', '            u->anchor = -1;   // (the next Shift starts at row)\n', ''),
    ('ui/handle.c', 'bool shift = in->click_mods & UI_MOD_SHIFT, ctrl = in->click_mods & UI_MOD_CTRL;', 'bool shift = in->click_mods & UI_MOD_SHIFT, ctrl = false;'),
    ('ui/handle.c', '            if (ctrl) ui_mark(u, row, !ui_marked(u, row));', '            if (ctrl) ui_mark(u, row, true);'),
    ('ui/handle.c', "    if (!prompt && in->click >= 0 && l->top + in->click < l->n) {", "    if (in->click >= 0 && l->top + in->click < l->n) {"),
    ('ui/prompts.c', '    bool yes = e.key == UK_ENTER || e.key == UK_SHIFT_ENTER || (e.key == UK_NONE', '    bool yes = e.key == UK_SHIFT_ENTER || (e.key == UK_NONE'),
    ('ui/prompts.c', "(e.key == UK_NONE && (e.ch == 'n' || e.ch == 'N'));", "(e.key == UK_NONE && e.ch == 'N');"),
    ('ui/handle.c', '        if (files) u->confirming = true, u->confirm_n = u->marked ? u->marked : 1;', '        if (files) u->confirming = true, u->confirm_n = 1;'),
    ('ui/prompts.c', "return (UiAction){ UA_ANSWER, a, (e.ch >= 'A' && e.ch <= 'Z') || u->ask_all };", "return (UiAction){ UA_ANSWER, a, u->ask_all };"),
    ('ui/prompts.c', "    if (e.key == UK_ESC) return u->asking = false, (UiAction){ UA_ANSWER, UI_ANS_CANCEL, false };", "    if (e.key == UK_ESC) return u->asking = false, (UiAction){ UA_ANSWER, UI_ANS_SKIP, false };"),
    ('ui/edit.c', '    if (*l.len - (hi - lo) + n > l.max) return false;', '    if (*l.len - (hi - lo) + n > l.max + 1) return false;'),
    ('ui/edit.c', '    while (at > 0 && ((unsigned char)l.b[at] & 0xC0) == 0x80) at--;', ''),
    ('ui/edit.c', '    while (at < *l.len && ((unsigned char)l.b[at] & 0xC0) == 0x80) at++;', ''),
    ('ui/edit.c', '    case UK_DELETE: ui_line_cut(l, lo < hi ? lo : *l.caret, lo < hi ? hi : ui_line_fwd(l, *l.caret)); return true;', ''),
    ('ui/edit.c', '    case UK_HOME: *l.caret = *l.anchor = 0; return true;', ''),
    ('ui/edit.c', "    case UK_SHIFT_ENTER: return (UiAction){ u->edit_path ? UA_GO : UA_RENAME, 0, false };", "    case UK_SHIFT_ENTER: ui_edit_close(u); return (UiAction){ u->edit_path ? UA_GO : UA_RENAME, 0, false };"),
    ('ui/handle.c', 'if (files) ui_edit_rename', 'if (l->n > 0) ui_edit_rename'),
    ('ui/handle.c', '        else if (files) return *out = ui_view_act(u, (UiAction){ UA_TRASH, 0, false }), true;', '        else return *out = ui_view_act(u, (UiAction){ UA_TRASH, 0, false }), true;'),
    ('ui/handle.c', "        isize row = l->top + in->click;\n        ui_edit_close(u);", "        isize row = l->top + in->click;"),
    ('ui/ui.c', '    if (cp < 0x20 || cp == 0x7F || cp > 0x10FFFF) return 0;', '    if (cp < 0x20 || cp > 0x10FFFF) return 0;'),
]
# ---- F4
MUTANTS += [
    # path buttons
    ('ui/helpers.c', "    if (str_eq(dir, S(\"/\"))) return p.len > 0 && p.p[0] == '/';", ""),
    ('ui/helpers.c', "return str_starts_with(p, dir) && (p.len == dir.len || p.p[dir.len] == '/');", "return str_starts_with(p, dir);"),
    ('ui/helpers.c', "if (!c->deep[0] || !ui_under(str_from_c(c->deep), cw)) snprintf", "if (!c->deep[0]) snprintf"),
    ('ui/helpers.c', "    while (h.len > 1 && h.p[h.len - 1] == '/') h.len--;", ""),
    ('ui/helpers.c', "if (h.len > 1 && h.p[0] == '/' && ui_under(cw, h)) c->seg", "if (h.len > 1 && h.p[0] == '/' && ui_under(str_from_c(c->deep), h)) c->seg"),
    ('ui/helpers.c', "    if (!c->seg[i].home && c->seg[i].end == 1) return S(\"/\");", ""),
    ('ui/helpers.c', "    while (hi < n && (hi == lo || used + w[hi] <= room)) used += w[hi++];", "    while (hi < n && used + w[hi] <= room) used += w[hi++];"),
    ('ui/helpers.c', "    if (all <= room) return *lo = 0, *hi = c->n, false;", "    if (all < room) return *lo = 0, *hi = c->n, false;"),
    ('ui/helpers.c', "    room -= 2 * arrow;\n", ""),
    ('ui/helpers.c', "        while (*lo < c->cur && ui_crumbs_fit(w, c->n, room, *lo) <= c->cur) (*lo)++;", ""),
    ('ui/helpers.c', "    c->lo = to < 0 ? 0 : to > c->n - 1 ? c->n - 1 : to;", "    c->lo = to;"),
    ('ui/helpers.c', "    c->lo = -1;\n    Str d = str_from_c(c->deep);", "    Str d = str_from_c(c->deep);"),
    # the location line
    ('ui/helpers.c', "    if (text.p[0] == '~' && (text.len == 1 || text.p[1] == '/')) {", "    if (text.p[0] == '~') {"),
    ('ui/helpers.c', "        while (text.len && text.p[0] == '/') text = str_cut(text, 1, text.len);", ""),
    ('ui/helpers.c', "    text = str_trim(text);\n    if (text.len == 0) return false;", "    if (text.len == 0) return false;"),
    ('ui/helpers.c', "        if (!home || home[0] != '/') return false;", ""),
    ('ui/edit.c', ', u->edit.anchor = 0;\n    ui_find_leave(u);', ', u->edit.anchor = (int)n;\n    ui_find_leave(u);'),
    ('ui/edit.c', "(e.ch != '/' || l.slash)", "(e.ch != '/')"),
    ('ui/edit.c', "    return u->edit_path ? PATH_MAX - 1 : NAME_MAX;", "    return NAME_MAX;"),
    ('ui/edit.c', "    case UK_SHIFT_ENTER: return (UiAction){ u->edit_path ? UA_GO : UA_RENAME, 0, false };", "    case UK_SHIFT_ENTER: return (UiAction){ UA_RENAME, 0, false };"),
    ('ui/edit.c', '        if (u->edit_path) ui_edit_close(u);\n        return UI_NONE;', '        ui_edit_close(u);\n        return UI_NONE;'),
    # bar clicks
    ('ui/handle.c', "    if (!prompt && in->bar != UB_NONE) {", "    if (in->bar != UB_NONE) {"),
    ('ui/handle.c', '            if (u->focus == UI_FOCUS_EDIT && u->edit_path) return ui_edit_close(u), *out = UI_NONE, true;\n', ''),
    ('ui/handle.c', '        case UB_UP: ui_edit_close(u); return *out = (UiAction){ UA_PARENT, 0, false }, true;', '        case UB_UP: return *out = (UiAction){ UA_PARENT, 0, false }, true;'),
    ('ui/handle.c', '    { UK_PARENT, UA_PARENT, 0, false, false },', '    { UK_NONE, UA_PARENT, 0, false, false },'),
    # counts
    (L, "        if (nm[0] == '.' && !hidden) continue;\n        n++;", "        n++;"),
    (L, "    if (!d) return -1;\n    isize n = 0;", "    if (!d) return 0;\n    isize n = 0;"),
]
# ---- F5: tabs
MUTANTS += [
    ('ui/helpers.c', "    if (t->n == UI_TABS_MAX) return false;\n    int at", "    int at"),
    ('ui/helpers.c', "    int at = t->n == 0 ? 0 : t->cur + 1;", "    int at = t->n;"),
    ('ui/helpers.c', "    if (show) t->cur = at;   // the first", "    if (0) t->cur = at;   // the first"),
    ('ui/helpers.c', "    if (at < 0 || at >= t->n) return -1;\n    int slot", "    int slot"),
    ('ui/helpers.c', "    if (at < t->cur || (at == t->cur && t->cur == t->n)) t->cur--;", "    if (at < t->cur) t->cur--;"),
    ('ui/helpers.c', "    if (at < t->cur || (at == t->cur && t->cur == t->n)) t->cur--;", "    if (at == t->cur && t->cur == t->n) t->cur--;"),
    ('ui/helpers.c', "    if (t->cur < 0) t->cur = 0;\n    return slot;", "    return slot;"),
    ('ui/helpers.c', "    if (t->n > 0) t->cur = ((t->cur + by) % t->n + t->n) % t->n;", "    if (t->n > 0) t->cur = (t->cur + by) % t->n;"),
    ('ui/helpers.c', "    if (k == 9 && t->n > 0) t->cur = t->n - 1;", "    if (k == 9 && t->n > 0) {}"),
    ('ui/helpers.c', "    else if (k >= 1 && k <= t->n) t->cur = k - 1;", "    else if (k >= 1) t->cur = k - 1;"),
    ('ui/helpers.c', "    if (to < 0 || to >= t->n) return;\n    int s = t->slot[to];", "    if (to < 0) return;\n    int s = t->slot[to];"),
    ('ui/helpers.c', "    t->slot[to] = t->slot[t->cur], t->slot[t->cur] = s, t->cur = to;", "    t->slot[to] = t->slot[t->cur], t->slot[t->cur] = s;"),
    ('ui/handle.c', "    if (!prompt && in->tab_close >= 0) return", "    if (in->tab_close >= 0) return"),
    ('ui/handle.c', '    if (!prompt && in->mid_row >= 0 && l->top + in->mid_row < l->n) return *out = (UiAction){ UA_TAB_BG_ROW, l->top + in->mid_row, false }, true;', '    if (!prompt && in->mid_row >= 0) return *out = (UiAction){ UA_TAB_BG_ROW, in->mid_row, false }, true;'),
    ('ui/handle.c', "    if (!prompt && in->mid_side >= 0 && u->side.top + in->mid_side < u->side.n && !ui_side_head", "    if (!prompt && in->mid_side >= 0 && !ui_side_head"),
    ('ui/handle.c', '{ UK_TAB_CLOSE, UA_TAB_CLOSE, -1, false, false },', '{ UK_TAB_CLOSE, UA_TAB_CLOSE, 0, false, false },'),
    ('ui/handle.c', '    case UK_TAB_JUMP: return *out = (UiAction){ UA_TAB_JUMP, (isize)ch, false }, true;', ''),
    ('ui/handle.c', '    if (!prompt && in->tab_click >= 0) return *out = (ui_edit_close(u), (UiAction){ UA_TAB_SHOW, in->tab_click, false }), true;', '    if (!prompt && in->tab_click >= 0) return *out = (UiAction){ UA_TAB_SHOW, in->tab_click, false }, true;'),
]
# ---- F5.1: the modal panels
MUTANTS += [
    ('ui/prompts.c', ' || on == UI_ON_DELETE || on == UI_ON_EMPTY', ' || on == UI_ON_EMPTY'),   # (F16 step 7d: the case list there is ui_is_question now)
    ('ui/prompts.c', 'u->confirming = false; return (UiAction){ yes ? UA_DELETE : UA_NONE, 0, false };', 'u->confirming = false; return (UiAction){ UA_DELETE, 0, false };'),
    ('ui/handle.c', '    if (on == UI_ON_CLASH && in->modal == UI_MODAL_ALL) u->ask_all = !u->ask_all;\n', ''),
    ('ui/handle.c', '        return *out = (UiAction){ UA_ANSWER, a, a != UI_ANS_CANCEL && u->ask_all }, true;', '        return *out = (UiAction){ UA_ANSWER, a, false }, true;'),
    ('ui/prompts.c', "    return (UiAction){ UA_ANSWER, a, (e.ch >= 'A' && e.ch <= 'Z') || u->ask_all };", "    return (UiAction){ UA_ANSWER, a, e.ch >= 'A' && e.ch <= 'Z' };"),
]
# ---- F5.2: the edit lines' selection and text clipboard, the path buttons by keyboard
MUTANTS += [
    ('ui/edit.c', '    *lo = *l.anchor < *l.caret ? *l.anchor : *l.caret;', '    *lo = *l.anchor;'),
    ('ui/edit.c', '    *hi = *l.anchor < *l.caret ? *l.caret : *l.anchor;', '    *hi = *l.caret;'),
    ('ui/edit.c', '    *l.caret = *l.anchor = from;', '    *l.caret = from;'),
    ('ui/edit.c', '    ui_line_cut(l, lo, hi);\n    memmove', '    memmove'),
    ('ui/edit.c', '    *l.len += n, *l.caret += n, *l.anchor = *l.caret;', '    *l.len += n, *l.caret += n;'),
    ('ui/edit.c', '    if (lo == hi) return false;\n    memcpy(u->copy', '    memcpy(u->copy'),
    ('ui/edit.c', '    u->copy_len = hi - lo, u->copied = true;', '    u->copy_len = hi - lo;'),
    ('ui/edit.c', "text.p[i] != '\\n' && text.p[i] != '\\r';)", "text.p[i] != '\\n';)"),
    ('ui/edit.c', "text.p[i] != '\\n' && text.p[i] != '\\r';)", "text.p[i] != '\\r';)"),
    ('ui/edit.c', "        bool drop = c < 0x20 || c == 0x7F || (c == '/' && !l.slash);", "        bool drop = c == 0x7F || (c == '/' && !l.slash);"),
    ('ui/edit.c', "        bool drop = c < 0x20 || c == 0x7F || (c == '/' && !l.slash);", "        bool drop = c < 0x20 || (c == '/' && !l.slash);"),
    ('ui/edit.c', "        bool drop = c < 0x20 || c == 0x7F || (c == '/' && !l.slash);", "        bool drop = c < 0x20 || c == 0x7F || c == '/';"),
    ('ui/edit.c', '        if (n + (e - i) > room) break;', '        if (n + (e - i) > room + 1) break;'),
    ('ui/edit.c', '        while (e < text.len && ((unsigned char)text.p[e] & 0xC0) == 0x80) e++;\n', ''),
    ('ui/edit.c', '    if (n > 0) ui_line_put(l, b, n);', '    ui_line_put(l, b, n);'),
    ('ui/edit.c', '    case UK_LEFT: *l.caret = *l.anchor = lo < hi ? lo : ui_line_back(l, *l.caret); return true;', '    case UK_LEFT: *l.caret = *l.anchor = ui_line_back(l, *l.caret); return true;'),
    ('ui/edit.c', '    case UK_RIGHT: *l.caret = *l.anchor = lo < hi ? hi : ui_line_fwd(l, *l.caret); return true;', '    case UK_RIGHT: *l.caret = *l.anchor = ui_line_fwd(l, *l.caret); return true;'),
    ('ui/edit.c', '    case UK_RIGHT: *l.caret = *l.anchor = lo < hi ? hi : ui_line_fwd(l, *l.caret); return true;', '    case UK_RIGHT: *l.caret = lo < hi ? hi : ui_line_fwd(l, *l.caret); return true;'),
    ('ui/edit.c', '    case UK_SEL_LEFT: *l.caret = ui_line_back(l, *l.caret); return true;\n', ''),
    ('ui/edit.c', '    case UK_SEL_RIGHT: *l.caret = ui_line_fwd(l, *l.caret); return true;\n', ''),
    ('ui/edit.c', '    case UK_SEL_HOME: *l.caret = 0; return true;\n', ''),
    ('ui/edit.c', '    case UK_SEL_END: *l.caret = *l.len; return true;\n', ''),
    ('ui/edit.c', '    case UK_ALL: *l.anchor = 0, *l.caret = *l.len; return true;\n', ''),
    ('ui/edit.c', '    case UK_BACKSPACE: ui_line_cut(l, lo < hi ? lo : ui_line_back(l, *l.caret), lo < hi ? hi : *l.caret); return true;', '    case UK_BACKSPACE: ui_line_cut(l, ui_line_back(l, *l.caret), *l.caret); return true;'),
    ('ui/edit.c', '        if (!l.secret && ui_line_copy(u, l)) ui_line_cut(l, lo, hi);', '        if (!l.secret) ui_line_copy(u, l);'),   # (retargeted, step 8 A)
    ('ui/edit.c', '    case UK_PASTE: ui_line_paste(l, paste); return true;\n', ''),
    # the path buttons
    ('ui/prompts.c', '    if (in->crumb_n <= 0) return;\n    u->focus = UI_FOCUS_CRUMBS', '    u->focus = UI_FOCUS_CRUMBS'),
    ('ui/prompts.c', "    u->crumb_at = in->crumb_last < 0 ? 0 : in->crumb_last >= in->crumb_n ? in->crumb_n - 1 : in->crumb_last;", "    u->crumb_at = in->crumb_last >= in->crumb_n ? in->crumb_n - 1 : in->crumb_last;"),
    ('ui/prompts.c', "    u->crumb_at = in->crumb_last < 0 ? 0 : in->crumb_last >= in->crumb_n ? in->crumb_n - 1 : in->crumb_last;", "    u->crumb_at = in->crumb_last < 0 ? 0 : in->crumb_last;"),
    ('ui/prompts.c', "if (u->crumb_at > 0) u->crumb_at--;", "u->crumb_at--;"),
    ('ui/prompts.c', 'if (u->crumb_at < in->crumb_n - 1 + u->branch_on) u->crumb_at++;', 'u->crumb_at++;'),
    ('ui/prompts.c', '        u->focus = UI_FOCUS_LIST, *a = (UiAction){ UA_CRUMB, u->crumb_at, false };', '        *a = (UiAction){ UA_CRUMB, u->crumb_at, false };'),
    ('ui/prompts.c', '*a = (UiAction){ UA_TAB_BG_CRUMB, u->crumb_at, false }; return true;', '*a = (UiAction){ UA_CRUMB, u->crumb_at, false }; return true;'),
    ('ui/prompts.c', '    if (e.cmd == UK_CRUMBS) return true;\n', ''),
    ('ui/prompts.c', '    case UK_SHIFT_ENTER: if (u->crumb_at < in->crumb_n) *a = (UiAction){ UA_TAB_BG_CRUMB, u->crumb_at, false }; return true;\n    case UK_ESC: case UK_TAB: u->focus = UI_FOCUS_LIST; return true;\n', '    case UK_SHIFT_ENTER: if (u->crumb_at < in->crumb_n) *a = (UiAction){ UA_TAB_BG_CRUMB, u->crumb_at, false }; return true;\n'),
    ('ui/prompts.c', '    case UK_SHIFT_ENTER: if (u->crumb_at < in->crumb_n) *a = (UiAction){ UA_TAB_BG_CRUMB, u->crumb_at, false }; return true;\n    case UK_ESC: case UK_TAB: u->focus = UI_FOCUS_LIST; return true;\n    default: u->focus = UI_FOCUS_LIST; return false;', '    case UK_SHIFT_ENTER: if (u->crumb_at < in->crumb_n) *a = (UiAction){ UA_TAB_BG_CRUMB, u->crumb_at, false }; return true;\n    case UK_ESC: case UK_TAB: u->focus = UI_FOCUS_LIST; return true;\n    default: return false;'),
    ('ui/prompts.c', '    case UK_SHIFT_ENTER: if (u->crumb_at < in->crumb_n) *a = (UiAction){ UA_TAB_BG_CRUMB, u->crumb_at, false }; return true;\n    case UK_ESC: case UK_TAB: u->focus = UI_FOCUS_LIST; return true;\n    default: u->focus = UI_FOCUS_LIST; return false;', '    case UK_SHIFT_ENTER: if (u->crumb_at < in->crumb_n) *a = (UiAction){ UA_TAB_BG_CRUMB, u->crumb_at, false }; return true;\n    case UK_ESC: case UK_TAB: u->focus = UI_FOCUS_LIST; return true;\n    default: u->focus = UI_FOCUS_LIST; return true;'),
    ('ui/handle.c', '    if (clicked || in->crumb_n <= 0) ui_unfocus(u, UI_FOCUS_CRUMBS);', '    if (in->crumb_n <= 0) ui_unfocus(u, UI_FOCUS_CRUMBS);'),
    ('ui/handle.c', '    if (clicked || in->crumb_n <= 0) ui_unfocus(u, UI_FOCUS_CRUMBS);', '    if (clicked) ui_unfocus(u, UI_FOCUS_CRUMBS);'),
    ('ui/handle.c', "in->tab_click >= 0 || in->tab_close >= 0 || in->mid_row >= 0 || in->mid_side >= 0 || in->mid_crumb >= 0;", "in->tab_click >= 0 || in->tab_close >= 0 || in->mid_row >= 0 || in->mid_side >= 0;"),
    ('ui/handle.c', "    bool clicked = in->click >= 0 || in->dclick >= 0 ||", "    bool clicked = in->dclick >= 0 ||"),
    ('ui/handle.c', '    if (u->focus == UI_FOCUS_CRUMBS && u->crumb_at > in->crumb_n - 1 + u->branch_on) u->crumb_at = in->crumb_n - 1 + u->branch_on;   // (crumb_n: the branch button, F12 step 5c)\n', ''),
    ('ui/handle.c', '        if (u->focus == UI_FOCUS_EDIT && u->edit_path && e.cmd == UK_CRUMBS) {', '        if (u->focus == UI_FOCUS_EDIT && e.cmd == UK_CRUMBS) {'),
    ('ui/handle.c', '        if (on == UI_ON_CLASH) {\n            UiAction a = ui_ask_event(u, e);', '        if (0) {\n            UiAction a = ui_ask_event(u, e);'),
    ('ui/handle.c', '        else ui_crumbs_focus(u, in);\n        break;', '        else {}\n        break;'),
    ('ui/helpers.c', "    if (at < lo) c->lo = at;\n", ""),
    ('ui/helpers.c', "    while (at >= hi && lo < at) hi = ui_crumbs_fit(w, c->n, room - 2 * arrow, ++lo), c->lo = lo;", "    while (at >= hi && lo < at) hi = ui_crumbs_fit(w, c->n, room, ++lo), c->lo = lo;"),
    ('ui/helpers.c', "    while (at >= hi && lo < at) hi = ui_crumbs_fit(w, c->n, room - 2 * arrow, ++lo), c->lo = lo;", "    while (at > hi && lo < at) hi = ui_crumbs_fit(w, c->n, room - 2 * arrow, ++lo), c->lo = lo;"),
]
# ---- F5.3: the job queue, the throughput line, the job rows' keys and panels
Q = 'fs/jobq.c'
MUTANTS += [
    (Q, "    while (a.len > 1 && a.p[a.len - 1] == '/') a.len--;\n", ""),
    (Q, "    return l.len == s.len || str_eq(s, S(\"/\")) || l.p[s.len] == '/';", "    return l.len == s.len || l.p[s.len] == '/';"),
    (Q, "    return l.len == s.len || str_eq(s, S(\"/\")) || l.p[s.len] == '/';", "    return true;"),
    (Q, "    if (!str_starts_with(l, s)) return false;", ""),
    (Q, "    if (nrun >= JOBQ_WORKERS) return -1;", ""),
    (Q, "    if (nrun >= JOBQ_WORKERS) return -1;", "    if (nrun > JOBQ_WORKERS) return -1;"),
    (Q, "        for (int e = 0; free && e < w; e++) free = !jobq_overlap(wait[w], wait[e]);\n", ""),
    (Q, "        for (int r = 0; free && r < nrun; r++) free = !jobq_overlap(wait[w], run[r]);\n", ""),
    (Q, "    if (l->ns == 0 || ms - l->s[last].ms >= 50) {", "    if (1) {"),
    (Q, "        if (ms - l->s[i].ms > JOBQ_SPEED_MS) break;", "        if (ms - l->s[i].ms > JOBQ_SPEED_MS * 2) break;"),
    (Q, "    if (l->ns > 0 && done < l->s[last].bytes) done = l->s[last].bytes;   // never back\n", ""),
    (Q, "    if (dt >= JOBQ_SPAN_MS) l->now", "    if (dt > 0) l->now"),
    (Q, "    if (l->now > l->max) l->max = l->now;\n", ""),
    (Q, "        for (int k = 0; k < l->reached; k++) l->speed[k] = l->now;", ""),
    (Q, "    if (!l->known && l->now > 0) {", "    if (l->now > 0) {"),
    (Q, "    if (to > JOBQ_POINTS) to = JOBQ_POINTS;\n    while", "    while"),
    (Q, "    while (v >= 1000 && u < 3) v /= 1000, u++;", "    while (v >= 1000) v /= 1000, u++;"),
    (Q, "    if (bps <= 0) snprintf(buf, cap, \"%s\", \"\");", "    if (0) snprintf(buf, cap, \"%s\", \"\");"),
    (Q, "    else if (u == 0 || v >= 100) snprintf", "    else if (u == 0) snprintf"),
    # ui: the rows' keys
    ('ui/prompts.c', "    case UK_UP: if (u->job_at > 0) u->job_at--; return true;", "    case UK_UP: u->job_at--; return true;"),
    ('ui/prompts.c', "    case UK_DOWN: if (u->job_at < in->job_n - 1) u->job_at++; return true;", "    case UK_DOWN: u->job_at++; return true;"),
    ('ui/prompts.c', "    case UK_ENTER: u->cancel_asking = true, u->cancel_id = in->job_id[u->job_at]; return true;", "    case UK_ENTER: u->cancel_asking = true, u->cancel_id = in->job_id[0]; return true;"),
    ('ui/prompts.c', '    if (e.cmd == UK_JOBS) return u->focus = UI_FOCUS_LIST, true;', '    if (e.cmd == UK_JOBS) return true;'),
    ('ui/prompts.c', '    case UK_ENTER: u->cancel_asking = true, u->cancel_id = in->job_id[u->job_at]; return true;\n    case UK_ESC: case UK_TAB: u->focus = UI_FOCUS_LIST; return true;\n', '    case UK_ENTER: u->cancel_asking = true, u->cancel_id = in->job_id[u->job_at]; return true;\n'),
    ('ui/prompts.c', '    case UK_ENTER: u->cancel_asking = true, u->cancel_id = in->job_id[u->job_at]; return true;\n    case UK_ESC: case UK_TAB: u->focus = UI_FOCUS_LIST; return true;\n    default: u->focus = UI_FOCUS_LIST; return false;', '    case UK_ENTER: u->cancel_asking = true, u->cancel_id = in->job_id[u->job_at]; return true;\n    case UK_ESC: case UK_TAB: u->focus = UI_FOCUS_LIST; return true;\n    default: return false;'),
    ('ui/handle.c', '        else if (in->job_n > 0) u->focus = UI_FOCUS_JOBS, u->job_at = 0;', '        else u->focus = UI_FOCUS_JOBS, u->job_at = 0;'),
    ('ui/handle.c', '        else if (in->job_n > 0) u->focus = UI_FOCUS_JOBS, u->job_at = 0;', '        else if (in->job_n > 0) u->focus = UI_FOCUS_JOBS;'),
    ('ui/handle.c', "    if (u->job_at >= in->job_n) u->job_at = in->job_n - 1;\n", ""),
    ('ui/handle.c', '    if (in->job_n <= 0) ui_unfocus(u, UI_FOCUS_JOBS);\n', ''),
    ('ui/handle.c', '    if (clicked) ui_unfocus(u, UI_FOCUS_JOBS);\n', ''),
    ('ui/handle.c', "    if (!prompt && in->job_x >= 0 && in->job_x < in->job_n) {", "    if (in->job_x >= 0 && in->job_x < in->job_n) {"),
    ('ui/handle.c', "    if (!prompt && in->job_x >= 0 && in->job_x < in->job_n) {", "    if (!prompt && in->job_x >= 0) {"),
    ('ui/handle.c', "        u->cancel_asking = true, u->cancel_id = in->job_id[in->job_x];\n        return", "        u->cancel_asking = true, u->cancel_id = in->job_id[0];\n        return"),
    # the panels
    ('ui/handle.c', 'if (in->modal == UI_MODAL_OK || in->nev > 0) return u->full_told', 'if (in->modal == UI_MODAL_OK) return u->full_told'),
    ('ui/handle.c', 'if (in->modal == UI_MODAL_OK || in->nev > 0) return u->full_told', 'if (in->nev > 0) return u->full_told'),
    ('ui/prompts.c', 'return (UiAction){ yes ? UA_QUIT : UA_NONE, 0, false };', 'return (UiAction){ UA_QUIT, 0, false };'),
    ('ui/prompts.c', 'yes ? UA_CANCEL_JOB : UA_NONE', 'UA_CANCEL_JOB'),
    # ui_on_top (2026-10-28): each question or panel left out of the order
    (U, '    if (u->full_told) return UI_ON_FULL;\n', ''),
    (U, '    if (u->quit_asking) return UI_ON_QUIT;\n', ''),
    (U, '    if (u->cancel_asking) return UI_ON_CANCEL;\n', ''),
    (U, '    if (u->confirming) return UI_ON_DELETE;\n', ''),
    (U, '    if (u->empty_asking) return UI_ON_EMPTY;\n', ''),
    (U, '    if (u->asking) return UI_ON_CLASH;\n', ''),
    ('ui/ui.c', '[UI_PANEL_PROPS] = UI_ON_PROPS', '[UI_PANEL_PROPS] = UI_ON_NONE'),
    ('ui/ui.c', '[UI_PANEL_OW] = UI_ON_OW', '[UI_PANEL_OW] = UI_ON_NONE'),
    ('ui/ui.c', '[UI_PANEL_BM] = UI_ON_BM', '[UI_PANEL_BM] = UI_ON_NONE'),
    ('ui/ui.c', '[UI_PANEL_KEYS] = UI_ON_KEYS', '[UI_PANEL_KEYS] = UI_ON_NONE'),
    ('ui/ui.c', '[UI_PANEL_ABOUT] = UI_ON_ABOUT', '[UI_PANEL_ABOUT] = UI_ON_NONE'),
    ('ui/ui.c', '[UI_PANEL_BAR] = UI_ON_BAR', '[UI_PANEL_BAR] = UI_ON_NONE'),
    ('ui/edit.c', '    else if (on != UI_ON_NONE) return false;   // (a question, or a panel without a field)\n', ''),
    ('ui/handle.c', '        on = ui_on_top(u), prompt = on != UI_ON_NONE;   // (an earlier key may have opened or closed one)\n', ''),
    ('ui/handle.c', "            if (in->jobs_all <= 0) return (UiAction){ UA_QUIT, 0, false };", "            if (in->job_n <= 0) return (UiAction){ UA_QUIT, 0, false };"),
    ('ui/handle.c', "            u->quit_asking = true, u->cancel_asking = false;", "            u->quit_asking = true;"),
    ('ui/handle.c', '            if (!answered) continue;\n            UiAction a = ui_answer(u, on, yes);', '            UiAction a = ui_answer(u, on, yes);'),
    ('ui/prompts.c', '    case UI_ON_QUIT: u->quit_asking = false; return', '    case UI_ON_QUIT: return'),
    ('ui/prompts.c', 'e.key == UK_ENTER || e.key == UK_SHIFT_ENTER ||', 'e.key == UK_ENTER ||'),
    ('ui/prompts.c', "    bool no = e.key == UK_ESC || (e.key == UK_NONE && (e.ch == 'n' || e.ch == 'N'));\n    *answered", "    bool no = e.key == UK_NONE && (e.ch == 'n' || e.ch == 'N');\n    *answered"),
]
MUTANTS += [
    (Q, "k >= 0 && ms - l->at_ms[k] <= JOBQ_SCALE_MS; k--)", "k >= 0; k--)"),
    (Q, "k >= 0 && ms - l->at_ms[k] <= JOBQ_SCALE_MS; k--)", "k >= 0 && ms - l->at_ms[k] < JOBQ_SCALE_MS; k--)"),
    (Q, "        if (l->speed[k] > top) top = l->speed[k];", ""),
    (Q, "    if (top <= 0 && l->reached > 0) top = l->speed[l->reached - 1];\n", ""),
    (Q, "    return top * 1.2f;", "    return top;"),
    (Q, "l->at_ms[l->reached] = ms, ", ""),
]
MUTANTS += [
    (Q, "    if (!happened && ms - *last < JOBQ_FRAME_MS) return false;", "    if (ms - *last < JOBQ_FRAME_MS) return false;"),
    (Q, "    if (!happened && ms - *last < JOBQ_FRAME_MS) return false;", "    if (!happened && ms - *last <= JOBQ_FRAME_MS) return false;"),
    (Q, "    if (!happened && ms - *last < JOBQ_FRAME_MS) return false;", ""),
    (Q, "    *last = ms;\n    return true;", "    return true;"),
]
# ---- F5.4: a missing icon theme falls back
MUTANTS += [
    (I, "    for (Str name = theme; name.len && db->n == 0;) {", "    for (Str name = theme; name.len && db->n == 0; name.len = 0) {"),
    (I, "        while (dash > 0 && name.p[dash - 1] != '-') dash--;", "        while (dash > 0 && name.p[dash - 1] != '-') dash--, dash--;"),
    (I, "        name.len = dash > 1 ? dash - 1 : 0;", "        name.len = dash > 1 ? dash : 0;"),
    (I, "        name.len = dash > 1 ? dash - 1 : 0;", "        name.len = dash - 1;"),
    (I, "    if (db->n == 0) icon_add_theme(a, db, bases, nb, S(\"Yaru\"), 0, apps);\n", ""),
    (I, "    if (db->n == 0) icon_add_theme(a, db, bases, nb, S(\"Adwaita\"), 0, apps);\n", ""),
]
# ---- F6: places and drives, the headings, the modes, the state, the status line, the tree, the trash view
ST, TR = 'fs/state.c', 'fs/tree.c'
MUTANTS += [
    ('fs/places.c', '        if (place_is_drive(pl->p[i].kind) == place_is_drive(kind) && place_is_net(pl->p[i].kind) == place_is_net(kind) && strcmp(pl->p[i].path, path) == 0) return true;', '        if (strcmp(pl->p[i].path, path) == 0) return true;'),   # (retargeted, step 8 A)
    (PL, "    places_add(pl, PL_TRASH, TRASH_URI, S(\"Trash\"));\n", ""),
    (PL, "    places_head(pl, \"Drives\");\n", ""),
    (PL, "        if (pl->p[i].kind == PL_TRASH && strcmp(path, TRASH_URI) == 0) return i;\n", ""),
    ('fs/places.c', '        if (pl->p[i].kind == PL_HEAD || pl->p[i].kind == PL_TRASH || place_folderless(pl->p[i].kind)) continue;\n', ''),   # (retargeted, step 8 A)
    (PL, "        names[n++] = S(\"user-trash\");\n        break;\n", "        break;\n"),
    (PL, "    *size = (i64)v.f_blocks * (i64)v.f_frsize;", "    *size = 0;"),
    ('ui/cursor.c', "    while (ui_side_head(u, i)) i += dir;", "    if (ui_side_head(u, i)) i += dir;"),
    ('ui/cursor.c', "    if (s->top == s->sel && ui_side_head(u, s->sel - 1)) s->top--;\n", ""),
    ('ui/handle.c', '    if (!prompt && in->side_click >= 0 && ui_side_head(u, u->side.top + in->side_click)) return *out = UI_NONE, true;\n', ''),
    ('ui/handle.c', " && !ui_side_head(u, u->side.top + in->mid_side)) return", ") return"),
    ('ui/handle.c', ' ? u->side_here : 0, ui_side_settle(u, 1);', ' ? u->side_here : 0;'),
    ('ui/handle.c', '    case UK_F9: return *out = ui_side_mode(u, (SideMode)((u->side_mode + 1) % SIDE_MODES)), true;', '    case UK_F9: return *out = ui_side_mode(u, (SideMode)((u->side_mode + 1) % 2)), true;'),
    ('ui/cursor.c', '    if (m == SIDE_HIDDEN) ui_unfocus(u, UI_FOCUS_SIDE);\n', ''),
    ('ui/handle.c', "    if (!prompt && in->side_mode >= 0 && in->side_mode < SIDE_MODES) return", "    if (!prompt && in->side_mode >= 0) return"),
    ('ui/cursor.c', " || in->job_x >= 0 || in->side_mode >= 0 || in->side_twisty >= 0 || in->side_eject >= 0 ||", " || in->job_x >= 0 || in->side_twisty >= 0 || in->side_eject >= 0 ||"),
    ('ui/cursor.c', " || in->mid_crumb >= 0 || in->modal > 0 ||", " || in->mid_crumb >= 0 ||"),
    ('ui/cursor.c', "           in->backs > 0 || in->forwards > 0 || in->job_x", "           in->forwards > 0 || in->job_x"),
    ('ui/helpers.c', "        if (in->one_dir && in->one_count >= 0) snprintf", "        if (in->one_dir) snprintf"),
    ('ui/helpers.c', "\" (%td item%s)\", in->one_count, in->one_count == 1 ? \"\" : \"s\");", "\" (%td items)\", in->one_count);"),
    ('ui/helpers.c', "        else if (!in->one_dir && in->one_size >= 0) ui_size", "        else if (in->one_size >= 0) ui_size"),
    ('ui/helpers.c', "    if (in->marked > 1) {\n        snprintf(buf, cap, \"%td items selected\", in->marked);\n        return;\n    }\n", ""),
    ('ui/helpers.c', "    if (k > 0 && in->hidden && (size_t)k < cap) k += snprintf", "    if (k > 0 && (size_t)k < cap) k += snprintf"),
    ('ui/helpers.c', "    if (k > 0 && in->avail >= 0 && (size_t)k < cap) ui_size", "    if (k > 0 && (size_t)k < cap) ui_size"),
    ('ui/handle.c', "    if (!prompt && in->side_twisty >= 0 && u->side_mode == SIDE_TREE && u->side.top", "    if (!prompt && in->side_twisty >= 0 && u->side.top"),
    ('ui/handle.c', '        if (u->view == UI_VIEW_TRASH && files) return *out = (UiAction){ UA_RESTORE, 0, false }, true;', '        if (files) return *out = (UiAction){ UA_RESTORE, 0, false }, true;'),
    ('ui/handle.c', '        if (files && u->view == UI_VIEW_TRASH) u->confirming = true,', '        if (0) u->confirming = true,'),
    ('ui/prompts.c', 'yes ? UA_EMPTY_TRASH : UA_NONE', 'UA_EMPTY_TRASH'),
    ('ui/handle.c', '        case UB_RESTORE: if (u->view == UI_VIEW_TRASH && l->n > 0) return', '        case UB_RESTORE: if (l->n > 0) return'),
    ('ui/handle.c', '        case UB_EMPTY: if (u->view == UI_VIEW_TRASH) u->empty_asking = true; break;', '        case UB_EMPTY: u->empty_asking = true; break;'),
    ('ui/handle.c', '            UiAction a = ui_answer(u, on, yes);\n            if (a.kind != UA_NONE) return a;\n            continue;', '            UiAction a = ui_answer(u, on, yes);\n            if (a.kind != UA_NONE) return a;'),
    ('ui/helpers.c', "    if (str_eq(ui_crumb_path(c, i), S(TRASH_URI))) return S(\"Trash\");\n", ""),
    (ST, "        if (!str_eq(key, S(\"sidebar\"))) continue;\n", ""),
    (ST, "    if (!ops_allowed(path, err)) return false;\n    char dir[PATH_MAX], tmp", "    char dir[PATH_MAX], tmp"),
    ('fs/paths.c', '    if (ok && rename(tmp, path) == 0) return true;', '    if (ok && link(tmp, path) == 0) return true;'),
    (TR, "    tree_open(t, S(\"/\"));\n", ""),
    (TR, "    if (de->d_type != DT_LNK && de->d_type != DT_UNKNOWN) return false;", "    if (de->d_type != DT_UNKNOWN) return false;"),
    (TR, "(nm[0] == '.' && !hidden)", "(nm[0] == '.')"),
    (TR, "    qsort(names, (size_t)n, sizeof *names, tree_name_cmp);\n", ""),
    (TR, "        t->cut = true;\n", ""),
    (TR, "    t->n = 0, t->cut = false;", "    t->n = 0;"),
    (TR, "        if (t->row[k].depth < t->row[i].depth) return k;", "        if (t->row[k].depth <= t->row[i].depth) return k;"),
    (TR, "    *toggle = i >= 0 && i < t->n && t->row[i].open;", "    *toggle = i >= 0 && i < t->n && !t->row[i].open;"),
    (TR, "    return i + 1 < t->n && t->row[i + 1].depth > t->row[i].depth ? i + 1 : i;", "    return i + 1 < t->n ? i + 1 : i;"),
    (TR, "    memmove(t->open[i], t->open[i + 1], sizeof t->open[0] * (size_t)(t->nopen - i - 1));\n", ""),
    (TR, ".name = path.len == 1 ? S(\"/\") : path_base(path),", ".name = path_base(path),"),
    (TR, "    if (path.len >= PATH_MAX || tree_is_open(t, path)) return;", "    if (path.len >= PATH_MAX) return;"),
    (TR, "    if (d.len == 0 || d.p[0] != '/') return;", "    if (d.len == 0) return;"),
    (T, "    for (int i = 0; i < d->n; i++)\n        if (strcmp(d->dir[i], dir) == 0) return;\n", ""),
    (T, "    if (lstat(info, &st) != 0 || !S_ISDIR(st.st_mode)) return;\n    snprintf(d->dir[d->n]", "    snprintf(d->dir[d->n]"),
    (T, "        snprintf(dir, sizeof dir, \"%s%s.Trash/%u\", mp, sep, uid);\n        trash_dirs_add(d, dir, mp, TRASH_ADMIN);\n", ""),
    (T, "            in_group = str_eq(line, S(\"[Trash Info]\"));", "            in_group = true;"),
    (T, "        if (!in_group) continue;\n", ""),
    (T, "            else continue;   // relative in the home trash: nowhere to put it", "            else n = snprintf(from, cap, \"%s\", dec);"),
    (T, "            if (fstatat(ffd, item, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;   // no item: an orphan", ""),
    (T, "                out->hidden++;\n                if (!show_hidden) continue;", "                if (!show_hidden) continue;"),
    (T, "                out->hidden++;\n                if (!show_hidden) continue;", "                out->hidden++;"),
    (T, "                .mtime = when,\n", ""),
    (T, "    out->e = e, out->n = n;\n    fs_sort(out, sort);\n    return true;\n}\n\n// A trashed item back", "    out->e = e, out->n = n;\n    return true;\n}\n\n// A trashed item back"),
    (T, "            if (end && *end == 0) tm.tm_isdst = -1, *when = (i64)mktime(&tm);", ""),
]
# ---- F6 review: a drive's trash can't send a restore off its drive
MUTANTS += [
    (T, " || !dec[0] || trash_path_climbs(dec)) continue;", " || !dec[0]) continue;"),
    (T, "    if (t->top && t->top[0]) {\n        char res[PATH_MAX], top[PATH_MAX];", "    if (0) {\n        char res[PATH_MAX], top[PATH_MAX];"),
    (T, "        if (e - i == 2 && p.p[i] == '.' && p.p[i + 1] == '.') return true;", "        if (e - i >= 2 && p.p[i] == '.' && p.p[i + 1] == '.') return true;"),
    (T, "        if (e - i == 2 && p.p[i] == '.' && p.p[i + 1] == '.') return true;", "        if (e - i == 2 && p.p[i] == '.') return true;"),
    (T, "                              arena_cstr(a, str_from_c(d->top[k])) };", "                              \"\" };"),
]
# ---- F6 review: .trashinfo kept when its item fails; Empty Trash on the contents; owned drive trashes; errors outlive a key
MUTANTS += [
    (J, "            if (it->after_ok && (i == 0 || j->item[i - 1].out != OUT_NEW)) {", "            if (it->after_ok && j->item[i - 1].out != OUT_NEW) {"),
    (J, "            if (it->after_ok && (i == 0 || j->item[i - 1].out != OUT_NEW)) {", "            if (0) {"),
    (J, "                it->out = OUT_SKIPPED;\n                continue;", "                continue;"),
    (T, "    if (kind == TRASH_HOME) return true;\n", ""),
    (T, "    if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) return false;", "    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) return false;"),
    (T, "    if (kind == TRASH_USER) return true;\n", ""),
    (T, "    return lstat(a, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX);", "    return lstat(a, &st) == 0 && S_ISDIR(st.st_mode);"),
    (T, "    return lstat(a, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX);", "    return stat(a, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX);"),
    (T, "                    if (*n + 2 > max) return closedir(dir), false;", "                    if (*n + 1 > max) return closedir(dir), false;"),
    (T, "                    if (lstat(other, &st) == 0) out[(*n)++] = (TrashDoomed){ arena_cstr(a, str_from_c(other)), true };", "                    if (lstat(other, &st) == 0) out[(*n)++] = (TrashDoomed){ arena_cstr(a, str_from_c(other)), false };"),
    (T, "                        if (lstat(other, &st) == 0) continue;", ""),
    (T, "        if (d->top[k][0] == 0 && lstat(cache, &st) == 0) {", "        if (lstat(cache, &st) == 0) {"),
    (T, "            if (*n + 1 > max) return false;\n            out[(*n)++] = (TrashDoomed){ arena_cstr(a, str_from_c(cache)), false };", "            out[(*n)++] = (TrashDoomed){ arena_cstr(a, str_from_c(cache)), false };"),
    ('ui/cursor.c', "    return !bad || now_ms - at_ms >= UI_ERROR_MIN_MS;", "    return !bad || now_ms - at_ms > UI_ERROR_MIN_MS;"),
    ('ui/cursor.c', "    return !bad || now_ms - at_ms >= UI_ERROR_MIN_MS;", "    return true;"),
]
# ---- F6.1: a drive's device, eject's command and result, the eject click
SP = 'fs/spawn.c'
MUTANTS += [
    (PL, "        if (!octal_decode(src, p->dev, sizeof p->dev)) p->dev[0] = 0;", "        snprintf(p->dev, sizeof p->dev, \"%.*s\", STR_ARG(src));"),
    (PL, "    return p->kind == PL_REMOVABLE && strncmp(p->dev, \"/dev/\", 5) == 0;", "    return p->kind == PL_REMOVABLE && p->dev[0];"),
    (PL, "    return p->kind == PL_REMOVABLE && strncmp(p->dev, \"/dev/\", 5) == 0;", "    return place_is_drive(p->kind) && strncmp(p->dev, \"/dev/\", 5) == 0;"),
    (SP, "        n += take;", "        n += (size_t)r;"),
    (SP, "(size_t)r < room ? (size_t)r : room;", "(size_t)r;"),
    ('fs/spawn.c', '"\\"$2\\" unmount -b \\"$1\\" && echo OFX-UNMOUNTED && \\"$2\\" power-off', '"\\"$2\\" unmount -b \\"$1\\"; echo OFX-UNMOUNTED && \\"$2\\" power-off'),
    (SP, "    return done && unmounted ? EJECT_DONE : unmounted ? EJECT_UNMOUNTED : EJECT_FAILED;", "    return done ? EJECT_DONE : unmounted ? EJECT_UNMOUNTED : EJECT_FAILED;"),
    (SP, "        else if (line.len && !why[0]) snprintf(why, cap, \"%.*s\", STR_ARG(line));", "        else if (line.len) snprintf(why, cap, \"%.*s\", STR_ARG(line));"),
    (SP, "        line = str_trim(line);\n        if (str_eq(line, S(\"OFX-UNMOUNTED\")))", "        if (str_eq(line, S(\"OFX-UNMOUNTED\")))"),
    ('ui/cursor.c', " || in->side_twisty >= 0 || in->side_eject >= 0 ||", " || in->side_twisty >= 0 ||"),
    ('ui/handle.c', "    if (!prompt && in->side_eject >= 0 && u->side_mode == SIDE_PLACES && u->side.top", "    if (!prompt && in->side_eject >= 0 && u->side.top"),
]
MUTANTS += [
    (T, "    if (at + k < n && d[at + k].after_ok) k--;", ""),
    (T, "    isize k = n - at < max ? n - at : max;", "    isize k = max;"),
]
# ---- F6.1 review: spawn_capture's deadline
MUTANTS += [
    (SP, "            kill(-pid, SIGKILL);", "            kill(pid, SIGKILL);"),
    (SP, "            kill(-pid, SIGKILL);\n", ""),
    (SP, "            *timed_out = true;\n", ""),
    (SP, "        int ready = left > 0 ? poll(&p, 1, (int)left) : 0;", "        int ready = poll(&p, 1, -1);"),
]
# ---- F6.2: the icon view's state word, cursor, scroll and names
MUTANTS += [
    ('ui/cursor.c', "    return l->cols > 1 ? l->cols : 1;", "    return 1;"),
    ('ui/cursor.c', "all = (l->n + c - 1) / c;", "all = l->n / c;"),
    ('ui/cursor.c', "    return all > rows ? (all - rows) * c : 0;", "    return all > rows ? all - rows : 0;"),
    ('ui/cursor.c', "    return ui_clamp(top, 0, ui_max_top(l));", "    return top;"),
    ('ui/cursor.c', "    if (row >= top + rows) top = row - rows + 1;", "    if (row >= top + rows) top = row - rows;"),
    ('ui/cursor.c', "    l->cols = cols;\n    l->top = ui_top_at(l, l->top);\n    ui_show_sel(l);", "    l->cols = cols;\n    l->top = ui_top_at(l, l->top);"),
    ('ui/cursor.c', "    l->top = ui_top_at(l, l->top - (isize)notches * ui_cols(l));", "    l->top = ui_top_at(l, l->top - (isize)notches);"),
    ('ui/cursor.c', "    if (to < 0) to = col;", "    if (to < 0) to = 0;"),
    ('ui/cursor.c', "    if (to >= l->n) to = (l->n - 1) / c * c + col;", "    if (to >= l->n) to = l->n - 1;"),
    ('ui/cursor.c', "    if (to >= l->n) to = l->n - 1;\n    l->sel = to;", "    l->sel = to;"),
    ('ui/cursor.c', "    if (!grid) ui_set_cols(&u->list, 1);\n", ""),
    ('ui/handle.c', '    bool grid = u->grid && m == l;', '    bool grid = u->grid;'),
    ('ui/handle.c', '    if (grid && (key == UK_LEFT || key == UK_RIGHT)) {', '    if (grid && key == UK_RIGHT) {'),
    ('ui/handle.c', '        ui_move_rows(l, key == UK_PAGEUP ? -page : page);', '        ui_move_rows(l, key == UK_PAGEUP ? -1 : 1);'),
    ('ui/handle.c', '    case UK_VIEW_ICONS: return *out = ui_view(u, true), true;', '    case UK_VIEW_ICONS: return *out = ui_view(u, false), true;'),
    ('ui/handle.c', '        case UB_VIEW_ICONS: return *out = ui_view(u, true), true;', ''),
    ('ui/helpers.c', "    return c == ' ' || c == '-' || c == '_' || c == '.';", "    return c == ' ';"),
    ('ui/helpers.c', "        if (n == most - 1 || ui_width(rest, adv) <= width) {", "        if (ui_width(rest, adv) <= width) {"),
    ('ui/helpers.c', "            if (w + a > width && end > at) break;", "            if (w + a > width) break;"),
    ('ui/helpers.c', "        isize e = brk > at ? brk : end;", "        isize e = end;"),
    ('ui/helpers.c', "    while (n < most && (at < s.len || n == 0)) {", "    while (n < most && at < s.len) {"),
]
# ---- F6.2 keys (Michael, 2026-10-24): Left/Right only in the tree; Tab and Ctrl+Left to the sidebar
MUTANTS += [
    ('ui/handle.c', '        if (key == UK_TAB && u->focus == UI_FOCUS_SIDE) {', '        if (u->focus == UI_FOCUS_SIDE) {'),
    ('ui/handle.c', '            UiAction a = ui_side_mode(u, SIDE_PLACES);\n            u->focus = UI_FOCUS_SIDE;', '            UiAction a = ui_side_mode(u, SIDE_PLACES);'),
    ('ui/handle.c', '        if (u->side_mode == SIDE_HIDDEN) {\n            UiAction a', '        if (0) {\n            UiAction a'),
    ('ui/handle.c', '        if (u->focus == UI_FOCUS_SIDE && u->side.n > 0 && u->side_mode == SIDE_TREE) return *out = (UiAction){ UA_TREE_RIGHT, u->side.sel, false }, true;\n        break;', '        if (u->focus == UI_FOCUS_SIDE && u->side.n > 0 && u->side_mode == SIDE_TREE) return *out = (UiAction){ UA_TREE_RIGHT, u->side.sel, false }, true;\n        if (files) return *out = (UiAction){ UA_OPEN, l->sel, false }, true;\n        break;'),
    ('ui/handle.c', '        if (u->focus == UI_FOCUS_SIDE && u->side.n > 0 && u->side_mode == SIDE_TREE) return *out = (UiAction){ UA_TREE_LEFT, u->side.sel, false }, true;\n        break;', '        if (u->focus == UI_FOCUS_SIDE && u->side.n > 0 && u->side_mode == SIDE_TREE) return *out = (UiAction){ UA_TREE_LEFT, u->side.sel, false }, true;\n        return *out = (UiAction){ UA_PARENT, 0, false }, true;'),
]
# ---- tooltips (Michael, 2026-10-24)
MUTANTS += [
    ('ui/helpers.c', "    if (target == t->target) return false;\n    *t = (UiTip)", "    *t = (UiTip)"),
    ('ui/helpers.c', "    return t->target >= 0 && !t->shown && !t->off ? t->since + UI_TIP_MS : 0;", "    return t->target >= 0 && !t->shown && !t->off ? t->since : 0;"),
    ('ui/helpers.c', "    return t->target >= 0 && !t->shown && !t->off ? t->since + UI_TIP_MS : 0;", "    return t->target >= 0 && !t->shown ? t->since + UI_TIP_MS : 0;"),
    ('ui/helpers.c', "    if (!due || now < due) return false;", "    if (!due || now <= due) return false;"),
    ('ui/helpers.c', "    t->shown = false, t->off = t->target >= 0;", "    t->shown = false, t->off = true;"),
    ('ui/helpers.c', "    t->shown = false, t->off = t->target >= 0;", "    t->off = t->target >= 0;"),
]
# ---- F6.3: the icon view's sizes
MUTANTS += [
    ('ui/cursor.c', "    if (px <= 0) px = UI_ZOOM_DEFAULT;", ""),
    ('ui/cursor.c', "        if (abs(UI_ZOOM_PX[k] - px) < abs(UI_ZOOM_PX[best] - px)) best = k;", "        if (abs(UI_ZOOM_PX[k] - px) <= abs(UI_ZOOM_PX[best] - px)) best = k;"),
    ('ui/cursor.c', "    for (int k = 1; k < UI_ZOOMS; k++)\n        if (abs(", "    for (int k = 1; k < UI_ZOOMS - 1; k++)\n        if (abs("),
    ('ui/cursor.c', "    k = (int)ui_clamp(k, 0, UI_ZOOMS - 1);", "    k = (int)ui_clamp(k, 0, UI_ZOOMS - 2);"),
    ('ui/cursor.c', '    if (!u->grid || UI_ZOOM_PX[k] == ui_zoom_px(u)) return UI_NONE;', '    if (UI_ZOOM_PX[k] == ui_zoom_px(u)) return UI_NONE;'),
    ('ui/cursor.c', '    if (!u->grid || UI_ZOOM_PX[k] == ui_zoom_px(u)) return UI_NONE;', '    if (!u->grid) return UI_NONE;'),
    ('ui/cursor.c', "    return (UiAction){ UA_ZOOM, u->zoom, false };", "    return (UiAction){ UA_ZOOM, 0, false };"),
    ('ui/handle.c', '    if (!prompt && in->zoom_to >= 0) return *out = ui_zoom(u, in->zoom_to), true;', '    if (in->zoom_to >= 0) return *out = ui_zoom(u, in->zoom_to), true;'),
    ('ui/handle.c', '    if (!prompt && in->zoom_to >= 0) return *out = ui_zoom(u, in->zoom_to), true;', ''),
    ('ui/handle.c', '    if (!prompt && in->zoom_wheel) return *out = ui_zoom_by(u, in->zoom_wheel > 0 ? 1 : -1), true;', '    if (in->zoom_wheel) return *out = ui_zoom_by(u, in->zoom_wheel > 0 ? 1 : -1), true;'),
    ('ui/handle.c', 'ui_zoom_by(u, in->zoom_wheel > 0 ? 1 : -1)', 'ui_zoom_by(u, in->zoom_wheel)'),
    ('ui/handle.c', 'ui_zoom_by(u, in->zoom_wheel > 0 ? 1 : -1)', 'ui_zoom_by(u, in->zoom_wheel > 0 ? -1 : 1)'),
    ('ui/cursor.c', '    return ui_zoom(u, ui_zoom_level(u->zoom) + step);', '    return ui_zoom(u, ui_zoom_level(u->zoom) - step);'),
    ('ui/handle.c', '        case UB_ZOOM_OUT: return *out = ui_zoom_by(u, -1), true;', ''),
    ('ui/handle.c', '        case UB_ZOOM_IN: return *out = ui_zoom_by(u, 1), true;', ''),
    ('ui/handle.c', '    case UK_ZOOM_IN: return *out = ui_zoom_by(u, 1), true;', ''),
    ('ui/handle.c', '    case UK_ZOOM_OUT: return *out = ui_zoom_by(u, -1), true;', '    case UK_ZOOM_OUT: return *out = ui_zoom_by(u, 1), true;'),
    ('ui/handle.c', '    case UK_ZOOM_RESET: return *out = ui_zoom(u, ui_zoom_level(UI_ZOOM_DEFAULT)), true;', '    case UK_ZOOM_RESET: return *out = ui_zoom(u, 0), true;'),
    ('ui/cursor.c', "           in->zoom_wheel != 0 || in->zoom_to >= 0 ||", "           in->zoom_to >= 0 ||"),
    ('ui/cursor.c', "           in->zoom_wheel != 0 || in->zoom_to >= 0 ||", "           in->zoom_wheel != 0 ||"),
    (U, "                      .zoom_to = -1, .menu_title", "                      .zoom_to = 0, .menu_title"),
    (U, ".menu_title = -1, .menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box", ".menu_title = 0, .menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box"),
    (U, ".menu_title = -1, .menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box", ".menu_title = -1, .menu_item = 0, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box"),
    ('ui/cursor.c', "    isize c = ui_cols(l), on = l->sel / c - l->top / c;", "    isize c = ui_cols(l), on = 0;"),
    ('ui/cursor.c', "    l->top = (l->sel / c - on) * c;   // out", "    l->top = l->top;   // out"),
    (I, "    return hi * d->scale < size * scale;", "    return hi * d->scale <= size * scale;"),
    (I, "    int hi = d->type == IT_FIXED ? d->size : d->type == IT_SCALABLE ? d->max : d->size + d->threshold;", "    int hi = d->size;"),
    (I, "        if (!best || (best_short && !short_) || (best_short == short_ && dist < best_d)) best", "        if (!best || dist < best_d) best"),
    (I, "        if (!best || (best_short && !short_) || (best_short == short_ && dist < best_d)) best", "        if (!best || (best_short && !short_) || dist < best_d) best"),
]
# ---- files open with gio, as Nemo does (Michael, 2026-10-24)
MUTANTS += [
    (SP, '    const char *cmd[] = { "gio", "open", path };', '    const char *cmd[] = { "xdg-open", path };'),
    (SP, '    const char *cmd[] = { "gio", "open", path };', '    const char *cmd[] = { "gio", path };'),
]
# ---- F6.4: previews, the thumbnail cache and the decoders
TH = 'fs/thumb.c'
CL = 'fs/clean.c'
MUTANTS += [
    (TH, '    if (str_eq(mime, S("image/jpeg"))) return TK_JPEG;\n', ''),
    (TH, 'b = b + md5_rotl(a + f + K[i] + w[g], R[(i / 16) * 4 + i % 4]);', 'b = b + md5_rotl(a + f + K[i] + w[g], R[i % 4]);'),
    (TH, '    while (m.n % 64 != 56) md5_add(&m, "", 1);', '    while (m.n % 64 != 55) md5_add(&m, "", 1);'),
    (TH, '(m.s[i / 4] >> (8 * (i % 4))) & 0xff);', '(m.s[i / 4] >> (8 * (3 - i % 4))) & 0xff);'),
    ('fs/paths.c', '    if (cap < 8) return false;\n    memcpy(out, "file://", 7);', '    memcpy(out, "file://", 7);'),
    (TH, '    while (l < THUMB_LEVELS - 1 && (128 << l) < px) l++;', '    while (l < THUMB_LEVELS - 1 && (128 << l) <= px) l++;'),
    (TH, '    while (l < THUMB_LEVELS - 1 && (128 << l) < px) l++;', '    while (l < THUMB_LEVELS && (128 << l) < px) l++;'),
    (TH, '        if (n > (u64)(png.len - i - 12)) return false;\n', ''),
    (TH, '        if (memcmp(type, "IEND", 4) == 0) return true;\n', ''),
    (TH, '    return m->has_uri && strcmp(m->uri, uri) == 0 && m->has_mtime', '    return m->has_uri && m->has_mtime'),
    (TH, '&& m->has_mtime && m->mtime == mtime && (!m->has_size', '&& m->mtime == mtime && (!m->has_size'),
    (TH, ' && (!m->has_size || m->size == size);', ';'),
    ('fs/thumb.c', '    i64 v = s.len > 18 ? -1 :', '    i64 v = s.len > 19 ? -1 :'),
    ('fs/thumb.c', '    return v >= 0 && (*out = v, true);', '    return (*out = v, true);'),
    (TH, '        r[0] = 4;', '        r[0] = 1;'),
    ('fs/paths.c', '    bool ok = fchmod(fd, mode) == 0 && fd_write_all(fd, body, n) && (!sync || fsync(fd) == 0);', '    bool ok = fchmod(fd, mode) == 0 && (!sync || fsync(fd) == 0);'),
    (TH, '    int w = (int)(((i64)in.w * side + big / 2) / big), h', '    int w = (int)(((i64)in.w * side) / big), h'),
    (TH, '                    return v >= 1 && v <= 8 ? (int)v : 1;', '                    return (int)v;'),
    (TH, "            bool le = t[0] == 'I' && t[1] == 'I';", '            bool le = false;'),
    (TH, '            case 2: dx = w - 1 - x, dy = y; break;\n', ''),
    (TH, '            case 3: dx = w - 1 - x, dy = h - 1 - y; break;', '            case 3: dx = w - 1 - x, dy = y; break;'),
    (TH, '            case 4: dx = x, dy = h - 1 - y; break;\n', ''),
    (TH, '            case 5: dx = y, dy = x; break;', '            case 5: dx = h - 1 - y, dy = x; break;'),
    (TH, '            case 6: dx = h - 1 - y, dy = x; break;', '            case 6: dx = y, dy = x; break;'),
    (TH, '            case 7: dx = h - 1 - y, dy = w - 1 - x; break;', '            case 7: dx = y, dy = w - 1 - x; break;'),
    (TH, '            case 8: dx = y, dy = w - 1 - x; break;', '            case 8: dx = h - 1 - y, dy = w - 1 - x; break;'),
    (TH, '    bool swap = o >= 5;', '    bool swap = o > 5;'),
    (TH, '    if (tj3Set(tj, TJPARAM_MAXMEMORY, thumb_jpeg_max_mib) == 0 && tj3Set(tj, TJPARAM_SCANLIMIT, THUMB_JPEG_SCANS_MAX) == 0 &&', '    if ('),
    (TH, '    return r == 0 || tj3GetErrorCode(tj) == TJERR_WARNING;', '    return r == 0;'),
    (TH, '            if (s >= side && s < TJSCALED(big, best)) best = sf[k];', '            if (s < TJSCALED(big, best)) best = sf[k];'),
    (TH, '    return ok && img_fit(a, full, side, &fit) && (o == 1 ?', '    return ok && img_fit(a, full, side, &fit) && (1 ?'),
    (TH, '        c.options.use_scaling = 1;\n', ''),
    (TH, 'thumb_fresh(&m, uri, q->mtime, q->size) && png_decode(a, c, q->px, out)) return TG_CACHED;', 'png_decode(a, c, q->px, out)) return TG_CACHED;'),
    (TH, '    if (q->kind == TK_NONE || !q->make || !thumb_file(', '    if (!q->make || !thumb_file('),
    (TH, '    if (q->kind == TK_NONE || !q->make || !thumb_file(', '    if (q->kind == TK_NONE || !thumb_file('),
    (TH, '    if (f.len && png_meta(f, &m) && thumb_fresh(&m, uri, q->mtime, q->size)) return TG_FAIL_KNOWN;\n', ''),
    (TH, '    bool empty = S_ISREG(st.st_mode) && st.st_size == 0;', '    bool empty = false;'),
    (TH, '    if (!src.len && !empty) return TG_NONE;', '    if (!src.len) return TG_NONE;'),
    (TH, '        if (st.st_mtim.tv_sec != q->mtime || st.st_size != q->size) return TG_CHANGED;\n        made = !empty', '        made = !empty'),
    (TH, '        if (st.st_mtim.tv_sec != q->mtime || st.st_size != q->size) return TG_CHANGED;\n        ThumbGot g = thumb_exec', '        ThumbGot g = thumb_exec'),
    (TH, '        if (png_encode(a, (Img){ clear, 1, 1 }, uri, q->mtime, q->size, &png)) thumb_write(fail, png);', '        ;'),
    (TH, '    if (png_encode(a, t, uri, q->mtime, q->size, &png)) thumb_write(file, png);', '    ;'),
    (TH, '    return img_fit(a, t, q->px, out) ? TG_MADE : TG_NONE;', '    return img_fit(a, t, side, out) ? TG_MADE : TG_NONE;'),
    ('fs/paths.c', "        if (t.len >= n && memcmp(t.p, far[i], (size_t)n) == 0 && (t.len == n || t.p[n] == '.')) return true;", '        if (t.len >= n && memcmp(t.p, far[i], (size_t)n) == 0 && t.len == n) return true;'),   # (retargeted, step C)
    ('fs/paths.c', "        if (t.len >= n && memcmp(t.p, far[i], (size_t)n) == 0 && (t.len == n || t.p[n] == '.')) return true;", '        if (t.len >= n && memcmp(t.p, far[i], (size_t)n) == 0) return true;'),   # (retargeted, step C)
    (TH, '                            if (c == transparent || c >= map->ColorCount) continue;', '                            if (c >= map->ColorCount) continue;'),
    (TH, ' || d->Left + d->Width > g->SWidth || ', ' || '),
    (TH, '                    for (int y = d->Interlace ? start[pass] : 0; ok', '                    for (int y = 0; ok'),
    (TH, 'if (code == GRAPHICS_EXT_FUNC_CODE && ext && DGifExtensionToGCB(ext[0], ext + 1, &gcb) == GIF_OK) transparent = gcb.TransparentColor;', ';'),
    (TH, '                ok = true;\n                for (int pass', '                ok = false;\n                for (int pass'),
    (TH, '    im.format = PNG_FORMAT_RGBA;', '    im.format = PNG_FORMAT_BGRA;'),
    (TH, '    isize k = n < s->n - s->at ? n : s->n - s->at;', '    isize k = n;'),
    (M, '    if (!str_eq(media, S("application"))) names[n++] = S("application-x-generic");\n', ''),
    (M, '        if (up == 0) names[n++] = str_from_c(arena_sprintf(a, "%.*s-x-generic", STR_ARG(media)));\n', ''),
    # ---- F6.5: the thumbnail cache's cleanup
    ("fs/conf.c", "#define CLEAN_UNUSED_S   (90 * 86400LL)", "#define CLEAN_UNUSED_S   (88 * 86400LL)"),
    ("fs/conf.c", "#define CLEAN_UNUSED_S   (90 * 86400LL)", "#define CLEAN_UNUSED_S   (92 * 86400LL)"),
    (CL, "#define CLEAN_TMP_S    86400LL", "#define CLEAN_TMP_S    (3 * 86400LL)"),
    (CL, "        if (!((n[i] >= '0' && n[i] <= '9') || (n[i] >= 'a' && n[i] <= 'f'))) return false;", "        if (!((n[i] >= '0' && n[i] <= '9') || (n[i] >= 'a' && n[i] <= 'g'))) return false;"),
    (CL, "        if (!((n[i] >= '0' && n[i] <= '9') || (n[i] >= 'a' && n[i] <= 'f'))) return false;", "        if (!((n[i] >= '0' && n[i] <= ':') || (n[i] >= 'a' && n[i] <= 'f'))) return false;"),
    (CL, "    for (int i = 0; i < 32; i++)", "    for (int i = 0; i < 31; i++)"),
    (CL, '    return strcmp(n + 32, ".png") == 0;', '    return strncmp(n + 32, ".png", 4) == 0;'),
    (CL, '    if (strncmp(n, ".ofx-", 5) != 0) return false;', '    if (strncmp(n, ".ofx", 4) != 0) return false;'),
    (CL, "        if (p == d || p - d > 10 || *p != (part ? '.' : '-')) return false;", "        if (p - d > 10 || *p != (part ? '.' : '-')) return false;"),
    (CL, "        if (p == d || p - d > 10 || *p != (part ? '.' : '-')) return false;", "        if (p == d || *p != (part ? '.' : '-')) return false;"),
    (CL, "        if (p == d || p - d > 10 || *p != (part ? '.' : '-')) return false;", "        if (p == d || p - d > 10 || *p != (part ? '.' : '.')) return false;"),
    (CL, '    const char *p = n + 5;\n    for (int part = 0; part < 2; part++) {', '    const char *p = n + 5;\n    for (int part = 0; part < 1; part++) {'),
    (CL, '    return strcmp(p, "png") == 0;', '    return strncmp(p, "png", 3) == 0;'),
    ('fs/paths.c', '    MountEnt *e = arena_try_new(a, MountEnt, lines + 1);', '    MountEnt *e = arena_try_new(a, MountEnt, 1);'),   # (retargeted, step C)
    (CL, '{ { "/run/media/", 2 }, { "/media/", 1 }, { "/mnt/", 1 } };', '{ { "/run/media/", 1 }, { "/media/", 1 }, { "/mnt/", 1 } };'),
    (CL, '{ { "/run/media/", 2 }, { "/media/", 1 }, { "/mnt/", 1 } };', '{ { "/run/media/", 2 }, { "/media/", 1 } };'),
    (CL, '{ { "/run/media/", 2 }, { "/media/", 1 }, { "/mnt/", 1 } };', '{ { "/run/media/", 2 }, { "/mnt/", 1 } };'),
    (CL, "            if (!s) return false;   // the folder itself", "            if (!s) return true;   // the folder itself"),
    (CL, '        snprintf(top, sizeof top, "%.*s", (int)(p - 1 - path), path);', '        snprintf(top, sizeof top, "%.*s", (int)(p - path), path);'),
    (CL, "        return !clean_mounted(ms, top);", "        return true;"),
    (CL, "        if (strncmp(path, roots[i].prefix, n) != 0) continue;", "        if (strncmp(path, roots[i].prefix, n - 1) != 0) continue;"),
    ('fs/clean.c', 'file_read_at(a, dfd, name, THUMB_CACHED_MAX, &st, O_NOATIME | O_NOFOLLOW)', 'file_read_at(a, dfd, name, THUMB_CACHED_MAX, &st, O_NOFOLLOW)'),
    ('fs/clean.c', 'file_read_at(a, dfd, name, THUMB_CACHED_MAX, &st, O_NOATIME | O_NOFOLLOW)', 'file_read_at(a, dfd, name, THUMB_CACHED_MAX, &st, O_NOATIME)'),
    # Not a mutant: dropping `n == st.st_size` (a short read) is inert, as the loop reads to the
    # fstat size unless the file shrinks in the middle; kept as a guard.
    (CL, "            if (stat(path, &f) != 0) r = errno == ENOENT || errno == ENOTDIR ? CR_ORPHAN : CR_KEEP;", "            if (stat(path, &f) != 0) r = errno == ENOENT ? CR_ORPHAN : CR_KEEP;"),
    (CL, "            if (stat(path, &f) != 0) r = errno == ENOENT || errno == ENOTDIR ? CR_ORPHAN : CR_KEEP;", "            if (stat(path, &f) != 0) r = CR_ORPHAN;"),
    (CL, "            else if ((m->has_mtime && m->mtime != f.st_mtim.tv_sec) || (m->has_size && m->size != f.st_size)) r = CR_STALE;", "            else if (m->has_mtime && m->mtime != f.st_mtim.tv_sec) r = CR_STALE;"),
    (CL, "            else if ((m->has_mtime && m->mtime != f.st_mtim.tv_sec) || (m->has_size && m->size != f.st_size)) r = CR_STALE;", "            else if (m->has_size && m->size != f.st_size) r = CR_STALE;"),
    (CL, "    return a->atime < b->atime ? -1 : a->atime > b->atime;", "    return a->atime > b->atime ? -1 : a->atime < b->atime;"),
    (CL, '    if (!o->dry && (!ops_allowed(path, &err) || (unlinkat(fds[f->dir], f->name, 0) != 0 && errno != ENOENT))) {', '    if (!o->dry && (unlinkat(fds[f->dir], f->name, 0) != 0 && errno != ENOENT)) {'),
    (CL, '    if (!o->dry && (!ops_allowed(path, &err) || (unlinkat(fds[f->dir], f->name, 0) != 0 && errno != ENOENT))) {', '    if ((!ops_allowed(path, &err) || (unlinkat(fds[f->dir], f->name, 0) != 0 && errno != ENOENT))) {'),
    ('fs/clean.c', "(unlinkat(fds[f->dir], f->name, 0) != 0 && errno != ENOENT))) {   // (gone already: another clean, the timer's or a window's)\n        s->failed++;\n        return;", "(unlinkat(fds[f->dir], f->name, 0) != 0 && errno != ENOENT))) {   // (gone already: another clean, the timer's or a window's)\n        return;"),
    ('fs/clean.c', "(unlinkat(fds[f->dir], f->name, 0) != 0 && errno != ENOENT))) {   // (gone already: another clean, the timer's or a window's)\n        s->failed++;\n        return;", "(unlinkat(fds[f->dir], f->name, 0) != 0 && errno != ENOENT))) {   // (gone already: another clean, the timer's or a window's)\n        s->failed++;"),
    (CL, "    s->n[r]++, s->bytes[r] += f->size;", "    s->n[r]++;"),
    (CL, "    int rfd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);", "    int rfd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);"),
    (CL, "        int fd = openat(dirfd(fd_dir), d->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);", "        int fd = openat(dirfd(fd_dir), d->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);"),
    (CL, "    for (int i = 0; i < THUMB_LEVELS; i++) {", "    for (int i = 0; i < THUMB_LEVELS - 1; i++) {"),
    (CL, '    int ffd = openat(rfd, "fail", O_RDONLY', '    int ffd = openat(rfd, "fails", O_RDONLY'),
    (CL, "            if ((!temp && !clean_name(d->d_name)) || fstatat(", "            if (fstatat("),
    (CL, "fstatat(fds[i], d->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode)) continue;", "fstatat(fds[i], d->d_name, &st, 0) != 0 || !S_ISREG(st.st_mode)) continue;"),
    (CL, "fstatat(fds[i], d->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode)) continue;", "fstatat(fds[i], d->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;"),
    (CL, '                    : f->temp ? (o->now - f->mtime > CLEAN_TMP_S ? CR_TEMP : CR_KEEP)', '                    : f->temp ? (o->now - f->atime > CLEAN_TMP_S ? CR_TEMP : CR_KEEP)'),
    (CL, "                    : o->unused_s > 0 && o->now - f->atime > o->unused_s ? CR_UNUSED", "                    : o->unused_s > 0 && o->now - f->mtime > o->unused_s ? CR_UNUSED"),
    (CL, "        files[kept++] = *f, total += f->size;", "        files[kept++] = *f;"),
    # Not a mutant: `total >= o->cap` on the outer test is equivalent, the loop's own test deletes
    # nothing at exactly the cap (tested).
    (CL, "        for (isize i = 0; i < kept && total > o->cap; i++) {", "        for (isize i = 0; i < kept && total >= o->cap; i++) {"),
    (CL, "            if (s->n[CR_CAP] > before) total -= files[i].size;", "            total -= files[i].size;"),
    (CL, "        qsort(files, (size_t)kept, sizeof *files, clean_by_atime);\n", ""),
    (CL, "    s->n[CR_KEEP] = kept - s->n[CR_CAP], s->bytes[CR_KEEP] = total;", "    s->n[CR_KEEP] = kept, s->bytes[CR_KEEP] = total;"),
    # ---- F6.5 review: links followed (clean_real), stat for the file
    (CL, "            clean_real(ms, path, real, sizeof real) && !clean_absent_drive(ms, real)) {", "            clean_real(ms, path, real, sizeof real) && !clean_absent_drive(ms, path)) {"),
    (CL, "            clean_real(ms, path, real, sizeof real) && !clean_absent_drive(ms, real)) {", "            !clean_absent_drive(ms, path)) {"),
    (CL, "            if (stat(path, &f) != 0) r =", "            if (lstat(path, &f) != 0) r ="),
    # (dropped, step C: clean_real's own walk is path_walk now, whose own mutants cover this step)
    # ---- the mount list parsed once a run (clean_mounts, clean_local)
    ('fs/clean.c', '    Mounts ms = mounts_parse(a, mountinfo);', '    Mounts ms = mounts_parse(a, STR_EMPTY);'),   # (retargeted, step C)
    # (dropped, step C: a line with no type is left out now, not given its parent's verdict)
    # Not a mutant: `>` for `>=` in clean_local is equivalent, as far is thumb_local's verdict on
    # the mount point, the same for every line on that point.
    ('fs/paths.c', '    return !m || !m->far;', '    return !m || m->far;'),   # (retargeted, step C)
    ('fs/clean.c', '        if (strcmp(ms->e[i].mp, dir) == 0) return true;', '        if (strncmp(ms->e[i].mp, dir, strlen(dir)) == 0) return true;'),   # (retargeted, step C)
    # (dropped, step C: clean_real's own walk is path_walk now, whose own mutants cover this step)
    # (dropped, step C: clean_real's own walk is path_walk now, whose own mutants cover this step)
    # (dropped, step C: clean_real's own walk is path_walk now, whose own mutants cover this step)
    # (dropped, step C: clean_real's own walk is path_walk now, whose own mutants cover this step)
    ('fs/list.c', '        if (!there && !(flags & WALK_MISSING_OK)) return false;', '        if (!there) return false;'),   # (retargeted, step C)
    # (dropped, step C: clean_real's own walk is path_walk now, whose own mutants cover this step)
    # (dropped, step C: clean_real's own walk is path_walk now, whose own mutants cover this step)
    # (dropped, step C: clean_real's own walk is path_walk now, whose own mutants cover this step)
    # (dropped, step C: clean_real's own walk is path_walk now, whose own mutants cover this step)
]

# ---- F7.1: the menu bar, New Window, Create New Empty File, the state's one key, the registry
W = 'fs/windows.c'
MUTANTS += [
    ('ui/menus.c', "    case UM_NEW_TAB: case UM_CLOSE_ALL: case UM_CLOSE: case UM_PROPERTIES: return true;", "    case UM_NEW_TAB: case UM_CLOSE_ALL: case UM_CLOSE: return true;"),
    ('ui/menus.c', "    case UM_OPEN: return files;", "    case UM_OPEN: return true;"),
    ('ui/menus.c', "    default: return false;\n    }\n}\n\n// Entry it's mark", "    default: return true;\n    }\n}\n\n// Entry it's mark"),
    ('ui/menus.c', "        int i = ((at + dir * k) % n + n) % n;", "        int i = (at + dir * k) % n;"),
    # Not mutants while File is the only menu: `k < n` (no step round to the entry itself) and
    # `keys ? 0` (its first entry, New Tab) are equivalent, as New Tab, Close All and Close can
    # always be picked. Edit (F7.4) has entries that can't.
    ('ui/menus.c', "    u->menu_at = keys ? ui_menu_next(u, -1, 1) : -1;", "    u->menu_at = ui_menu_next(u, -1, 1);"),
    ('ui/menus.c', "    ui_edit_close(u), ui_find_leave(u);\n    ui_unfocus", "    ui_find_leave(u);\n    ui_unfocus"),
    ('ui/menus.c', "    ui_edit_close(u), ui_find_leave(u);\n    ui_unfocus", "    ui_edit_close(u);\n    ui_unfocus"),
    ('ui/menus.c', "    ui_unfocus(u, UI_FOCUS_SEARCH), ui_unfocus(u, UI_FOCUS_INSIDE);\n}", "    ui_unfocus(u, UI_FOCUS_INSIDE);\n}"),
    ('ui/menus.c', "    ui_unfocus(u, UI_FOCUS_SEARCH), ui_unfocus(u, UI_FOCUS_INSIDE);\n}", "    ui_unfocus(u, UI_FOCUS_SEARCH);\n}"),
    ('ui/menus.c', "        if (u->menu_field != UI_FOCUS_LIST) u->focus = u->menu_field;\n", ""),
    ('ui/menus.c', "        if (u->menu_field != UI_FOCUS_LIST) u->focus = u->menu_field;", "        u->focus = u->menu_field;"),
    ('ui/menus.c', 'static UiAction ui_menu_pick(Ui *u, UiMenuItem it)\n{\n    u->menu_on = false;', 'static UiAction ui_menu_pick(Ui *u, UiMenuItem it)\n{'),
    ('ui/menus.c', '    case UM_OPEN: return (UiAction){ UA_OPEN, u->list.sel, u->marked > 0 };', '    case UM_OPEN: return (UiAction){ UA_OPEN, 0, u->marked > 0 };'),
    ('ui/handle.c', '    return (UiAction){ UA_CLOSE_ALL, 0, in->jobs_all <= 0 };', '    return (UiAction){ UA_CLOSE_ALL, 0, false };'),
    ('ui/ui.c', '[UM_CLOSE] = { "Close", "close", .cmd = UK_TAB_CLOSE, .runs = true }', '[UM_CLOSE] = { "Close", "close", .cmd = UK_TAB_CLOSE }'),
    ('ui/menus.c', '        int at = ui_menu_next(u, u->menu_at < 0 && e.key == UK_UP ? 0 : u->menu_at, e.key == UK_DOWN ? 1 : -1);', '        int at = ui_menu_next(u, u->menu_at, e.key == UK_DOWN ? 1 : -1);'),
    ('ui/menus.c', '    case UK_HOME: u->menu_at = ui_menu_next(u, -1, 1); return true;', '    case UK_HOME: return true;'),
    ('ui/menus.c', '    case UK_END: u->menu_at = ui_menu_next(u, 0, -1); return true;', '    case UK_END: return true;'),
    ('ui/menus.c', '        if (u->menu_at >= 0 && ui_menu_enabled(u, UI_MENU[u->menu].items[u->menu_at])) *a =', '        if (u->menu_at >= 0) *a ='),
    ('ui/menus.c', '    case UK_TAB: u->menu_on = false; return true;\n', ''),
    ('ui/menus.c', '    case UK_ESC:\n        u->menu_on = false;\n', '    case UK_ESC:\n'),
    ('ui/menus.c', '        if (m == u->menu) u->menu_on = false;', '        if (false) u->menu_on = false;'),
    ('ui/menus.c', '    default: u->menu_on = false; return false;\n    }\n}', '    default: return false;\n    }\n}'),
    ('ui/menus.c', '    default: u->menu_on = false; return false;\n    }\n}', '    default: u->menu_on = false; return true;\n    }\n}'),
    ('ui/menus.c', '    if (title >= 0 && title < UI_MENUS && title != u->menu && u->menu < UI_MENUS) {   // (not from a right-click menu, F8)', '    if (title >= 0 && title < UI_MENUS) {'),
    ('ui/menus.c', '|| !ui_menu_enabled(u, UI_MENU[u->menu].items[item]) || item == u->menu_at) return false;', '|| item == u->menu_at) return false;'),
    ('ui/menus.c', '    if (!u->menu_on) return false;\n    if (title', '    if (title'),
    ('ui/handle.c', "    if (prompt) u->menu_on = false;\n", ""),
    ('ui/handle.c', "        if (u->menu_on && u->menu == in->menu_title) u->menu_on = false;", "        if (false) u->menu_on = false;"),
    ('ui/handle.c', '        if (!ui_menu_enabled(u, it)) return *out = UI_NONE, true;\n        UiAction a = ui_menu_pick(u, it);', '        UiAction a = ui_menu_pick(u, it);'),
    ('ui/handle.c', '    if (u->menu_on && in->menu_away) return u->menu_on = false, *out = UI_NONE, true;', '    if (u->menu_on && in->menu_away) u->menu_on = false;'),
    ('ui/handle.c', "    if (!prompt && in->menu_title >= 0 && in->menu_title < UI_MENUS) {", "    if (in->menu_title >= 0 && in->menu_title < UI_MENUS) {"),
    ('ui/handle.c', '    if (in->jobs_all > 0) u->quit_asking = true, u->cancel_asking = false;\n    return (UiAction){ UA_CLOSE_ALL', '    return (UiAction){ UA_CLOSE_ALL'),
    ('ui/handle.c', '                if (e.cmd == UK_QUIT_ALL && !u->quit_asking) return ui_quit_all(u, in);   // (read above, before the menus)\n', ''),
    ('ui/handle.c', '        if (e.cmd == UK_QUIT_ALL && !u->quit_asking) return ui_quit_all(u, in);\n', '        if (e.cmd == UK_QUIT_ALL) return ui_quit_all(u, in);\n'),
    ('ui/handle.c', '        if (!prompt && !u->menu_on && (e.cmd == UK_F10 ||', '        if (!u->menu_on && (e.cmd == UK_F10 ||'),
    ('ui/handle.c', "        if (u->menu_on && !prompt) {\n            UiAction a;\n            if (ui_menu_event(u, e, &a)) {", "        if (u->menu_on && !prompt && false) {\n            UiAction a;\n            if (ui_menu_event(u, e, &a)) {"),
    ('ui/cursor.c', "|| in->menu_title >= 0 || in->menu_item >= 0 || in->menu_away || in->ctx_row >= 0 || in->ctx_here || in->ctx_side >= 0 || in->ctx_crumb >= 0 || in->ctx_tab >= 0 || in->prop_box", " || in->menu_title >= 0 || in->menu_item >= 0 || in->ctx_row >= 0 || in->ctx_here || in->ctx_side >= 0 || in->ctx_crumb >= 0 || in->ctx_tab >= 0 || in->prop_box"),
    # Not a mutant: dropping O_EXCL in op_mkfile is inert without a race, as name_free picked a
    # name not there; kept so another program's file made in between is never truncated.
    ('fs/ops.c', '        bool ok = kind == MAKE_DIR ? mkdir', '        bool ok = kind != MAKE_DIR ? mkdir'),
    (N, "            if (st.st_size != 0) return err_set(err, \"%s isn't empty now: can't undo\", it->to), false;", ""),
    (N, "            if (lstat(it->to, &st) != 0 || !S_ISREG(st.st_mode)) return err_set(err, \"%s isn't a file now: can't undo\", it->to), false;", ""),
    (N, "        case UNDO_MKFILE: one = ops_allowed(it->to, &e) && (unlink", "        case UNDO_MKFILE: one = (unlink"),
    (N, "|| u->kind == UNDO_MKDIR || u->kind == UNDO_MKFILE || u->kind == UNDO_CHMOD) snprintf", "|| u->kind == UNDO_MKDIR || u->kind == UNDO_CHMOD) snprintf"),
    (W, "    if (t && t[0] == '/') n = snprintf(out, cap, \"%s\", t);", "    if (t && t[0]) n = snprintf(out, cap, \"%s\", t);"),
    (W, "    for (int field = 3; field < 22; field++) {", "    for (int field = 3; field < 21; field++) {"),
    (W, "    char *s = strrchr(buf, ')');", "    char *s = strchr(buf, ')');"),
    # Not a mutant: `end == s + 1` is inert with a real /proc (field 22 is always a number).
    (W, "    if (!ops_allowed(p, err) || !mkdir_private(dir, err)) return false;", "    if (!mkdir_private(dir, err)) return false;"),
    (W, "    if (t < 0) return err_set(err, \"no start time for process %d\", (int)pid), false;", ""),
    (W, "    if (snprintf(p, sizeof p, \"%s/%d\", dir, (int)pid) < (int)sizeof p && ops_allowed(p, &err)) unlink(p);", "    if (snprintf(p, sizeof p, \"%s/%d\", dir, (int)pid) < (int)sizeof p) unlink(p);"),
    (W, "    if (!ops_allowed(dir, &err)) return 0;   // the test guard", "    // the test guard"),
    (W, "        if (pid > INT32_MAX || (pid_t)pid == self) continue;", "        if (pid > INT32_MAX) continue;"),
    (W, "    if (name[0] < '1' || name[0] > '9') return false;", "    if (name[0] < '0' || name[0] > '9') return false;"),
    (W, "    return name[i] == 0 && i <= 10;", "    return i <= 10;"),
    (W, "    return name[i] == 0 && i <= 10;", "    return name[i] == 0;"),
    (W, "    if (n < 2 || buf[n - 1] != '\\n') return false;", "    if (n < 1) return false;"),
    ('fs/windows.c', '    return v >= 0 && (*t = v, true);', '    return (*t = v, true);'),
    (W, "        if (!win_name(e->d_name)) continue;", ""),
    (W, "        if (n < 0) continue;   // not a regular file: not ofx's", ""),
    (W, "            if (errno == ESRCH) unlink(p);   // gone", ""),
    (W, "        if (now < 0 || (whole && now != want)) {", "        if (now < 0) {"),
    # Not a mutant: dropping `now < 0` is inert but for a race (the process ending between
    # pidfd_open and the stat read); kept so such an entry goes at once.
    (W, "            unlink(p);   // gone, or the pid is another process's now", ""),
    (W, "        } else if (whole && syscall(SYS_pidfd_send_signal, pfd, sig, NULL, 0) == 0) {", "        } else if (syscall(SYS_pidfd_send_signal, pfd, sig, NULL, 0) == 0) {"),
    (W, "            told++;\n        }   // not whole", "        }   // not whole"),
    ('ui/handle.c', "    for (int k = 0; k < in->nev && !u->quit_asking; k++)\n        if (in->ev[k].key == UK_QUIT) {", "    for (int k = 0; k < 0; k++)\n        if (in->ev[k].key == UK_QUIT) {"),
    ('ui/handle.c', "            if (in->jobs_all <= 0) return (UiAction){ UA_QUIT, 0, false };\n            u->quit_asking = true, u->cancel_asking = false;\n        }\n    // The panels", "            if (in->jobs_all < 0) return (UiAction){ UA_QUIT, 0, false };\n            u->quit_asking = true, u->cancel_asking = false;\n        }\n    // The panels"),
    ('ui/handle.c', "            u->quit_asking = true, u->cancel_asking = false;\n        }\n    // The panels", "            u->quit_asking = true;\n        }\n    // The panels"),
    ('ui/edit.c', "    if (dot <= 1) return (int)name.len;", "    if (dot < 1) return (int)name.len;"),
    ('ui/edit.c', "    if (dot <= 1) return (int)name.len;", ""),
    ('ui/edit.c', "    if (dot > 4 && memcmp(name.p + dot - 4, \".tar\", 4) == 0) dot -= 4;", ""),
    ('ui/edit.c', "    if (dot > 4 && memcmp(name.p + dot - 4, \".tar\", 4) == 0) dot -= 4;", "    if (dot >= 4 && memcmp(name.p + dot - 4, \".tar\", 4) == 0) dot -= 4;"),
    ('ui/edit.c', "    while (dot > 0 && name.p[dot - 1] != '.') dot--;", "    while (dot > 0 && name.p[dot - 1] != '.') dot -= 2;"),
    ('ui/edit.c', '    if (!dir) u->edit.caret = ui_name_stem((Str){ u->edit.b, u->edit.len });', ''),
    ('ui/handle.c', "names.dir && names.dir(names.ctx, l->sel));", "false);"),
    # ---- F7.1.1: the default app for a type
]
AP = 'fs/apps.c'
MUTANTS += [
    (AP, "    if (ch && ch[0] == '/') dirs[nd++] = arena_cstr(a, str_from_c(ch));", "    if (ch && ch[0] == '/') {}"),
    (AP, "        if (e > at) dirs[nd++] = arena_cstr(a, str_cut(cds, at, e));", ""),
    (AP, "    for (isize k = 0; k < ndata; k++) dirs[nd++] = arena_sprintf(a, \"%s/applications\", data[k]);", ""),
    (AP, "STR_ARG(lower(a, str_cut(ds, at, e))));", "STR_ARG(str_cut(ds, at, e)));"),
    (AP, "        if (n < max) out[n++] = arena_sprintf(a, \"%s/mimeapps.list\", dirs[k]);", ""),
    (AP, "        for (isize at = 0; at < ds.len && n < max;) {", "        for (isize at = 0; at < 0;) {"),
    (AP, "        if (str_eq(str_trim(str_cut(list, at, e)), id)) return true;", ""),
    (AP, "    if (str_eq(ini_get(text, S(\"Desktop Entry\"), S(\"Hidden\")), S(\"true\"))) return false;", ""),
    # Not a mutant: dropping `t.len == 0` is inert, as on_path("") finds a folder, which access
    # takes as executable; kept to say what an app without TryExec is.
    (AP, "    return prog[0] == '/' ? access(prog, X_OK) == 0 : on_path(prog);", "    return true;"),
    (AP, "    return prog[0] == '/' ? access(prog, X_OK) == 0 : on_path(prog);", "    return prog[0] == '/' ? access(prog, X_OK) == 0 : true;"),
    (AP, "    if (id.len == 0 || str_find(id, '/') >= 0 || !str_ends_with(id, S(\".desktop\"))) return false;", "    if (id.len == 0) return false;"),
    (AP, "            dash = strchr(dash, '-');\n            if (!dash) break;\n            *dash = '/';", "            break;"),
    (AP, "    if (ok) ok = apps_usable(read_file(a, out));", ""),
    (AP, "    for (isize k = 0; k < nd && !ok; k++) {\n        char rel", "    for (isize k = 0; k < nd; k++) {\n        char rel"),
    (AP, "        if (id.len && !apps_listed(removed, id) && apps_find(a, id, out, cap)) return true;", "        if (id.len && apps_find(a, id, out, cap)) return true;"),
    (AP, "                if (!have && parent.len) out[n++] = parent;", ""),
    (AP, "    if (!plain && n < max && str_starts_with(type, S(\"text/\"))) out[n++] = S(\"text/plain\");", ""),
    (AP, "    for (isize i = 0; i < n && n < max; i++) {", "    for (isize i = 0; i < 1 && n < max; i++) {"),
    (AP, "                if (pass == 0 && str_eq(alias, *type)) *type = canon;", ""),
    (AP, "                if (pass == 1 && str_eq(canon, *type) && n < max - 1) out[1 + n++] = alias;", ""),
    (AP, "        if (r.len) removed = str_from_c(", "        if (false) removed = str_from_c("),
    (AP, "        for (isize k = 0; k < nl && !ok; k++) ok = apps_first_of(a, text[k], S(\"Default Applications\")", "        for (isize k = 0; k < 0 && !ok; k++) ok = apps_first_of(a, text[k], S(\"Default Applications\")"),
    (AP, "            removed = apps_removed(a, text[k], names, nn, removed);\n", ""),
    (AP, "            ok = apps_first_of(a, text[k], S(\"Added Associations\"), names, nn, removed, out, cap);", ""),
    (AP, "            ok = apps_first_of(a, cache, S(\"MIME Cache\"), names, nn, removed, out, cap);", ""),
    (AP, "    apps_names(a, &type, names, COUNT(names));   // canonical first\n", ""),
    (AP, "        isize nn = apps_names(a, &tt, names, COUNT(names));", "        isize nn = (names[0] = tt, 1);"),
    (SP, "    const char *cmd[] = { \"gio\", \"launch\", desktop, path };", "    const char *cmd[] = { \"gio\", \"open\", path };"),
    (AP, "    if (!str_eq(ini_get(text, S(\"Desktop Entry\"), S(\"Type\")), S(\"Application\"))) return false;", ""),
    # ---- F7.2: the Properties panel
]
PR = 'fs/props.c'
MUTANTS += [
    (PR, "    return w->gen && atomic_load(w->gen) != w->mine;", "    return false;"),
    (PR, "    if (depth > DU_DEPTH_MAX) return close(fd), s->cut = true, true;", ""),
    (PR, "        if (strcmp(e->d_name, \".\") == 0 || strcmp(e->d_name, \"..\") == 0) continue;", "        if (strcmp(e->d_name, \".\") == 0) continue;"),
    (PR, "        if (fstatat(dirfd(d), e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {", "        if (fstatat(dirfd(d), e->d_name, &st, 0) != 0) {"),
    # Not a mutant without a mount to walk onto: `st.st_dev != w->dev` (a drive mounted inside a
    # folder counted, not entered; network mounts below one never read). Kept; checked by hand.
    ('fs/props.c', '            if (sub < 0) s->cut = true;\n            else if (w->pts_len', '            if (sub < 0) {}\n            else if (w->pts_len'),   # (retargeted, step 8 A)
    (PR, "    s->files++, s->bytes += st->st_size, s->once_bytes += st->st_size;", "    s->files++, s->once_bytes += st->st_size;"),
    # ---- hard links counted once too (Michael, 2026-10-26)
    (PR, "    s->files++, s->bytes += st->st_size, s->once_bytes += st->st_size;", "    s->files++, s->bytes += st->st_size;"),
    (PR, "    if (st->st_nlink < 2 || !w->inodes) return;", "    if (!w->inodes) return;"),
    (PR, "    if (st->st_nlink < 2 || !w->inodes) return;", "    if (st->st_nlink < 2) return;"),
    (PR, "    s->linked_names++;\n", ""),
    (PR, "        if (slot->dev == dev && slot->ino == ino) {", "        if (slot->ino == ino) {"),
    (PR, "            s->once_bytes -= st->st_size;\n            return;", "            return;"),
    (PR, "            slot->dev = dev, slot->ino = ino, s->linked_files++;", "            slot->dev = dev, slot->ino = ino;"),
    (PR, "            slot->dev = dev, slot->ino = ino, s->linked_files++;", "            s->linked_files++;"),
    (PR, "    s->linked_files++, s->once_over = true;", "    s->linked_files++;"),
    (PR, "    s->linked_files++, s->once_over = true;", "    s->once_over = true;"),
    (PR, "    if (w->inodes) memset(w->inodes, 0, DU_INODES * sizeof *w->inodes);", ""),
    (PR, "        if (!S_ISDIR(st.st_mode)) {\n            du_file(w, &st, s);", "        if (!S_ISDIR(st.st_mode)) {\n            s->files++, s->bytes += st.st_size, s->once_bytes += st.st_size;"),
    (PR, "        } else {\n            du_file(w, &st, s);", "        } else {\n            s->files++, s->bytes += st.st_size, s->once_bytes += st.st_size;"),
    # ---- the panel's MIME type: its words, and gio's type (Michael, 2026-10-26)
    (M, "    if (slash <= 0 || str_find(str_cut(type, slash + 1, type.len), '/') >= 0 || type.p[0] == '.')", "    if (slash <= 0 || type.p[0] == '.')"),
    (M, "    if (slash <= 0 || str_find(str_cut(type, slash + 1, type.len), '/') >= 0 || type.p[0] == '.')", "    if (slash < 0 || str_find(str_cut(type, slash + 1, type.len), '/') >= 0 || type.p[0] == '.')"),
    (M, "    if (slash <= 0 || str_find(str_cut(type, slash + 1, type.len), '/') >= 0 || type.p[0] == '.')", "    if (slash <= 0 || str_find(str_cut(type, slash + 1, type.len), '/') >= 0)"),
    (M, "    for (isize k = 0; k < nd && !out[0]; k++) {", "    for (isize k = 0; k < nd; k++) {"),
    (M, "        if (!end) continue;", "        if (!end) end = at + strlen(at);"),
    (M, "            if (which >= 0) out[n++] = ents[which].c, at += strlen(ents[which].e);", "            if (which >= 0) out[n++] = ents[which].c, at++;"),
    (M, "        while (at < end && n + 1 < cap) {", "        while (at < end && n < cap) {"),
    (M, "        out[n] = 0;\n    }\n    arena_restore", "    }\n    arena_restore"),
    (P, "    if (!spawn_capture(NULL, argv, buf, sizeof buf, timeout_ms, &late, &err) || late) return false;", "    if (!spawn_capture(NULL, argv, buf, sizeof buf, timeout_ms, &late, &err)) return false;"),
    (P, "    if (!at) return false;\n    at += strlen(key);", "    at += strlen(key);"),
    (P, "    if (n == 0 || n >= cap) return false;", "    if (n == 0) return false;"),
    (PR, "        if (++w->seen % DU_TICK == 0 && w->tick) w->tick(w->ctx, s);", ""),
    (PR, "        if (du_stopped(w)) {\n            go = false;\n            break;\n        }", ""),
    (PR, "        if (du_stopped(w)) return false;\n        struct stat st;", "        struct stat st;"),
    (PR, "        if (lstat(roots[i], &st) != 0) {\n            s->cut = true;", "        if (lstat(roots[i], &st) != 0) {"),
    (PR, "        s->dirs++;\n        w->dev = st.st_dev;", "        w->dev = st.st_dev;"),
    (PR, "        else if (!du_dir(w, fd, s, 1)) return false;", "        else du_dir(w, fd, s, 1);"),
    (PR, "    if (w->tick) w->tick(w->ctx, s);\n    return true;", "    return true;"),
    (PR, "    return (mode_t)(0400 >> k);", "    return (mode_t)(0400 >> (k + 1));"),
    (PR, "    if (!ops_allowed(real, err)) return false;", ""),
    # Not a mutant: dropping the realpath check is inert, as stat on what is left fails the same
    # way; kept so the guard never judges a half-made path.
    (PR, "    if (chmod(real, (st.st_mode & 07000) | (perms & 0777)) != 0)", "    if (chmod(real, perms & 0777) != 0)"),
    (PR, "    *old = st.st_mode & 07777;", "    *old = st.st_mode & 0777;"),
    (N, "            one = op_chmod(it->to, (mode_t)strtol(it->info ? it->info : \"\", NULL, 8), &was, &e);", "            one = true;"),
    # Not a mutant: UNDO_CHMOD in undo_what's list is inert, its record always one item.
    ('ui/panels.c', '    u->panel = UI_PANEL_PROPS, u->prop_at = 0;', '    u->panel = UI_PANEL_PROPS;'),
    ('ui/panels.c', '    case UK_UP: if (row > 0) u->prop_at -= 3; break;', '    case UK_UP: if (row >= 0) u->prop_at -= 3; break;'),
    ('ui/panels.c', '    case UK_DOWN: if (row < 2) u->prop_at += 3; break;', '    case UK_DOWN: u->prop_at += 3; break;'),
    ('ui/panels.c', '        if (u->prop_can) return (UiAction){ UA_CHMOD, u->prop_at, false };', '        return (UiAction){ UA_CHMOD, u->prop_at, false };'),
    ('ui/panels.c', '    if (e.cmd == UK_PROPERTIES) return ui_panel_close(u, UI_PANEL_PROPS), UI_NONE;\n', ''),
    ('ui/handle.c', '    if (on == UI_ON_PROPS && in->prop_box >= 0', '    if (u->panel == UI_PANEL_PROPS && in->prop_box >= 0'),
    ('ui/handle.c', '        return *out = u->prop_can ? (UiAction){ UA_CHMOD, in->prop_box, false } : UI_NONE, true;', '        return *out = (UiAction){ UA_CHMOD, in->prop_box, false }, true;'),
    ('ui/handle.c', '    if (on == UI_ON_PROPS && in->modal == UI_MODAL_OK) return ui_panel_close(u, UI_PANEL_PROPS), *out = UI_NONE, true;\n', ''),
    ('ui/handle.c', '    case UK_PROPERTIES: return *out = ui_props_open(u, UI_PROPS_MARKED), true;', ''),
    ('ui/ui.c', '[UM_PROPERTIES] = { "Properties\\xE2\\x80\\xA6", "properties", .cmd = UK_PROPERTIES, .runs = true }', '[UM_PROPERTIES] = { "Properties\\xE2\\x80\\xA6", "properties", .cmd = UK_PROPERTIES }'),
    ('ui/cursor.c', "|| in->menu_away || in->ctx_row >= 0 || in->ctx_here || in->ctx_side >= 0 || in->ctx_crumb >= 0 || in->ctx_tab >= 0 || in->prop_box >= 0 ||\n", "|| in->menu_away || in->ctx_row >= 0 || in->ctx_here || in->ctx_side >= 0 || in->ctx_crumb >= 0 || in->ctx_tab >= 0 ||\n"),
    ('ui/ui.c', '.menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box = -1, .pick_row = -1, .pick_drow = -1, .bmp_row = -1, .bmp_drow = -1,', '.menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box = 0, .pick_row = -1, .pick_drow = -1, .bmp_row = -1, .bmp_drow = -1,'),
    ('ui/ui.c', '.menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box = -1, .pick_row = -1, .pick_drow = -1, .bmp_row = -1, .bmp_drow = -1,', '.menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box = -1, .pick_row = 0, .pick_drow = -1, .bmp_row = -1, .bmp_drow = -1,'),
    ('ui/ui.c', '.menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box = -1, .pick_row = -1, .pick_drow = -1, .bmp_row = -1, .bmp_drow = -1,', '.menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box = -1, .pick_row = -1, .pick_drow = 0, .bmp_row = -1, .bmp_drow = -1,'),
    # ---- the files' cursor round the ends (Michael, 2026-10-25)
    ('ui/cursor.c', "        l->sel = by < 0 ? (l->sel == 0 ? l->n - 1 : l->sel - 1) : (l->sel == l->n - 1 ? 0 : l->sel + 1);", "        l->sel = by < 0 ? (l->sel == 0 ? 0 : l->sel - 1) : (l->sel == l->n - 1 ? 0 : l->sel + 1);"),
    ('ui/cursor.c', "        l->sel = by < 0 ? (l->sel == 0 ? l->n - 1 : l->sel - 1) : (l->sel == l->n - 1 ? 0 : l->sel + 1);", "        l->sel = by < 0 ? (l->sel == 0 ? l->n - 1 : l->sel - 1) : (l->sel == l->n - 1 ? l->sel : l->sel + 1);"),
    ('ui/cursor.c', "    } else if (by < 0 && l->sel < c) {   // the top row", "    } else if (by < 0 && l->sel < 0) {   // the top row"),
    ('ui/cursor.c', "        l->sel = last_row + col < l->n ? last_row + col : last_row + col - c;", "        l->sel = last_row + col;"),
    ('ui/cursor.c', "        l->sel = last_row + col < l->n ? last_row + col : last_row + col - c;", "        l->sel = l->n - 1;"),
    ('ui/cursor.c', "    } else if (by > 0 && l->sel >= last_row) {   // the bottom row: down to the top\n        l->sel = col;", "    } else if (by > 0 && l->sel >= last_row) {   // the bottom row: down to the top\n        l->sel = 0;"),
    ('ui/cursor.c', "    } else if (by > 0 && l->sel >= last_row) {", "    } else if (by > 0 && l->sel > last_row) {"),
    ('ui/cursor.c', "    if (l->n == 0) return;\n    isize c = rows", "    isize c = rows"),
    ('ui/handle.c', '        if (m == l && !shifted) ui_step_round(l, -1, false);', '        if (false) ui_step_round(l, -1, false);'),
    ('ui/handle.c', '        if (m == l && !shifted) ui_step_round(l, 1, false);', '        if (true) ui_step_round(l, 1, false);'),
    ('ui/handle.c', '        else ui_step_round(l, key == UK_UP ? -1 : 1, true);', '        else ui_move_rows(l, key == UK_UP ? -1 : 1);'),
    ('ui/handle.c', '        else ui_step_round(l, key == UK_LEFT ? -1 : 1, false);', '        else ui_move(l, key == UK_LEFT ? -1 : 1);'),
    ('ui/helpers.c', "    if (n > 1 && strncmp(path, home, n) == 0 && (path[n] == '/' || path[n] == 0)) snprintf(buf, cap, \"~%s\", path + n);", "    if (n > 1 && strncmp(path, home, n) == 0) snprintf(buf, cap, \"~%s\", path + n);"),
    ('ui/helpers.c', "    if (n > 1 && strncmp(path, home, n) == 0 && (path[n] == '/' || path[n] == 0)) snprintf(buf, cap, \"~%s\", path + n);", "    if (n > 0 && strncmp(path, home, n) == 0 && (path[n] == '/' || path[n] == 0)) snprintf(buf, cap, \"~%s\", path + n);"),
    (N, "(unsigned long long)st.st_dev != dev || (unsigned long long)st.st_ino != ino)", "(unsigned long long)st.st_ino != ino)"),
    (N, "(unsigned long long)st.st_dev != dev || (unsigned long long)st.st_ino != ino)", "(unsigned long long)st.st_dev != dev)"),
    # Not a mutant: `!= 3` as `< 1` in UNDO_CHMOD's check is inert, a short record leaving dev 0,
    # which no file has.
    (N, "        if (u->kind == UNDO_CHMOD) {\n            struct stat st;", "        if (false) {\n            struct stat st;"),
]
# ---- F7.3: the matcher, find's toggles, the app list, Open With's keys (Michael, 2026-10-26)
MUTANTS += [
    ('ui/match.c', "    if (m->re_on) regfree(&m->re);\n", ""),
    ('ui/match.c', "    m->len = (int)(text.len < UI_FIND_MAX ? text.len : UI_FIND_MAX);", "    m->len = (int)text.len;"),
    ('ui/match.c', "    if (!o.regex || m->len == 0) return;", "    if (!o.regex) return;"),
    ('ui/match.c', "REG_EXTENDED | (o.cased ? 0 : REG_ICASE));", "REG_EXTENDED);"),
    ('ui/match.c', "REG_EXTENDED | (o.cased ? 0 : REG_ICASE));", "REG_ICASE);"),
    ('ui/match.c', "    else m->bad = true, regerror(rc, NULL, m->err, sizeof m->err);", "    else m->bad = true;"),
    ('ui/match.c', "|| (c >= '0' && c <= '9') || c == '_' || c >= 0x80;", "|| (c >= '0' && c <= '9') || c == '_';"),
    ('ui/match.c', "|| (c >= '0' && c <= '9') || c == '_' || c >= 0x80;", "|| (c >= '0' && c <= '9') || c >= 0x80;"),
    ('ui/match.c', "|| (c >= '0' && c <= '9') || c == '_' || c >= 0x80;", "|| c == '_' || c >= 0x80;"),
    ('ui/match.c', "    return (from == 0 || !ui_word_byte((unsigned char)hay.p[from - 1])) &&", "    return (from == 0) &&"),
    ('ui/match.c', "&& (to >= hay.len || !ui_word_byte((unsigned char)hay.p[to]));", "&& (to >= hay.len);"),
    ('ui/match.c', '    if (m->len == 0) return *from = *to = 0, true;\n    if (m->o.regex) {', '    if (m->o.regex) {'),
    ('ui/match.c', "        if (!m->re_on) return false;\n        locale_t was = ui_utf8_on();\n        bool hit", "        locale_t was = ui_utf8_on();\n        bool hit"),
    ('ui/match.c', "    if (regexec(&m->re, hay.p ? hay.p : \"\", 1, &all, REG_STARTEND) != 0) return false;", ""),
    ('ui/match.c', '    if (!m->o.word) return *at = all.rm_so, *end = all.rm_eo, true;', ''),
    ('ui/match.c', "        if (from > 0 && ui_word_byte((unsigned char)hay.p[from - 1])) continue;", ""),
    ('ui/match.c', "            if (to < hay.len && ui_word_byte((unsigned char)hay.p[to])) continue;", ""),
    ('ui/match.c', "            int flags = REG_STARTEND | (from > 0 ? REG_NOTBOL : 0) | (to < hay.len ? REG_NOTEOL : 0);", "            int flags = REG_STARTEND | (to < hay.len ? REG_NOTEOL : 0);"),
    ('ui/match.c', "            int flags = REG_STARTEND | (from > 0 ? REG_NOTBOL : 0) | (to < hay.len ? REG_NOTEOL : 0);", "            int flags = REG_STARTEND | (from > 0 ? REG_NOTBOL : 0);"),
    ('ui/match.c', '== 0 && pm.rm_so == 0 && pm.rm_eo == to - from) return *at = from, *end = to, true;', '== 0 && pm.rm_so == 0) return *at = from, *end = to, true;'),
    ('ui/match.c', '== 0 && pm.rm_so == 0 && pm.rm_eo == to - from) return *at = from, *end = to, true;', '== 0 && pm.rm_eo == to - from) return *at = from, *end = to, true;'),
    ('ui/match.c', "        for (isize to = hay.len; to >= from; to--) {", "        for (isize to = hay.len; to > from; to--) {"),
    ('ui/match.c', '    if (which == UI_TOGGLE_CASE) o.cased = !o.cased;', ''),
    ('ui/match.c', '    if (which == UI_TOGGLE_WORD) o.word = !o.word;', ''),
    ('ui/match.c', '    if (which == UI_TOGGLE_REGEX) o.regex = !o.regex;', ''),
    ('ui/match.c', "e.ch == 'w' ? UI_TOGGLE_WORD : e.ch == 'r' ? UI_TOGGLE_REGEX : 0;", "e.ch == 'w' ? UI_TOGGLE_WORD : UI_TOGGLE_REGEX;"),
    ('ui/match.c', "    if (e.key != UK_MENU) return 0;\n", ""),
    ('ui/find.c', "    if (l->n == 0 || m->len == 0) return -1;", "    if (l->n == 0) return -1;"),
    ('ui/find.c', "    ui_matcher_set(&u->find_m, (Str){ u->last, u->last_len }, u->find_opts);\n", ""),
    ('ui/handle.c', "        u->find_opts = ui_toggle(u->find_opts, ui_toggle_key(e));\n        ui_find_update(u, names);", "        u->find_opts = ui_toggle(u->find_opts, ui_toggle_key(e));"),
    ('ui/panels.c', '    p->in_list = false, p->fresh = true, UI_TEXT_CLEAR(p->filter);', '    p->in_list = false, p->fresh = true;'),
    ('ui/panels.c', '    p->list = (UiList){ .rows = p->list.rows };', ''),
    ('ui/panels.c', '    ui_matcher_set(&p->m, STR_EMPTY, (UiMatchOpts){ 0 });\n', ''),
    ('ui/panels.c', '        if (ui_match(&p->m, names[i])) idx[k++] = i;', '        idx[k++] = i;'),
    ('ui/panels.c', '    p->list.n = k, p->list.sel = 0, p->list.top = 0;', '    p->list.n = k;'),



    ('ui/panels.c', '    else if (d == UI_PICK_CHOOSE && k.pick->list.n > 0) {', '    else if (d == UI_PICK_CHOOSE) {'),   # (retargeted, step E)
    ('ui/panels.c', '    case UK_TAB: p->in_list = !p->in_list; return UI_PICK_NONE;', ''),
    ('ui/panels.c', '        if (e.key == UK_UP && l->sel == 0) p->in_list = false;', '        if (0) p->in_list = false;'),
    ('ui/cursor.c', '    case UK_HOME: ui_move(l, -l->n); return true;\n', ''),
    ('ui/cursor.c', '    case UK_PAGEUP: ui_move(l, -page); return true;\n', ''),
    ('ui/panels.c', '            if (e.key == UK_NONE && ui_utf8(e.ch, (char[4]){ 0 }) == 0) break;\n            p->in_list = false, p->filter.caret', '            p->in_list = false, p->filter.caret'),
    ('ui/panels.c', '            p->in_list = false, p->filter.caret = p->filter.anchor = p->filter.len;\n', '            p->in_list = false;\n'),


    ('ui/panels.c', '    if (!h && e.key == UK_DOWN && l->n > 0) p->in_list = true;', '    if (!h && e.key == UK_DOWN) p->in_list = true;'),
    ('ui/panels.c', '    if (h == UI_LINE_CHANGED) return UI_PICK_FILTER;\n    if (!h', '    if (false) return UI_PICK_FILTER;\n    if (!h'),
    ('ui/menus.c', '    case UM_OPEN_WITH: return ui_ow_open(u);\n', ''),
    ('ui/handle.c', '    if (k.pick && k.pick->fresh && k.names) ui_picker_filter(u, u->panel, in);', ''),   # (retargeted, step E)
    ('ui/panels.c', '    if (in->pick_field) p->in_list = false;', ''),
    ('ui/panels.c', '        if (in->pick_drow >= 0) return UI_PICK_CHOOSE;', ''),
    ('ui/panels.c', '    if (in->modal == UI_MODAL_YES) return UI_PICK_CHOOSE;', ''),
    ('ui/panels.c', '    if (in->modal == UI_MODAL_NO) return UI_PICK_CLOSE;', ''),
    ('ui/panels.c', '    if (in->panel_wheel) ui_scroll(&p->list, in->panel_wheel);', ''),
    ('ui/handle.c', '    if (on == UI_ON_OW) {\n        if (in->toggle == UI_TOGGLE_ALWAYS) {', '    if (u->panel == UI_PANEL_OW) {\n        if (in->toggle == UI_TOGGLE_ALWAYS) {'),
    ('ui/handle.c', '        if (files) return *out = (UiAction){ UA_OPEN, l->sel, u->marked > 0 }, true;', '        if (files) return *out = (UiAction){ UA_OPEN, l->sel, false }, true;'),
    ('ui/cursor.c', "           in->pick_row >= 0 || in->pick_drow >= 0 || in->pick_field || in->toggle > 0 || in->find_x ||", "           in->pick_row >= 0 || in->pick_drow >= 0 || in->find_x ||"),
    (AP, "        if (e->d_name[0] == '.') continue;\n        char *path", "        char *path"),
    (AP, "            if (depth < APPS_DEPTH_MAX) apps_scan", "            if (true) apps_scan"),
    (AP, "        if (!S_ISREG(st.st_mode) || !str_ends_with(id, S(\".desktop\"))) continue;", "        if (!S_ISREG(st.st_mode)) continue;"),
    (AP, "        if (dup) continue;\n", ""),
    (AP, "        if (!apps_usable(text)) continue;", ""),
    (AP, "            .id = id, .path = str_from_c(path), .name = name.len ? name : id,", "            .id = id, .path = str_from_c(path), .name = name,"),
    (AP, "            .nodisplay = str_eq(ini_get(text, S(\"Desktop Entry\"), S(\"NoDisplay\")), S(\"true\")), .section = AS_OTHER,", "            .nodisplay = false, .section = AS_OTHER,"),
    (AP, "    if (x->section != y->section) return x->section < y->section ? -1 : 1;", ""),
    (AP, "        int cx = fold((unsigned char)x->name.p[i]), cy = fold((unsigned char)y->name.p[i]);", "        int cx = (unsigned char)x->name.p[i], cy = (unsigned char)y->name.p[i];"),
    (AP, "        for (isize j = 0; j < k1 && nn < COUNT(names); j++) names[nn++] = one[j];", "        names[nn++] = tt;"),
    (P, "    for (isize i = 0; i < n && i < SPAWN_FILES_MAX; i++) cmd[k++] = paths[i];", "    for (isize i = 0; i < n && i < SPAWN_FILES_MAX; i++) cmd[k] = paths[i];"),
]
# ---- F7.3 after Michael's try (2026-10-26): Related apart from Recommended, regex in C.UTF-8
MUTANTS += [
    ('ui/match.c', "    locale_t was = ui_utf8_on();\n    int rc = regcomp", "    locale_t was = (locale_t)0;\n    int rc = regcomp"),
    ('ui/match.c', "        locale_t was = ui_utf8_on();\n        bool hit", "        locale_t was = (locale_t)0;\n        bool hit"),
    ('ui/match.c', "    if (!tried) tried = true, loc = newlocale(LC_CTYPE_MASK, \"C.UTF-8\", (locale_t)0);", "    if (!tried) tried = true, loc = (locale_t)0;"),
    (AP, "        if (t == 0) nself = nn;   // the type and its aliases; the parents' after them", "        if (t == 0) nself = 1;"),
    (AP, "            if (apps_listed(p->mimes, names[j])) own |= j < nself, related = true;", "            if (apps_listed(p->mimes, names[j])) own |= true, related = true;"),
    (AP, "            if (apps_listed(p->mimes, names[j])) own |= j < nself, related = true;", "            if (apps_listed(p->mimes, names[j])) own |= j < nself;"),
    (AP, "        own &= !apps_listed(removed[0], p->id), related &= !apps_listed(removed[1], p->id);", "        own &= !apps_listed(removed[0], p->id);"),
    (AP, "        own &= !apps_listed(removed[0], p->id), related &= !apps_listed(removed[1], p->id);", "        related &= !apps_listed(removed[1], p->id);"),
    (AP, "                   : related && !p->nodisplay                     ? AS_RELATED", "                   : related                                      ? AS_RELATED"),
    (AP, "        bool own = apps_listed(listed[0], p->id), related = apps_listed(listed[1], p->id);", "        bool own = false, related = apps_listed(listed[1], p->id);"),
    (AP, "        bool own = apps_listed(listed[0], p->id), related = apps_listed(listed[1], p->id);", "        bool own = apps_listed(listed[0], p->id), related = false;"),
    (AP, "        isize upto = sc ? nn : nself;", "        isize upto = nn;"),
    (AP, "                   : p->nodisplay                                 ? AS_NONE", "                   : false                                        ? AS_NONE"),
    (AP, "            removed[sc] = apps_removed(a, text[k], names, upto, removed[sc]);", ""),
]
# ---- the apps' icons apart, SVG among them (Michael, 2026-10-26)
MUTANTS += [
    (I, "    if (apps) return str_eq(context, S(\"Apps\")) || str_eq(context, S(\"Applications\"));", ""),
    (I, "    if (apps) return str_eq(context, S(\"Apps\")) || str_eq(context, S(\"Applications\"));", "    if (apps) return str_eq(context, S(\"Apps\"));"),
    ('fs/icons.c', 'is_svg = (svg || str_ends_with(s, S("-symbolic.svg"))) && str_ends_with(s, S(".svg"));', 'is_svg = str_ends_with(s, S(".svg"));'),   # (retargeted, step 8 A)
    (I, "        if (!strmap_get(m, names[i], &had) || kind[i] < had) strmap_put(m, names[i], kind[i]);", "        strmap_put(m, names[i], kind[i]);"),
    (I, "kind == 2 ? \"svg\" : \"png\");", "\"png\");"),
    (I, "    icon_load_ctx(a, db, theme, true);", "    icon_load_ctx(a, db, theme, false);"),
    (I, "                if (icon_scan(a, d.path, &d.names, apps)) t->dirs[t->ndirs++] = d;", "                if (icon_scan(a, d.path, &d.names, false)) t->dirs[t->ndirs++] = d;"),
]
# ---- F7.3.1: Set as default (Michael, 2026-10-26)
MUTANTS += [
    ('fs/apps.c', '    for (isize k = 0; k < nl && !found && strcmp(lists[k], mine) != 0; k++) {', '    for (isize k = 0; k < nl && !found; k++) {'),
    ('fs/apps.c', '        for (isize j = 0; j < nn && !found; j++)\n            if (ini_get(text, S("Default Applications"), names[j]).len)', '        for (isize j = 0; j < 1 && !found; j++)\n            if (ini_get(text, S("Default Applications"), names[j]).len)'),
    ('fs/apps.c', 'if (ini_get(text, S("Default Applications"), names[j]).len) found = true', 'if (ini_get(text, S("Added Associations"), names[j]).len) found = true'),
    ('fs/apps.c', '    if (semi >= 0) v = str_cut(v, 0, semi);', ''),
    ('fs/apps.c', '    snprintf(out, cap, "%.*s", STR_ARG(str_trim(v)));', '    snprintf(out, cap, "%.*s", STR_ARG(v));'),
    ('fs/apps.c', '    if (!ops_allowed(list, err)) return false;\n    char t[256]', '    char t[256]'),
    ('fs/apps.c', '    if (late || strcmp(now, app) != 0) {', '    if (late) {'),
    ('fs/apps.c', '        return err_set(err, "%s", out[0] ? out : "gio didn\'t set it"), false;', '        return err_set(err, "gio didn\'t set it"), false;'),
    ('ui/panels.c', '    if (u->ow_def_can) u->ow_def = !u->ow_def;', '    u->ow_def = !u->ow_def;'),
    ('ui/panels.c', '        bool def = p == UI_PANEL_OW && u->ow_def && u->ow_def_can;', '        bool def = p == UI_PANEL_OW && u->ow_def;'),   # (retargeted, step E)
    ('ui/panels.c', '    u->ow_def = false;\n    ui_picker_open(u, UI_PANEL_OW);', '    ui_picker_open(u, UI_PANEL_OW);'),   # (retargeted, step E)
    ('ui/handle.c', '        if (in->toggle == UI_TOGGLE_ALWAYS) {\n            ui_ow_def_flip(u);', '        if (in->toggle == UI_TOGGLE_ALWAYS) {\n            u->ow_def = !u->ow_def;'),
]
# ---- the find bar (Michael, 2026-10-26)
MUTANTS += [
    ('ui/find.c', 'static void ui_find_keep(Ui *u)\n{\n    ui_unfocus(u, UI_FOCUS_FIND);\n', 'static void ui_find_keep(Ui *u)\n{\n'),
    ('ui/handle.c', "        if (u->find_select) ui_find_select(u, names);\n        else ui_find_leave(u);", "        if (u->find_select) ui_find_select(u, names);\n        else ui_find_close(u);"),
    ('ui/handle.c', '    case UK_UP: case UK_DOWN: case UK_SEL_UP: case UK_SEL_DOWN: ui_find_step(u, names, e.key == UK_UP || e.key == UK_SEL_UP ? -1 : 1); return true;', '    case UK_UP: case UK_DOWN: case UK_SEL_UP: case UK_SEL_DOWN: ui_find_step(u, names, e.key == UK_UP ? -1 : 1); return true;'),
    ('ui/find.c', "    if (i >= 0) u->list.sel = i, ui_show_sel(&u->list);\n}\n\n// How many", "}\n\n// How many"),
    ('ui/handle.c', '        if (in->job_n > 0) u->focus = UI_FOCUS_JOBS, u->job_at = 0;\n        return true;', '        return true;'),
    ('ui/handle.c', '    if (e.cmd == UK_JOBS) {\n        ui_find_leave(u);', '    if (e.cmd == UK_JOBS) {\n        ui_find_close(u);'),
    ('ui/handle.c', '        if (u->finding) u->focus = UI_FOCUS_FIND;', '        if (false) u->focus = UI_FOCUS_FIND;'),
    ('ui/handle.c', '        if (u->focus == UI_FOCUS_FIND && u->finding && ui_find_event(u, names, e, in)) continue;', '        if (u->finding && ui_find_event(u, names, e, in)) continue;'),
    # (the magnifier's find bar toggle: the search's since F9, 2026-10-28)
    ('ui/handle.c', '        } else ui_find_close(u);', '        } else ui_find_leave(u);'),
    ('ui/handle.c', '        } else u->focus = UI_FIELD_FOCUS[in->field];', '        } else {}'),
    # Not a mutant: ui_find_open's `jobs_focus = false` is inert, every way there (a key, a click)
    # has taken the keys from the job rows already.
]
# ---- the review's F7.3.1 notes: plain text folds past ASCII, an alias set by its canonical name
MUTANTS += [
    ('ui/match.c', "    return l ? (u32)towlower_l((wint_t)cp, l) : cp;", "    return cp;"),
    ('ui/match.c', "        if (ui_fold_cp(x) != ui_fold_cp(y)) return false;", "        if (x != y) return false;"),
    ('ui/match.c', "        if (a >= hay.len) return false;\n        u32 x", "        u32 x"),
    ('ui/match.c', '            if ((c & 0xC0) == 0x80) continue;   // a match starts at a character', ''),
    ('ui/match.c', '            i = p - hay.p, end = i + m->len;', '            end = i + m->len;'),
    ('ui/match.c', '        if (!m->o.word || ui_whole(hay, i, end)) return *from = i, *to = end, true;', '        if (!m->o.word || ui_whole(hay, i, i + m->len)) return *from = i, *to = end, true;'),
    ('fs/apps.c', '    apps_names(a, &type, names, APPS_TYPES_MAX);\n    char list[PATH_MAX];', '    char list[PATH_MAX];'),
    ('ui/match.c', '    for (isize i = 0; i < hay.len; i++) {\n        isize end = i;\n        if (m->o.cased) {', '    for (isize i = 1; i < hay.len; i++) {\n        isize end = i;\n        if (m->o.cased) {'),
    ('ui/match.c', '            i = p - hay.p, end = i + m->len;', '            i = p - hay.p;'),
]
MUTANTS += [
    ('ui/find.c', "    return ui_clamp(u->find_from, 0, u->list.n > 0 ? u->list.n - 1 : 0);", "    return u->find_from;"),
    ('ui/find.c', "u->list.sel = i >= 0 ? i : ui_find_back(u);", "u->list.sel = i >= 0 ? i : u->find_from;"),
]
# ---- the find bar's count and chevrons (Michael, 2026-10-26)
MUTANTS += [
    ('ui/find.c', '    if (!u->finding || u->find.len == 0 || u->find_m.len == 0) return 0;', '    if (u->find.len == 0 || u->find_m.len == 0) return 0;'),
    ('ui/find.c', '    if (!u->finding || u->find.len == 0 || u->find_m.len == 0) return 0;', '    if (!u->finding || u->find_m.len == 0) return 0;'),
    ('ui/find.c', "        if (i == u->list.sel) *at = n;", "        *at = n;"),
    ('ui/find.c', "        n++;\n        if (i == u->list.sel)", "        if (i == u->list.sel)"),
    ('ui/handle.c', "            if (ui_find_count(u, names, &at) >= 2) ui_find_step(", "            if (ui_find_count(u, names, &at) >= 1) ui_find_step("),
    ('ui/handle.c', "ui_find_step(u, names, in->find_step > 0 ? 1 : -1);", "ui_find_step(u, names, 1);"),
    ('ui/cursor.c', 'in->find_x || in->find_step || in->field >= 0 ||', 'in->find_x || in->field >= 0 ||'),
    ('ui/handle.c', 'u->finding && (in->find_x || in->find_step)) {', 'u->finding && in->find_x) {'),
    # the review's LOWs: a Shift move doesn't wrap; an empty bar finds nothing
    ('ui/handle.c', "        if (shifted) ui_move(l, key == UK_LEFT ? -1 : 1);   // a Shift move doesn't wrap\n        else ", '        '),
    ('ui/handle.c', '        if (shifted) ui_move_rows(l, key == UK_UP ? -1 : 1);\n        else ', '        '),
    ('ui/handle.c', '        if (m == l && !shifted) ui_step_round(l, -1, false);', '        if (m == l) ui_step_round(l, -1, false);'),
    ('ui/handle.c', '        if (m == l && !shifted) ui_step_round(l, 1, false);', '        if (m == l) ui_step_round(l, 1, false);'),
    ('ui/find.c', "    ui_matcher_set(&u->find_m, STR_EMPTY, u->find_opts);   // Up and Down in an empty bar find nothing (a review)\n", ""),
]
# ---- Shift and a move select (Michael, 2026-10-26)
MUTANTS += [
    ('ui/edit.c', '    if (from < 0) {\n        if (u->list.sel != was)', '    if (from <= 0) {\n        if (u->list.sel != was)'),
    ('ui/edit.c', '        if (u->list.sel != was) u->anchor = -1;\n', ''),
    ('ui/edit.c', '        if (u->list.sel != was) u->anchor = -1;', '        u->anchor = -1;'),
    ('ui/edit.c', '    } else if (u->shift_add) {\n        ui_mark_range(u, from, u->list.sel);', '    } else if (false) {\n        ui_mark_range(u, from, u->list.sel);'),
    ('ui/edit.c', '        ui_range(u, from, u->list.sel, false);', '        ui_mark_range(u, from, u->list.sel);'),
    ('ui/edit.c', '    u->mark = mark, u->marked = 0, u->anchor = -1;', '    u->mark = mark, u->marked = 0;'),
    ('ui/edit.c', '    if (u->anchor < 0 || u->anchor >= u->list.n || at != u->range_end) u->anchor = at;', '    if (u->anchor >= u->list.n || at != u->range_end) u->anchor = at;'),
    ('ui/edit.c', '    if (u->anchor < 0 || u->anchor >= u->list.n || at != u->range_end) u->anchor = at;', '    if (u->anchor < 0 || at != u->range_end) u->anchor = at;'),
    ('ui/edit.c', '    if (u->anchor < 0 || u->anchor >= u->list.n || at != u->range_end) u->anchor = at;', '    if (u->anchor < 0 || u->anchor >= u->list.n) u->anchor = at;'),
    ('ui/edit.c', '    if (u->anchor < 0 || u->anchor >= u->list.n || at != u->range_end) u->anchor = at;', '    if (u->anchor < 0 || u->anchor > u->list.n || at != u->range_end) u->anchor = at;'),
    ('ui/edit.c', '    if (add) ui_mark_range(u, a, to);\n    else ui_mark_only(u, a, to);', '    ui_mark_only(u, a, to);'),
    ('ui/edit.c', '    if (add) ui_mark_range(u, a, to);\n    else ui_mark_only(u, a, to);', '    ui_mark_range(u, a, to);'),
    ('ui/edit.c', '    u->range_end = to;\n}', '}'),
    ('ui/edit.c', '    ui_mark_clear(u);\n    ui_mark_range(u, from, to);', '    ui_mark_range(u, from, to);'),
    ('fs/conf.c', '        if (str_eq(v, S("range")) || str_eq(v, S("add"))) return *(bool *)f = str_eq(v, S("add")), true;', '        if (str_eq(v, S("range")) || str_eq(v, S("add"))) return *(bool *)f = true, true;'),
    ('fs/conf.c', '        if (str_eq(v, S("range")) || str_eq(v, S("add"))) return *(bool *)f = str_eq(v, S("add")), true;', '        if (str_eq(v, S("add"))) return *(bool *)f = str_eq(v, S("add")), true;'),
    ('fs/conf.c', '        if (str_eq(v, S("range")) || str_eq(v, S("add"))) return *(bool *)f = str_eq(v, S("add")), true;', '        return *(bool *)f = str_eq(v, S("add")), true;'),
    ('ui/handle.c', 'k < in->nev; k++, extend = ui_extend(u, extend, was)) {', 'k < in->nev; k++) {'),
    ('ui/handle.c', '        UiEvent e = k < 0 ? (UiEvent){ UK_CMD, 0, u->menu_cmd } : in->ev[k];\n        was = l->sel;\n', '        UiEvent e = k < 0 ? (UiEvent){ UK_CMD, 0, u->menu_cmd } : in->ev[k];\n'),
    (KY, "{ T('H'), UK_SEL_LEFT }, ", ''),
    (KY, "{ T('K'), UK_SEL_UP }", "{ T('K'), UK_RIGHT }"),
    ('ui/handle.c', '    if (shifted && u->focus != UI_FOCUS_SIDE) *extend = l->sel;', '    if (shifted) *extend = l->sel;'),
]
# ---- FileManager1 (2026-10-26): the URIs to windows
FM1 = 'fs/fm1.c'
MUTANTS += [
    (FM1, "    return (Str){ p, s && s > p ? s - p : 1 };", "    return (Str){ p, s ? s - p : 1 };"),
    (FM1, "        Str key = c == FM1_FOLDERS ? str_from_c(path) : fm1_parent(path);", "        Str key = fm1_parent(path);"),
    (FM1, "            seen = str_eq(c == FM1_FOLDERS ? str_from_c(out->path[k]) : fm1_parent(out->path[k]), key);", "            seen = str_eq(str_from_c(out->path[k]), key);"),
    (FM1, "        if (seen) continue;", ""),
    (FM1, "        if (out->n == FM1_MAX) {", "        if (out->n == FM1_MAX + 1) {"),
    (FM1, "            out->skipped++;\n            continue;\n        }\n        memcpy", "            continue;\n        }\n        memcpy"),
    (FM1, "    out->n = 0, out->skipped = 0;", "    out->n = 0;"),
]
MUTANTS += [
    (FM1, "        for (size_t k = strlen(path); k > 1 && path[k - 1] == '/'; k--) path[k - 1] = 0;   // a/b/ is a/b\n", ""),
]
# ---- F7.4: the Edit, View and Go menus
MUTANTS += [
    ('ui/menus.c', '    bool files = u->list.n > 0, here = files && u->focus != UI_FOCUS_SIDE;', '    bool files = u->list.n > 0, here = files;'),
    ('ui/menus.c', "    case UM_UNDO: return u->can_undo;", "    case UM_UNDO: return true;"),
    ('ui/menus.c', "    case UM_SELECT_ALL: case UM_SELECT_MATCH: case UM_INVERT: return files;", "    case UM_SELECT_ALL: case UM_SELECT_MATCH: case UM_INVERT: return true;"),
    ('ui/menus.c', '    case UM_TRASH: return here && ui_allows(u, UA_TRASH);', '    case UM_TRASH: return true;'),
    ('ui/menus.c', "    case UM_RESTORE: return here;", "    case UM_RESTORE: return true;"),
    ('ui/menus.c', "    case UM_ZOOM_IN: return u->grid && ui_zoom_level(ui_zoom_px(u)) < UI_ZOOMS - 1;", "    case UM_ZOOM_IN: return u->grid;"),
    ('ui/menus.c', "    case UM_ZOOM_OUT: return u->grid && ui_zoom_level(ui_zoom_px(u)) > 0;", "    case UM_ZOOM_OUT: return u->grid;"),
    ('ui/menus.c', "    case UM_ZOOM_RESET: return u->grid && ui_zoom_px(u) != UI_ZOOM_DEFAULT;", "    case UM_ZOOM_RESET: return u->grid;"),
    ('ui/menus.c', "    case UM_ZOOM_RESET: return u->grid && ui_zoom_px(u) != UI_ZOOM_DEFAULT;", "    case UM_ZOOM_RESET: return ui_zoom_px(u) != UI_ZOOM_DEFAULT;"),
    ('ui/menus.c', "    case UM_PARENT: return u->can_up;", "    case UM_PARENT: return true;"),
    ('ui/menus.c', "    case UM_BACK: return u->can_back;", "    case UM_BACK: return u->can_forward;"),
    ('ui/menus.c', "    case UM_FORWARD: return u->can_forward;", "    case UM_FORWARD: return u->can_back;"),
    ('ui/menus.c', "    case UM_SIDE_TREE: return u->side_mode == SIDE_TREE;", "    case UM_SIDE_TREE: return u->side_mode == SIDE_PLACES;"),
    ('ui/menus.c', "    case UM_HIDDEN: return u->shows_hidden;", "    case UM_HIDDEN: return 0;"),
    ('ui/menus.c', "    case UM_SORT_REVERSE: return u->sort.reverse;", "    case UM_SORT_REVERSE: return 0;"),
    ('ui/menus.c', "    case UM_VIEW_LIST: return !u->grid;", "    case UM_VIEW_LIST: return u->grid;"),
    ('ui/menus.c', '        u->menu_cmd = UI_MENU_ITEM[it].cmd;\n        return UI_NONE;', '        return UI_NONE;'),
    ('ui/menus.c', '        u->sort.key = it == UM_SORT_NAME ? SORT_NAME : it == UM_SORT_SIZE ? SORT_SIZE : SORT_MTIME;', '        u->sort.key = it == UM_SORT_NAME ? SORT_NAME : SORT_MTIME;'),
    ('ui/menus.c', '    case UM_SIDE_NONE: return ui_side_mode(u, SIDE_HIDDEN);', '    case UM_SIDE_NONE: return ui_side_mode(u, SIDE_TREE);'),
    ('ui/handle.c', '{ UK_TRASH_VIEW, UA_TRASH_VIEW, 0, false, false },', '{ UK_NONE, UA_TRASH_VIEW, 0, false, false },'),
    ('ui/handle.c', '        if (files && shift_enter) return', '        if (files && false) return'),
    ('ui/handle.c', '        bool shift_enter = e.cmd == UK_SHIFT_ENTER;', '        bool shift_enter = false;'),
    # Not a mutant: the `continue` after a menu's own key is inert, the empty menu_key it falls into
    # ({UK_NONE, 0}) is dropped by the characters' switch.
    ('ui/handle.c', '                e = (UiEvent){ UK_CMD, 0, u->menu_cmd }, u->menu_cmd = UK_NONE;   // an entry picked runs its command, here', '                e = (UiEvent){ UK_CMD, 0, u->menu_cmd };'),
    ('ui/handle.c', '        if (a.kind != UA_NONE || !u->menu_cmd) return *out = a, true;', '        return *out = a, true;'),
    ('ui/handle.c', '    for (int k = u->menu_cmd ? -1 : 0; k < in->nev;', '    for (int k = 0; k < in->nev;'),
    ('ui/handle.c', '        if (k < 0) u->menu_cmd = UK_NONE;', ''),
    ('ui/handle.c', '        for (isize i = 0; i < l->n; i++) ui_mark(u, i, !ui_marked(u, i));', '        for (isize i = 0; i < l->n; i++) ui_mark(u, i, true);'),
    ('ui/handle.c', '{ UK_RELOAD, UA_RELOAD, 0, false, false },', '{ UK_NONE, UA_RELOAD, 0, false, false },'),
    ('ui/handle.c', '    { UK_HOMEDIR, UA_HOMEDIR, 0, false, false },', '    { UK_NONE, UA_HOMEDIR, 0, false, false },'),
]
# ---- F7.4: Select Items Matching
MUTANTS += [
    ('ui/find.c', '    u->finding = false, u->find_miss = false, u->find_select = false;\n', '    u->finding = false, u->find_miss = false;\n'),
    ('ui/find.c', "    if (u->find_select) {\n        if (u->finding) ui_find_close(u);\n        return;\n    }\n", ""),
    ('ui/find.c', "        if (u->finding) ui_find_close(u);\n        return;", "        return;"),
    ('ui/find.c', '    if (u->find.len > 0) {\n        ui_mark_clear(u);', '    if (true) {\n        ui_mark_clear(u);'),
    ('ui/find.c', "        ui_mark_clear(u);\n        for (isize i = 0; i < u->list.n; i++)\n            if (ui_match(", "        for (isize i = 0; i < u->list.n; i++)\n            if (ui_match("),
    ('ui/find.c', "            if (ui_match(&u->find_m, names.at(names.ctx, i))) ui_mark(u, i, true);", "            ui_mark(u, i, true);"),
    ('ui/find.c', "    ui_find_close(u);\n}\n\nstatic void ui_find_open", "}\n\nstatic void ui_find_open"),
    # Not a mutant: ui_find_open's `find_select = false` is inert, a select bar always has the keys
    # (leaving closes it) and takes / and Ctrl+F itself, so no find opens over one.
    ('ui/handle.c', "        if (u->find_select) ui_find_select(u, names);\n        else ui_find_leave(u);", "        ui_find_leave(u);"),
    ('ui/handle.c', '        if (l->n > 0) ui_find_open(u), u->find_select = true;', '        if (l->n > 0) ui_find_open(u);'),
    ('ui/handle.c', '        if (l->n > 0) ui_find_open(u), u->find_select = true;', '        ui_find_open(u), u->find_select = true;'),
    ('ui/menus.c', "    case UM_SELECT_ALL: case UM_SELECT_MATCH: case UM_INVERT: return files;", "    case UM_SELECT_ALL: case UM_INVERT: return files;"),
]
# ---- F7.4: Duplicate
MUTANTS += [
    ('ui/menus.c', '    case UM_DUPLICATE: return (UiAction){ UA_DUPLICATE, 0, false };', ''),
]
MUTANTS += [
    ('ui/find.c', '    if (u->find_m.bad && u->find.len > 0) return;\n', ''),
]
# ---- F7.5: the bookmarks file, the Bookmarks menu
BMK = 'fs/bookmarks.c'
MUTANTS += [
    (BMK, "        if (!line.len) continue;\n        if (out->n == BM_MAX) {", "        if (out->n == BM_MAX) {"),
    (BMK, "            out->full = true;\n            break;", "            break;"),
    ('fs/places.c', "    for (size_t n = strlen(path); n > 1 && path[n - 1] == '/';) path[--n] = 0;\n", ''),
    (BMK, "        if (bm->b[i].path[0] && strcmp(bm->b[i].path, path) == 0) return i;", "        if (strcmp(bm->b[i].path, path) == 0) return i;"),
    (BMK, "    if (bm_find(bm, path) >= 0) return err_set(err, \"already a bookmark\"), false;", ""),
    (BMK, "        int k = b->label.len ? snprintf(", "        int k = false ? snprintf("),
    (BMK, "    if (!ops_allowed(file, err)) return false;\n", ""),
    (BMK, "    mode_t mode = stat(file, &st) == 0 ? st.st_mode & 0777 : 0644;", "    mode_t mode = 0644;"),
    (BMK, "    if (mkdir(dir, 0700) != 0 && errno != EEXIST)", "    if (mkdir(dir, 0755) != 0 && errno != EEXIST)"),
    # Not a mutant: `ok &&` before bm_write's rename needs a write() or fsync() that fails midway,
    # which no test can bring about.
    ('fs/paths.c', '    unlink(tmp);\n    return err_set(err, "%s: %s", path, strerror(e)), false;', '    return err_set(err, "%s: %s", path, strerror(e)), false;'),
    ('ui/menus.c', "        if (ui_menu_shown(u, UI_MENU[m].items[k])) return true;\n    return false;", "        if (ui_menu_shown(u, UI_MENU[m].items[k])) return true;\n    return true;"),
]
# ---- F7.5: Edit Bookmarks
MUTANTS += [
    (BMK, "    return str_eq(x->uri, y->uri) && str_eq(x->label, y->label);", "    return str_eq(x->uri, y->uri);"),
    (BMK, "    for (int k = 0; k < i; k++) before += bm_same(&was->b[k], &was->b[i]);", ""),
    (BMK, "        if (bm_same(&now->b[k], &was->b[i]) && before-- == 0) return k;", "        if (bm_same(&now->b[k], &was->b[i])) return k;"),
    (BMK, "    if (i < 0 || i >= was->n) return -1;", ""),
    (BMK, "    memmove(&bm->b[i], &bm->b[i + 1], sizeof bm->b[0] * (size_t)(bm->n - i - 1));\n    bm->n--;", "    bm->n--;"),
    (BMK, "    bm->b[i] = bm->b[j], bm->b[j] = t;", "    bm->b[i] = bm->b[j];"),
    (BMK, "        if (label.p[k] == '\\n' || label.p[k] == '\\r') label.len = k;", ""),
    (BMK, "    label = str_trim(label);\n    bm->b[i].label", "    bm->b[i].label"),
    ('ui/panels.c', "    ui_set(&u->bmp_list, u->bm_total, at >= 0 && at < u->bm_total ? at : 0);", "    ui_set(&u->bmp_list, u->bm_total, 0);"),
    ('ui/panels.c', '    case UI_BMP_REMOVE: return (UiAction){ UA_BM_REMOVE, l->sel, false };', '    case UI_BMP_REMOVE: return (UiAction){ UA_NONE, 0, false };'),
    ('ui/panels.c', '    case UI_BMP_RENAME: u->bmp_naming = true; return', '    case UI_BMP_RENAME: return'),
    ('ui/panels.c', '    case UI_BMP_UP: return l->sel > 0 ?', '    case UI_BMP_UP: return l->sel >= 0 ?'),
    ('ui/panels.c', '    case UI_BMP_DOWN: return l->sel < l->n - 1 ?', '    case UI_BMP_DOWN: return l->sel < l->n ?'),
    ('ui/panels.c', '    if (l->n == 0) return UI_NONE;\n    switch (what) {', '    switch (what) {'),
    ('ui/panels.c', "        case UK_ENTER: case UK_SHIFT_ENTER: u->bmp_naming = false; return (UiAction){ UA_BM_RENAME, l->sel, false };", "        case UK_ENTER: case UK_SHIFT_ENTER: u->bmp_naming = false; break;"),
    ('ui/panels.c', "        case UK_ESC: u->bmp_naming = false; break;", "        case UK_ESC: u->bmp_naming = false, ui_panel_close(u, UI_PANEL_BM); break;"),

    ('ui/panels.c', '    case UK_PARENT: return ui_bmp_do(u, UI_BMP_UP);', ''),
    ('ui/panels.c', '    case UK_ALT_DOWN: return ui_bmp_do(u, UI_BMP_DOWN);', ''),
    ('ui/panels.c', '    case UK_DELETE: return ui_bmp_do(u, UI_BMP_REMOVE);', ''),
    ('ui/panels.c', "        if (l->n == 0) break;\n        ui_panel_close(u, UI_PANEL_BM);\n        return (UiAction){ UA_BM_GO, l->sel, false };", "        ui_panel_close(u, UI_PANEL_BM);\n        return (UiAction){ UA_BM_GO, l->sel, false };"),
    ('ui/panels.c', '    case UK_ESC: ui_panel_close(u, UI_PANEL_BM); break;\n    default: break;\n    }\n    return UI_NONE;\n}', '    default: break;\n    }\n    return UI_NONE;\n}'),
    ('ui/menus.c', '    case UM_BM_MORE: return ui_bmp_open(u, u->bm_more_at);', '    case UM_BM_MORE: return ui_bmp_open(u, 0);'),
    ('ui/handle.c', '    case UK_EDIT_BOOKMARKS: return *out = ui_bmp_open(u, 0), true;', ''),
    ('ui/handle.c', '            if (in->bmp_drow >= 0) return *out = (ui_panel_close(u, UI_PANEL_BM), (UiAction){ UA_BM_GO, u->bmp_list.sel, false }), true;', ''),
    ('ui/handle.c', '        if (in->bmp_btn >= 1 && in->bmp_btn <= UI_BMP_BTNS && !u->bmp_naming)', '        if (in->bmp_btn >= 1 && in->bmp_btn <= UI_BMP_BTNS)'),
    ('ui/handle.c', '        if (in->modal == UI_MODAL_NO) return ui_panel_close(u, UI_PANEL_BM), *out = UI_NONE, true;\n    } else if (on == UI_ON_KEYS', '    } else if (on == UI_ON_KEYS'),   # (retargeted, step E)
    ('ui/cursor.c', 'in->bmp_row >= 0 || in->bmp_drow >= 0 || in->bmp_btn || in->panel_wheel != 0;', 'in->bmp_row >= 0 || in->bmp_drow >= 0 || in->panel_wheel != 0;'),
]
# ---- F7.5: Keyboard Shortcuts
MUTANTS += [
    (U, '    { KS_MOVE, "Trash", UK_TRASH_VIEW, NULL, NULL },', '    { KS_MOVE, "Trash", UK_HOMEDIR, NULL, NULL },'),
    (U, '    { KS_CHANGE, "Add Bookmark", UK_ADD_BOOKMARK, NULL, NULL },\n', ''),
    ('ui/panels.c', '        if (n == head + 1) n = head;   // nothing under it', ''),
    ('ui/panels.c', '        u->keys_row[n++] = (short)(-1 - s);', '        u->keys_row[n++] = (short)(-1);'),
    ('ui/panels.c', '            if (UI_KEYS[r].sec == (UiKeySection)s && (!u->keys_filter.len || ui_match(&u->keys_m, str_from_c(UI_KEYS[r].action)) ||', '            if (UI_KEYS[r].sec == (UiKeySection)s && (!u->keys_filter.len || false ||'),
    ('ui/panels.c', "                                                      ui_match(&u->keys_m, str_from_c(ui_keys_key(r, 0))) || ui_match(&u->keys_m, str_from_c(ui_keys_key(r, 1))) ||", "                                                      ui_match(&u->keys_m, str_from_c(ui_keys_key(r, 1))) ||"),
    ('ui/panels.c', '    u->panel = UI_PANEL_KEYS, UI_TEXT_CLEAR(u->keys_filter);', '    u->panel = UI_PANEL_KEYS;'),


    ('ui/panels.c', "    case UK_UP: case UK_DOWN: ui_scroll(l, e.key == UK_UP ? 1 : -1); break;", "    case UK_UP: case UK_DOWN: ui_scroll(l, 1); break;"),
    ('ui/panels.c', "    case UK_END: ui_scroll(l, (int)-l->n); break;", ""),
    ('ui/panels.c', '    if (e.cmd == UK_SHORTCUTS) {\n        ui_keys_close(u);\n        return;\n    }\n', ''),
    ('ui/handle.c', '    case UK_SHORTCUTS: return *out = ui_keys_open(u), true;', ''),
    ('ui/handle.c', '        if (in->modal == UI_MODAL_NO) return ui_keys_close(u), *out = UI_NONE, true;', ''),
    ('ui/handle.c', "        if (in->panel_wheel) ui_scroll(&u->keys_list, in->panel_wheel);", ""),
]
# ---- F7.5: the review's MEDIUM (a file not read whole) and Q2 (links)
MUTANTS += [
    (BMK, "    if (!text.len && (stat(file, &st) == 0 ? st.st_size > 0 : errno != ENOENT)) return", "    if (false) return"),
    (BMK, "(stat(file, &st) == 0 ? st.st_size > 0 : errno != ENOENT)", "(stat(file, &st) == 0 ? st.st_size > 0 : false)"),
    (BMK, "    if (lstat(file, &lst) == 0 && S_ISLNK(lst.st_mode)) {", "    if (false) {"),
    (BMK, "        if (!realpath(file, real)) return err_set(err, \"%s: a link to nothing (%s)\", file, strerror(errno)), false;", "        if (!realpath(file, real)) return false;"),
    (BMK, "    if (stat(file, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink > 1) {", "    if (false) {"),
    # Not a mutant: bm_write's `S_ISREG` before the in-place write; without it a folder (nlink > 1)
    # fails to open there instead of failing to be renamed over: false either way, nothing left.
    (BMK, '        bool ok = fd_write_all(fd, text.p, text.len) && fsync(fd) == 0;', '        bool ok = true;'),
]
# ---- Help → About (2026-10-27), and the wheel over a panel (f4fced7)
MUTANTS += [
    ('ui/menus.c', '    case UM_ABOUT: return u->panel = UI_PANEL_ABOUT, UI_NONE;', ''),
    ('ui/menus.c', "case UM_SHORTCUTS: case UM_ABOUT: case UM_PREFERENCES: return true;", "case UM_SHORTCUTS: case UM_PREFERENCES: return true;"),
    ('ui/handle.c', '        if (in->modal == UI_MODAL_NO) return ui_panel_close(u, UI_PANEL_ABOUT), *out = UI_NONE, true;', ''),
    ('ui/handle.c', '        if (e.key == UK_ESC || e.key == UK_ENTER) u->panel = UI_PANEL_NONE;', '        if (e.key == UK_ENTER) u->panel = UI_PANEL_NONE;'),
    ('ui/handle.c', '        if (e.key == UK_ESC || e.key == UK_ENTER) u->panel = UI_PANEL_NONE;', '        if (e.key == UK_ESC) u->panel = UI_PANEL_NONE;'),
    ('ui/cursor.c', ' || in->bmp_btn || in->panel_wheel != 0;', ' || in->bmp_btn;'),
]
# ---- F8 step 1: the files' right-click menus
MUTANTS += [
    (U, ".menu_item = -1, .ctx_row = -1, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box = -1,", ".menu_item = -1, .ctx_row = 0, .ctx_side = -1, .ctx_crumb = -1, .ctx_tab = -1, .prop_box = -1,"),
    ('ui/menus.c', "    case UM_OPEN_TAB: case UM_EMPTY_TRASH: case UM_OPEN_WINDOW:", "    case UM_OPEN_TAB: case UM_EMPTY_TRASH: return true;\n    case UM_OPEN_WINDOW:"),
    ('ui/menus.c', '    case UM_HERE_PROPS: return u->view != UI_VIEW_SEARCH;   // (a search is no folder)\n', ''),
    ('ui/menus.c', '    case UM_OPEN_TAB: return (UiAction){ UA_TAB_BG_ROW, u->list.sel, false };', ''),
    ('ui/menus.c', '    case UM_EMPTY_TRASH: return u->empty_asking = true, UI_NONE;', ''),
    ('ui/menus.c', '    case UM_HERE_PROPS: return ui_props_open(u, UI_PROPS_HERE);', '    case UM_HERE_PROPS: return ui_props_open(u, UI_PROPS_MARKED);'),
    ('ui/menus.c', '    ui_unfocus(u, UI_FOCUS_SIDE), ui_unfocus(u, UI_FOCUS_CRUMBS), ui_unfocus(u, UI_FOCUS_JOBS);\n    u->ctx_one', '    u->ctx_one'),
    ('ui/menus.c', "    u->ctx_one = u->list.n > 0 && (u->marked == 0 || (u->marked == 1 && ui_marked(u, u->list.sel)));", "    u->ctx_one = u->list.n > 0 && u->marked <= 1;"),
    ('ui/menus.c', "    u->ctx_one = u->list.n > 0 && (u->marked == 0 || (u->marked == 1 && ui_marked(u, u->list.sel)));", "    u->ctx_one = u->list.n > 0 && u->marked == 0;"),
    ('ui/menus.c', "    u->ctx_one = u->list.n > 0 && (", "    u->ctx_one = ("),
    ('ui/menus.c', "    u->ctx_key = keys, u->ctx_w = false;", "    u->ctx_w = false;"),
    ('ui/menus.c', '        if (u->menu >= UI_MENUS) return true;   // a right-click menu has no neighbours (F8)', ''),
    ('ui/menus.c', ' && u->menu < UI_MENUS) {   // (not from a right-click menu, F8)', ') {   // (not from a right-click menu, F8)'),
    ('ui/handle.c', "        if (!ui_marked(u, row)) ui_mark_clear(u);", ""),
    ('ui/handle.c', "        if (!ui_marked(u, row)) ui_mark_clear(u);", "        ui_mark_clear(u);"),
    ('ui/handle.c', "        u->list.sel = row;\n        ui_ctx_open", "        ui_ctx_open"),
    ('ui/handle.c', "    if (!prompt && in->ctx_row >= 0 && u->list.top + in->ctx_row < u->list.n) {", "    if (in->ctx_row >= 0 && u->list.top + in->ctx_row < u->list.n) {"),
    ('ui/handle.c', "    if (!prompt && in->ctx_here) {\n        ui_mark_clear(u);", "    if (!prompt && in->ctx_here) {"),
    ('ui/cursor.c', " || in->ctx_row >= 0 || in->ctx_here || in->ctx_side >= 0 || in->ctx_crumb >= 0 || in->ctx_tab >= 0 || in->prop_box >= 0 ||", " || in->prop_box >= 0 ||"),
    ('ui/handle.c', 'UK_CONTEXT && u->focus != UI_FOCUS_EDIT && !(u->focus == UI_FOCUS_FIND && u->finding)', 'UK_CONTEXT && !(u->focus == UI_FOCUS_FIND && u->finding)'),
    ('ui/handle.c', 'UK_CONTEXT && u->focus != UI_FOCUS_EDIT && !(u->focus == UI_FOCUS_FIND && u->finding) && u->focus != UI_FOCUS_JOBS) {', 'UK_CONTEXT && u->focus != UI_FOCUS_EDIT && u->focus != UI_FOCUS_JOBS) {'),
    ('ui/handle.c', '            else if (u->focus != UI_FOCUS_SIDE) ui_ctx_open(u, u->list.n > 0 ? UI_CTX_ENTRY : UI_CTX_HERE, names, true);', '            else if (u->focus != UI_FOCUS_SIDE) ui_ctx_open(u, UI_CTX_ENTRY, names, true);'),
    ('ui/handle.c', '            else if (u->focus != UI_FOCUS_SIDE) ui_ctx_open(u, u->list.n > 0 ? UI_CTX_ENTRY : UI_CTX_HERE, names, true);', '            else if (u->focus != UI_FOCUS_SIDE) ui_ctx_open(u, u->list.n > 0 ? UI_CTX_ENTRY : UI_CTX_HERE, names, false);'),
]
# ---- F8 step 2: a folder's own entries, Follow Link, Make Link and its undo
MUTANTS += [
    ('ui/menus.c', "    case UM_PASTE_INTO: return files && u->can_paste && u->ctx_w;", "    case UM_PASTE_INTO: return files && u->ctx_w;"),
    ('ui/menus.c', '    case UM_OPEN_WINDOW: return (UiAction){ UA_NEW_WINDOW, u->list.sel, true };', '    case UM_OPEN_WINDOW: return (UiAction){ UA_NEW_WINDOW, u->list.sel, false };'),
    ('ui/menus.c', '    case UM_PASTE_INTO: return (UiAction){ UA_PASTE, u->list.sel, true };', '    case UM_PASTE_INTO: return (UiAction){ UA_PASTE, u->list.sel, false };'),
    ('ui/menus.c', '    case UM_TERMINAL_IN: return (UiAction){ UA_TERMINAL, u->list.sel, true };', '    case UM_TERMINAL_IN: return (UiAction){ UA_TERMINAL, 0, true };'),
    ('ui/menus.c', '    case UM_BOOKMARK_IT: return (UiAction){ UA_ADD_BOOKMARK, u->list.sel, true };', '    case UM_BOOKMARK_IT: return (UiAction){ UA_ADD_BOOKMARK, u->list.sel, false };'),
    ('ui/menus.c', '    case UM_FOLLOW: return (UiAction){ UA_FOLLOW, u->list.sel, false };', ''),
    ('fs/ops.c', '                : kind == MAKE_LINK ? symlink(target, p) == 0', '                : kind == MAKE_LINK ? symlink(target, p) != 0'),
    ('fs/ops.c', '    if (!name_valid(name, err)) return false;\n    for (int tries = 0; tries < 5; tries++) {\n        char p[PATH_MAX];\n        if (!name_free(dir, name, kind == MAKE_DIR', '    for (int tries = 0; tries < 5; tries++) {\n        char p[PATH_MAX];\n        if (!name_free(dir, name, kind == MAKE_DIR'),
    (N, "            if (k < 0 || !it->from || (at[k] = 0, strcmp(at, it->from) != 0)) return", "            if (k < 0) return"),
    (N, "        if (u->kind == UNDO_LINK) {\n            char at", "        if (false) {\n            char at"),
    (N, "        case UNDO_LINK:\n        case UNDO_MKFILE:", "        case UNDO_MKFILE:"),
]
# ---- F8 step 3: a folder elsewhere's menu
MUTANTS += [
    ('ui/menus.c', "    if (u->menu == UI_CTX_FOLDER) return ui_folder_shown(u, it);", ""),
    ('ui/menus.c', '    case UM_OPEN_WINDOW: case UM_TERMINAL_IN: case UM_SEARCH_IN: case UM_PASTE_INTO: case UM_PROPS_IT: return !trash;', '    case UM_OPEN_WINDOW: case UM_TERMINAL_IN: case UM_SEARCH_IN: case UM_PASTE_INTO: case UM_PROPS_IT: return true;'),   # (retargeted, step F)
    ('ui/menus.c', '    case UM_CUT_IT: case UM_COPY_IT: return k == UI_CTX_DIR;', '    case UM_CUT_IT: case UM_COPY_IT: return k != UI_CTX_TRASH;'),   # (retargeted, step F)
    ('ui/menus.c', '    case UM_CUT_IT: case UM_COPY_IT: return k == UI_CTX_DIR;', '    case UM_CUT_IT: case UM_COPY_IT: return k != UI_CTX_PLACE;'),   # (retargeted, step F)
    ('ui/menus.c', '    case UM_BOOKMARK_IT: return !trash && !u->ctx_bmed;', '    case UM_BOOKMARK_IT: return !trash;'),   # (retargeted, step F)
    ('ui/menus.c', '    case UM_BOOKMARK_IT: return !trash && !u->ctx_bmed;', '    case UM_BOOKMARK_IT: return !u->ctx_bmed;'),   # (retargeted, step F)
    ('ui/menus.c', " case UM_BM_EDIT_IT: return u->ctx_bmk >= 0;", " case UM_BM_EDIT_IT: return u->ctx_bmk > 0;"),
    ('ui/menus.c', '    case UM_EJECT_IT: return k == UI_CTX_DRIVE;', '    case UM_EJECT_IT: return true;'),   # (retargeted, step F)
    ('ui/menus.c', '    case UM_EMPTY_IT: return trash;', '    case UM_EMPTY_IT: return true;'),   # (retargeted, step F)
    ('ui/menus.c', "    case UM_SEP: case UM_OPEN_IT: case UM_OPEN_TAB: return true;", "    case UM_SEP: case UM_OPEN_TAB: return true;"),
    ('ui/menus.c', "        if (it == UM_PASTE_INTO) return u->can_paste && u->ctx_w;", ""),
    ('ui/menus.c', "        if (it == UM_BM_UP_IT) return u->ctx_bmk > 0;", "        if (it == UM_BM_UP_IT) return u->ctx_bmk >= 0;"),
    ('ui/menus.c', "        if (it == UM_BM_DOWN_IT) return u->ctx_bmk < u->ctx_bms - 1;", "        if (it == UM_BM_DOWN_IT) return u->ctx_bmk < u->ctx_bms;"),
    ('ui/menus.c', "        if (it == UM_EMPTY_IT) return u->ctx_full;", ""),
    ('ui/menus.c', '        case UM_BM_EDIT_IT: return ui_bmp_open(u, u->ctx_bmk);', '        case UM_BM_EDIT_IT: return ui_bmp_open(u, 0);'),
    ('ui/menus.c', '        case UM_BM_RENAME_IT: return ui_bmp_open(u, u->ctx_bmk), u->bmp_naming = true,', '        case UM_BM_RENAME_IT: return ui_bmp_open(u, u->ctx_bmk),'),
    ('ui/menus.c', '        case UM_BM_UP_IT: return (UiAction){ UA_BM_UP, u->ctx_bmk, false };', '        case UM_BM_UP_IT: return (UiAction){ UA_BM_DOWN, u->ctx_bmk, false };'),
    ('ui/menus.c', '        case UM_BM_DOWN_IT: return (UiAction){ UA_BM_DOWN, u->ctx_bmk, false };', ''),
    ('ui/menus.c', '        case UM_BM_REMOVE_IT: return (UiAction){ UA_BM_REMOVE, u->ctx_bmk, false };', '        case UM_BM_REMOVE_IT: return (UiAction){ UA_BM_REMOVE, 0, false };'),
    ('ui/menus.c', '        case UM_EMPTY_IT: return u->empty_asking = true, UI_NONE;', ''),
    ('ui/menus.c', '        case UM_PROPS_IT: return ui_props_open(u, UI_PROPS_CTX);', '        case UM_PROPS_IT: return ui_props_open(u, UI_PROPS_MARKED);'),
    ('ui/menus.c', "    u->ctx_side = (int)side, u->ctx_crumb = crumb, u->ctx_key = keys;", "    u->ctx_side = (int)side, u->ctx_crumb = crumb;"),
    ('ui/menus.c', '    u->ctx_kind = UI_CTX_DIR, u->ctx_bmed = u->ctx_full = u->ctx_w = false;\n', '    u->ctx_w = false;\n'),   # (retargeted, step F)
    ('ui/menus.c', "    u->ctx_bmk = -1, u->ctx_bms = 0;\n", ""),
    ('ui/handle.c', "    if (!prompt && in->ctx_side >= 0 && u->side.top + in->ctx_side < u->side.n && !ui_side_head(u, u->side.top + in->ctx_side)) {", "    if (!prompt && in->ctx_side >= 0 && u->side.top + in->ctx_side < u->side.n) {"),
    ('ui/handle.c', "        u->side.sel = u->side.top + in->ctx_side;\n        ui_folder_open", "        ui_folder_open"),
    ('ui/handle.c', "    if (!prompt && in->ctx_crumb >= 0 && in->ctx_crumb < in->crumb_n) {", "    if (!prompt && in->ctx_crumb >= 0) {"),
    ('ui/handle.c', '            else if (u->focus == UI_FOCUS_CRUMBS) ui_folder_open(u, -1, u->crumb_at, true);', '            else if (false) ui_folder_open(u, -1, u->crumb_at, true);'),
    ('ui/handle.c', '            else if (u->focus == UI_FOCUS_SIDE && u->side.n > 0 && !ui_side_head(u, u->side.sel)) ui_folder_open(u, u->side.sel, -1, true);', '            else if (u->focus == UI_FOCUS_SIDE && u->side.n > 0) ui_folder_open(u, u->side.sel, -1, true);'),
    ('ui/handle.c', '            else if (u->focus != UI_FOCUS_SIDE) ui_ctx_open(u, u->list.n > 0 ? UI_CTX_ENTRY : UI_CTX_HERE, names, true);', '            else ui_ctx_open(u, u->list.n > 0 ? UI_CTX_ENTRY : UI_CTX_HERE, names, true);'),
    ('ui/handle.c', ' && u->focus != UI_FOCUS_EDIT && !(u->focus == UI_FOCUS_FIND && u->finding) && u->focus != UI_FOCUS_JOBS) {\n            if (u->focus == UI_FOCUS_CRUMBS && u->crumb_at >= in->crumb_n)', ' && u->focus != UI_FOCUS_EDIT && !(u->focus == UI_FOCUS_FIND && u->finding)) {\n            if (u->focus == UI_FOCUS_CRUMBS && u->crumb_at >= in->crumb_n)'),
    ('ui/cursor.c', " || in->ctx_side >= 0 || in->ctx_crumb >= 0 || in->ctx_tab >= 0 || in->prop_box >= 0 ||", " || in->prop_box >= 0 ||"),
]
# ---- F8 step 4: a tab's menu
MUTANTS += [
    ('ui/menus.c', "    case UM_TAB_LEFT: return u->tab_at > 0;", "    case UM_TAB_LEFT: return true;"),
    ('ui/menus.c', "    case UM_TAB_RIGHT: return u->tab_at < u->tab_n - 1;", "    case UM_TAB_RIGHT: return u->tab_at < u->tab_n;"),
    ('ui/menus.c', "    case UM_TAB_OTHERS: return u->tab_n > 1;", "    case UM_TAB_OTHERS: return true;"),
    ('ui/menus.c', '    case UM_TAB_OTHERS: return (UiAction){ UA_TAB_OTHERS, 0, false };', ''),
    ('ui/handle.c', "    if (!prompt && in->ctx_tab >= 0 && in->ctx_tab < u->tab_n) {", "    if (!prompt && in->ctx_tab >= 0) {"),
    ('ui/handle.c', '        return *out = (UiAction){ UA_TAB_SHOW, in->ctx_tab, false }, true;', '        return *out = (UiAction){ UA_NONE, in->ctx_tab, false }, true;'),
    ('ui/cursor.c', " || in->ctx_tab >= 0 || in->prop_box >= 0 ||", " || in->prop_box >= 0 ||"),
]
# ---- icons up the subclasses, a blank page last (Michael, 2026-10-27)
MUTANTS += [
    (M, "        if (up == 0) names[n++] = str_from_c(arena_sprintf(a, \"%.*s-x-generic\", STR_ARG(media)));", "        names[n++] = str_from_c(arena_sprintf(a, \"%.*s-x-generic\", STR_ARG(media)));"),
    (M, "        if (!strmap_get(&db->parent, t, &i)) break;", "        break;"),
    (M, "    for (int up = 0; up <= MIME_PARENTS_MAX; up++) {", "    for (int up = 0; up <= MIME_PARENTS_MAX + 1; up++) {"),
    (M, "        t = db->icon_of[i];\n", ""),
    (M, "    names[n++] = S(\"empty\");\n", ""),
    (M, "        if (strmap_get(&db->parent, type, &have)) continue;", ""),
    (M, "        if (sp <= 0 || sp == line.len - 1 || db->nicons == MIME_GLOBS_MAX) continue;", "        if (sp <= 0 || db->nicons == MIME_GLOBS_MAX) continue;"),
    (M, "        db->icon_of[db->nicons] = str_trim(str_cut(line, sp + 1, line.len));", "        db->icon_of[db->nicons] = str_cut(line, sp, line.len);"),
    (M, "        mime_add_parents(db, read_file(a, arena_sprintf(a, \"%s/mime/subclasses\", dirs[k])));", ""),
]
# ---- F8 review: Paste Into Folder greyed on a folder that takes no new files
MUTANTS += [
    ('ui/menus.c', "        if (it == UM_PASTE_INTO) return u->can_paste && u->ctx_w;", "        if (it == UM_PASTE_INTO) return u->can_paste;"),
    ('ui/menus.c', "    case UM_PASTE_INTO: return files && u->can_paste && u->ctx_w;", "    case UM_PASTE_INTO: return files && u->can_paste;"),
    ('ui/menus.c', '    u->ctx_kind = UI_CTX_DIR, u->ctx_bmed = u->ctx_full = u->ctx_w = false;', '    u->ctx_kind = UI_CTX_DIR, u->ctx_bmed = u->ctx_full = false;'),   # (retargeted, step F)
    ('ui/menus.c', "    u->ctx_key = keys, u->ctx_w = false;   // (gui/ says, as it opens)", "    u->ctx_key = keys;   // (gui/ says, as it opens)"),
]
# ---- the list's Owner, Group, Permissions (View → Show Permissions) and links' targets (2026-10-27).
# Not mutants: `link ?` before link_target (a readlinkat per entry saved; on a non-link it fails and
# gives empty anyway) and its `k <= 0` (a link that can't be read between the stat and it: a race).
MUTANTS += [
    ('ui/helpers.c', "    for (int k = 0; k < 9; k++) p[k] = mode & (0400u >> k) ? RWX[k % 3] : '-';", "    for (int k = 0; k < 9; k++) p[k] = mode & (0400u >> k) ? RWX[k % 3] : 'x';"),
    ('ui/helpers.c', "    if (mode & 04000) p[2] = p[2] == 'x' ? 's' : 'S';", ""),
    ('ui/helpers.c', "    if (mode & 02000) p[5] = p[5] == 'x' ? 's' : 'S';", "    if (mode & 02000) p[5] = 's';"),
    ('ui/helpers.c', "    if (mode & 01000) p[8] = p[8] == 'x' ? 't' : 'T';", "    if (mode & 01000) p[8] = 't';"),
    ('ui/helpers.c', "    if (!mode) {\n        snprintf(buf, cap, \"-\");\n        return;\n    }", ""),
    ('ui/menus.c', "    case UM_PERMS_COLS: return u->perms_cols;", "    case UM_PERMS_COLS: return 1;"),
    ('ui/handle.c', '        if (!u->grid) return *out = (u->perms_cols = !u->perms_cols, (UiAction){ UA_PERMS_COLS, 0, false }), true;', '        if (!u->grid) return *out = (UiAction){ UA_PERMS_COLS, 0, false }, true;'),
    ('ui/handle.c', '        if (!u->grid) return *out = (u->perms_cols = !u->perms_cols, (UiAction){ UA_PERMS_COLS, 0, false }), true;', '        return *out = (u->perms_cols = !u->perms_cols, (UiAction){ UA_PERMS_COLS, 0, false }), true;'),
    (L, "        .uid = (u32)st.st_uid, .gid = (u32)st.st_gid,", "        .uid = 0, .gid = (u32)st.st_gid,"),
    (L, "        .target = link ? link_target(a, fd, nm) : STR_EMPTY,", "        .target = STR_EMPTY,"),
    (L, "    memcpy(p, buf, (size_t)k), p[k] = 0;", "    memcpy(p, buf, (size_t)k - 1), p[k] = 0;"),
    (ST, "        if (str_eq(key, S(\"perms\"))) s.no_perms = str_eq(val, S(\"off\"));", ""),
]
# ---- View → Show Group, Ctrl+P, Ctrl+G (2026-10-27)
MUTANTS += [
    (U, "    [UM_PERMS_COLS] = { \"Show Permissions\", \"perms-cols\", .cmd = UK_PERMS_COLS, .runs = true, .mark = 'c' },", "    [UM_PERMS_COLS] = { \"Show Permissions\", \"perms-cols\", .cmd = UK_PERMS_COLS, .mark = 'c' },"),
    (U, "    [UM_GROUP_COL] = { \"Show Group\", \"group-col\", .cmd = UK_GROUP_COL, .runs = true, .mark = 'c' },", "    [UM_GROUP_COL] = { \"Show Group\", \"group-col\", .cmd = UK_GROUP_COL, .mark = 'c' },"),
    ('ui/menus.c', "    case UM_GROUP_COL: return !u->grid && u->perms_cols;", "    case UM_GROUP_COL: return !u->grid;"),
    ('ui/menus.c', "    case UM_GROUP_COL: return !u->grid && u->perms_cols;", "    case UM_GROUP_COL: return u->perms_cols;"),
    ('ui/menus.c', "    case UM_PERMS_COLS: case UM_COLORS: return !u->grid;", "    case UM_PERMS_COLS: case UM_COLORS: return true;"),
    ('ui/menus.c', "    case UM_GROUP_COL: return u->group_col;", "    case UM_GROUP_COL: return 1;"),
    ('ui/handle.c', '        if (!u->grid && u->perms_cols) return *out = (u->group_col = !u->group_col, (UiAction){ UA_GROUP_COL, 0, false }), true;', '        if (u->perms_cols) return *out = (u->group_col = !u->group_col, (UiAction){ UA_GROUP_COL, 0, false }), true;'),
    ('ui/handle.c', '        if (!u->grid && u->perms_cols) return *out = (u->group_col = !u->group_col, (UiAction){ UA_GROUP_COL, 0, false }), true;', '        if (!u->grid) return *out = (u->group_col = !u->group_col, (UiAction){ UA_GROUP_COL, 0, false }), true;'),
    ('ui/handle.c', '        if (!u->grid && u->perms_cols) return *out = (u->group_col = !u->group_col, (UiAction){ UA_GROUP_COL, 0, false }), true;', '        if (!u->grid && u->perms_cols) return *out = (UiAction){ UA_GROUP_COL, 0, false }, true;'),
    (U, "UM_HIDDEN, UM_PERMS_COLS, UM_GROUP_COL, UM_COLORS, UM_SHOW_MENU, UM_SEP,", "UM_HIDDEN, UM_PERMS_COLS, UM_COLORS, UM_SHOW_MENU, UM_SEP,"),
    (ST, "        if (str_eq(key, S(\"group\"))) s.group = str_eq(val, S(\"on\"));", ""),
    (ST, "        if (str_eq(key, S(\"group\"))) s.group = str_eq(val, S(\"on\"));", "        if (str_eq(key, S(\"group\"))) s.group = true;"),
]
# ---- View → Show Menu (2026-10-27)
MUTANTS += [
    ('ui/menus.c', "    case UM_SHOW_MENU: return !u->bar_hidden;", "    case UM_SHOW_MENU: return 1;"),
    ('ui/menus.c', 'u->panel = u->bar_hidden ? UI_PANEL_BAR : UI_PANEL_NONE,', 'u->panel = UI_PANEL_BAR,'),
    ('ui/menus.c', 'return u->bar_hidden = !u->bar_hidden, u->panel =', 'return u->panel ='),
    ('ui/handle.c', '        if (in->modal == UI_MODAL_OK) return ui_panel_close(u, UI_PANEL_BAR), *out = UI_NONE, true;', ''),
    ('ui/handle.c', '        if (e.key == UK_ESC || e.key == UK_ENTER) u->panel = UI_PANEL_NONE;', '        if (e.key == UK_ESC || e.key == UK_ENTER) ui_panel_close(u, UI_PANEL_ABOUT);'),
    (ST, "        if (str_eq(key, S(\"menubar\"))) s.no_bar = str_eq(val, S(\"off\"));", ""),
]
# ---- View → Show Colors; the status line's note when the permissions give way (2026-10-27)
MUTANTS += [
    ('ui/menus.c', "    case UM_COLORS: return u->colors;", "    case UM_COLORS: return 1;"),
    ('ui/menus.c', '    case UM_COLORS: return u->colors = !u->colors, (UiAction){ UA_COLORS, 0, false };', '    case UM_COLORS: return (UiAction){ UA_COLORS, 0, false };'),
    ('ui/helpers.c', "    if (in->cramped && k < cap) snprintf", "    if (k < cap) snprintf"),
    ('ui/helpers.c', "    if (in->cramped && k < cap) snprintf", "    if (false) snprintf"),
    (ST, "        if (str_eq(key, S(\"colors\"))) s.no_colors = str_eq(val, S(\"off\"));", ""),
]
# ---- F9 step 1: the search view (2026-10-28)
SR = 'fs/search.c'
MUTANTS += [
    (SR, "    return strncmp(dir, SEARCH_URI, sizeof SEARCH_URI - 1) == 0;", "    return strncmp(dir, SEARCH_URI, sizeof SEARCH_URI - 2) == 0;"),
    (SR, '    return n >= 0 && (size_t)n < cap;\n}\n\n// A length', '    return n >= 0;\n}\n\n// A length'),
    (SR, "    if (!is_search(uri)) return false;", ""),
    (SR, '        if (*len > PATH_MAX) return false;', ''),
    (SR, "    if (*p++ != ':' || (isize)strnlen(p, (size_t)len) < len || len == 0 || p[0] != '/') return false;", "    if (*p++ != ':' || len == 0 || p[0] != '/') return false;"),
    (SR, "    if (*p++ != ':' || (isize)strnlen(p, (size_t)len) < len || len == 0 || p[0] != '/') return false;", "    if (*p++ != ':' || (isize)strnlen(p, (size_t)len) < len || len == 0) return false;"),
    (SR, "    if (*p++ != ':' || (isize)strnlen(p, (size_t)len) < len || len == 0 || p[0] != '/') return false;", "    if ((isize)strnlen(++p, (size_t)len) < len || len == 0 || p[0] != '/') return false;"),
    (SR, '    if (tlen < 0) return *text = rest, *inside = STR_EMPTY, true;', '    if (tlen < 0) return *text = (Str){ rest.p - 1, rest.len + 1 }, *inside = STR_EMPTY, true;'),
    (SR, "    return w->gen && atomic_load(w->gen) != w->mine;", "    return false;"),
    (SR, "        if (strcmp(name, l->name[k]) == 0) return k;", ""),
    (SR, "    if (depth > SEARCH_DEPTH_MAX) return close(fd), w->s.unread++, true;", ""),
    (SR, "    w->s.folders++;", ""),
    (SR, "        if (nm[0] == '.' && !w->hidden) {", "        if (nm[0] == '.') {"),
    (SR, "        if (nm[0] == '.' && !w->hidden) {", "        if (false) {"),
    (SR, "            w->s.hidden++;", ""),
    (SR, '        if (w->match(w->ctx, name)) {', '        if (true) {'),
    (SR, '            else if (!w->found(w->ctx, (Str){ w->rel, w->rel_len }, name, dirfd(d), NULL)) w->s.full = true, go = false;', '            else if (!w->found(w->ctx, (Str){ w->rel, w->rel_len }, name, dirfd(d), NULL)) go = false;'),
    (SR, '            else if (!w->found(w->ctx, (Str){ w->rel, w->rel_len }, name, dirfd(d), NULL)) w->s.full = true, go = false;', '            else if (!w->found(w->ctx, (Str){ w->rel, w->rel_len }, name, dirfd(d), NULL)) w->s.full = true;'),
    # Kept out, no test can reach it: the stat when readdir gives no type (DT_UNKNOWN). tmpfs, ext4
    # and btrfs always give one; some network and older file systems don't.
    (SR, "        if (t == DT_LNK && fstatat(dirfd(d), nm, &st, 0) == 0 && S_ISDIR(st.st_mode)) w->s.links++;", "        if (t == DT_LNK) w->s.links++;"),
    (SR, "        if (t == DT_LNK && fstatat(dirfd(d), nm, &st, 0) == 0 && S_ISDIR(st.st_mode)) w->s.links++;", "        if (fstatat(dirfd(d), nm, &st, 0) == 0 && S_ISDIR(st.st_mode)) w->s.links++;"),
    (SR, "        if (k > 0) at += (size_t)k;", ""),
    (SR, "        if (k >= 0) {", "        if (false) {"),
    (SR, "            w->s.skip[k]++;", ""),
    (SR, "        if (st.st_dev != w->dev) {", "        if (false) {"),
    (SR, "            w->s.drives++;", ""),
    (SR, "        if (was) w->rel[w->rel_len++] = '/';", ""),
    (SR, "        if (sub < 0) w->s.unread++;", "        if (sub < 0) {}"),
    (SR, "        w->rel_len = was, w->rel[was] = 0;", ""),
    (SR, "        w->s.unread++;\n        if (w->tick) w->tick(w->ctx, &w->s);\n        return true;", "        if (w->tick) w->tick(w->ctx, &w->s);\n        return true;"),
    (SR, "    for (int i = 0; i < names->n; i++) SEARCH_PART(s->skip[i], names->name[i], names->name[i])", ""),
    (SR, "    for (int i = 0; i < names->n; i++) SEARCH_PART(s->skip[i], names->name[i], names->name[i])", "    for (int i = 1; i < names->n; i++) SEARCH_PART(s->skip[i], names->name[i], names->name[i])"),
    (SR, "    SEARCH_PART(s->hidden, \"hidden\", \"hidden\")", ""),
    (SR, "    SEARCH_PART(s->drives, \"other drive\", \"other drives\")", "    SEARCH_PART(s->drives, \"other drives\", \"other drives\")"),
    (SR, "    SEARCH_PART(s->links, \"link to a folder\", \"links to folders\")", "    SEARCH_PART(s->links, \"links to folders\", \"links to folders\")"),
    (SR, "    SEARCH_PART(s->unread, \"unreadable folder\", \"unreadable folders\")", ""),
    (SR, "        sep = \", \";", ""),
    (L, "    if (c == 0) c = natural_cmp(a->dir, b->dir);", ""),
    (L, "        if (str_eq(l->e[i].name, name) && str_eq(l->e[i].dir, dir)) return i;", "        if (str_eq(l->e[i].name, name)) return i;"),
    (L, "    int n = e->dir.len ? snprintf(buf, cap, \"%.*s/%.*s\", STR_ARG(e->dir), STR_ARG(e->name)) : snprintf(buf, cap, \"%.*s\", STR_ARG(e->name));", "    int n = snprintf(buf, cap, \"%.*s\", STR_ARG(e->name));"),
    (L, "    return n >= 0 && (size_t)n < cap ? (Str){ buf, n } : STR_EMPTY;", "    return n >= 0 ? (Str){ buf, n } : STR_EMPTY;"),
    ('fs/list.c', '        if (!entry_read_far(a, fd, at, nm, ms, &e[n])) {', '        if (!entry_read_far(a, fd, at, nm, ms, &e[n]) && false) {'),   # (retargeted, step C)
    # the wildcard
    ('ui/match.c', "    return !m->o.regex && (memchr(m->text, '*', (size_t)m->len) || memchr(m->text, '?', (size_t)m->len));", "    return memchr(m->text, '*', (size_t)m->len) || memchr(m->text, '?', (size_t)m->len);"),
    ('ui/match.c', "    return !m->o.regex && (memchr(m->text, '*', (size_t)m->len) || memchr(m->text, '?', (size_t)m->len));", "    return !m->o.regex && memchr(m->text, '*', (size_t)m->len);"),
    ('ui/match.c', "    if (name.len > NAME_MAX) return false;", ""),
    ('ui/match.c', "    bool hit = fnmatch(m->text, buf, (m->o.cased ? 0 : FNM_CASEFOLD)) == 0;", "    bool hit = fnmatch(m->text, buf, 0) == 0;"),
    ('ui/match.c', "    locale_t was = ui_utf8_on();\n    bool hit = fnmatch", "    locale_t was = (locale_t)0;\n    bool hit = fnmatch"),
    # the field
    ('ui/handle.c', '        *o = ui_toggle(*o, ui_toggle_key(e)), u->search_edited = true;', '        *o = ui_toggle(*o, ui_toggle_key(e));'),
    ('ui/handle.c', '    case UK_ENTER: case UK_SHIFT_ENTER: case UK_ESC: case UK_DOWN: u->focus = UI_FOCUS_LIST; return true;', '    case UK_ENTER: case UK_SHIFT_ENTER: case UK_ESC: u->focus = UI_FOCUS_LIST; return true;'),
    ('ui/handle.c', '    case UK_ENTER: case UK_SHIFT_ENTER: case UK_ESC: case UK_DOWN: u->focus = UI_FOCUS_LIST; return true;', '    case UK_ENTER: case UK_SHIFT_ENTER: case UK_ESC: case UK_DOWN: return true;'),
    ('ui/handle.c', '    default: u->focus = UI_FOCUS_LIST; return false;\n    }\n}\n\n// The keys back to', '    default: return false;\n    }\n}\n\n// The keys back to'),
    ('ui/handle.c', '    case UI_FIELD_SEARCH: return u->view == UI_VIEW_SEARCH;', '    case UI_FIELD_SEARCH: return false;'),
    ('ui/handle.c', '    } else if (!prompt && !u->menu_on && in->field >= 0 && in->field < UI_FIELDS && ui_field_shown(u, (UiField)in->field)) {', '    } else if (!prompt && in->field >= 0 && in->field < UI_FIELDS && ui_field_shown(u, (UiField)in->field)) {'),
    ('ui/handle.c', '            if (u->view == UI_VIEW_SEARCH) u->search_edited = true;\n        } else', '        } else'),
    ('ui/handle.c', '    } else if (!prompt && !u->menu_on && in->field >= 0 && in->field < UI_FIELDS && ui_field_shown(u, (UiField)in->field)) {', '    } else if (!u->menu_on && in->field >= 0 && in->field < UI_FIELDS && ui_field_shown(u, (UiField)in->field)) {'),
    ('ui/cursor.c', ' in->field >= 0 || in->inside_btn ||', ' in->inside_btn ||'),
    ('ui/handle.c', '            return *out = ui_search_open(u), true;', '            return *out = UI_NONE, true;'),
    ('ui/handle.c', '    case UK_SEARCH: return *out = ui_search_open(u), true;', ''),
    (U, '"search", .cmd = UK_SEARCH, .runs = true },', '"search", .cmd = UK_SEARCH },'),
    ('ui/ui.c', 'UM_SEP, UM_FIND, UM_SEARCH, UM_LOCATION, UM_CONNECT, UM_AIRPLANE, UM_TERMINAL,\n};', 'UM_SEP, UM_FIND, UM_LOCATION, UM_CONNECT, UM_AIRPLANE, UM_TERMINAL,\n};'),   # (retargeted, step 8 A)
    # what the results can't do yet, and the menus
    ('ui/menus.c', '    case UM_HERE_PROPS: return u->view != UI_VIEW_SEARCH;', '    case UM_HERE_PROPS: return true;'),
    ('ui/helpers.c', "    if (str_eq(cw, S(TRASH_URI)) || is_search(cwd)) {", "    if (str_eq(cw, S(TRASH_URI))) {"),
    ('ui/helpers.c', "    if (str_starts_with(ui_crumb_path(c, i), S(SEARCH_URI))) return S(\"Search\");", ""),
    ('ui/helpers.c', "sel.len < (isize)sizeof stack[*n].sel ? (int)sel.len : 0, sel.p ? sel.p : \"\");", "(int)sel.len, sel.p ? sel.p : \"\");"),
    ('ui/helpers.c', "sel.len < (isize)sizeof stack[*n].sel ? (int)sel.len : 0, sel.p ? sel.p : \"\");", "sel.len <= (isize)sizeof stack[*n].sel ? (int)sel.len : 0, sel.p ? sel.p : \"\");"),
]
# ---- F9 step 2: working with results (2026-10-28)
MUTANTS += [
    (U, '"containing", .cmd = UK_CONTAINING, .runs = true },', '"containing", .cmd = UK_CONTAINING },'),
    ('ui/handle.c', '        if (u->view == UI_VIEW_SEARCH && files) return *out = (UiAction){ UA_CONTAINING, l->sel, false }, true;', '        if (files) return *out = (UiAction){ UA_CONTAINING, l->sel, false }, true;'),
    ('ui/handle.c', '        if (u->view == UI_VIEW_SEARCH && files) return *out = (UiAction){ UA_CONTAINING, l->sel, false }, true;', '        if (u->view == UI_VIEW_SEARCH && l->n > 0) return *out = (UiAction){ UA_CONTAINING, l->sel, false }, true;'),
    ('ui/handle.c', '        if (u->view == UI_VIEW_SEARCH && files) return *out = (UiAction){ UA_CONTAINING, l->sel, false }, true;', '        if (u->view == UI_VIEW_SEARCH && files) return *out = (UiAction){ UA_CONTAINING, 0, false }, true;'),
    ('ui/menus.c', '    case UM_SEARCH_IN: return (UiAction){ UA_SEARCH, u->list.sel, true };', '    case UM_SEARCH_IN: return (UiAction){ UA_SEARCH, u->list.sel, false };'),
    ('ui/menus.c', '    case UM_OPEN_WINDOW: case UM_TERMINAL_IN: case UM_SEARCH_IN: case UM_PASTE_INTO: case UM_PROPS_IT: return !trash;', '    case UM_OPEN_WINDOW: case UM_TERMINAL_IN: case UM_PASTE_INTO: case UM_PROPS_IT: return !trash;'),   # (retargeted, step F)
    (U, "    UM_OPEN, UM_CONTAINING, UM_OPEN_LINE, UM_OPEN_TAB,", "    UM_OPEN, UM_OPEN_LINE, UM_OPEN_TAB,"),
    (U, "    UM_OPEN, UM_CONTAINING, UM_OPEN_LINE, UM_OPEN_TAB,", "    UM_OPEN, UM_CONTAINING, UM_OPEN_TAB,"),
    (U, "UM_TERMINAL_IN, UM_SEARCH_IN, UM_FOLLOW,", "UM_TERMINAL_IN, UM_FOLLOW,"),
    ('ui/handle.c', "    case UK_CONTAINING:   // (the search view's: elsewhere every entry is in the folder shown)", '    case UK_CONTAINING + 1000:'),
]
# ---- the search field edits as the rename line does (Michael, 2026-10-28)
MUTANTS += [



    ('ui/edit.c', '    return UI_TEXT_LINE(u->search, false);', '    return UI_TEXT_LINE(u->search, true);'),
    ('ui/handle.c', '    UiLineHit h = ui_line_key(u, l, e, in->paste);\n    if (h == UI_LINE_CHANGED) u->search_edited = true;', '    UiLineHit h = ui_line_key(u, l, e, STR_EMPTY);\n    if (h == UI_LINE_CHANGED) u->search_edited = true;'),
]
# ---- the find bar's field on the shared line (Michael, 2026-10-28)
MUTANTS += [



    ('ui/edit.c', '    return UI_TEXT_LINE(u->find, false);', '    return UI_TEXT_LINE(u->find, true);'),
]
# ---- Ctrl+C, Ctrl+X with nothing selected in the fields: the list's (review, 2026-10-28)
MUTANTS += [
    ('ui/handle.c', "    if (ui_line_copies_nothing(l, e)) return u->focus = UI_FOCUS_LIST, false;   // (the cursor's result)\n", ''),
    ('ui/edit.c', '    return (e.key == UK_COPY || e.key == UK_CUT) && e.cmd && lo == hi;', '    return e.key == UK_COPY && e.cmd && lo == hi;'),
    ('ui/edit.c', '    return (e.key == UK_COPY || e.key == UK_CUT) && e.cmd && lo == hi;', '    return (e.key == UK_COPY || e.key == UK_CUT) && e.cmd;'),
    ('ui/handle.c', '    if (ui_line_copies_nothing(ui_find_line(u), e)) return ui_find_leave(u), false;\n', ''),
    ('ui/edit.c', '    return (e.key == UK_COPY || e.key == UK_CUT) && e.cmd && lo == hi;', '    return (e.key == UK_CUT) && e.cmd && lo == hi;'),
]
# ---- the text fields on one line editor, and words (2026-10-28)
MUTANTS += [
    ('ui/edit.c', "        while (at > 0 && !ui_word_byte((unsigned char)l.b[at - 1])) at--;", ""),
    ('ui/edit.c', "        while (at > 0 && ui_word_byte((unsigned char)l.b[at - 1])) at--;", ""),
    ('ui/edit.c', "    while (at < *l.len && !ui_word_byte((unsigned char)l.b[at])) at++;", ""),
    ('ui/edit.c', "    while (at < *l.len && ui_word_byte((unsigned char)l.b[at])) at++;\n    return at;", "    return at;"),
    ('ui/edit.c', "    case UK_WORD_LEFT: *l.caret = *l.anchor = ui_line_word(l, *l.caret, -1); return true;", "    case UK_WORD_LEFT: *l.caret = ui_line_word(l, *l.caret, -1); return true;"),
    ('ui/edit.c', "    case UK_WORD_RIGHT: *l.caret = *l.anchor = ui_line_word(l, *l.caret, 1); return true;", "    case UK_WORD_RIGHT: *l.caret = *l.anchor = ui_line_fwd(l, *l.caret); return true;"),
    ('ui/edit.c', "    case UK_SEL_WORD_LEFT: *l.caret = ui_line_word(l, *l.caret, -1); return true;", "    case UK_SEL_WORD_LEFT: *l.caret = *l.anchor = ui_line_word(l, *l.caret, -1); return true;"),
    ('ui/edit.c', "    case UK_SEL_WORD_RIGHT: *l.caret = ui_line_word(l, *l.caret, 1); return true;", ""),
    ('ui/edit.c', "    case UK_WORD_BACKSPACE: ui_line_cut(l, lo < hi ? lo : ui_line_word(l, *l.caret, -1), lo < hi ? hi : *l.caret); return true;", "    case UK_WORD_BACKSPACE: ui_line_cut(l, ui_line_word(l, *l.caret, -1), *l.caret); return true;"),
    ('ui/edit.c', "    case UK_WORD_DELETE: ui_line_cut(l, lo < hi ? lo : *l.caret, lo < hi ? hi : ui_line_word(l, *l.caret, 1)); return true;", "    case UK_WORD_DELETE: ui_line_cut(l, *l.caret, ui_line_word(l, *l.caret, 1)); return true;"),
    ('ui/edit.c', "    *l.caret = (int)ui_clamp(*l.caret, 0, *l.len), *l.anchor = (int)ui_clamp(*l.anchor, 0, *l.len);", ""),
    ('ui/edit.c', ': *l.len != was_len || memcmp(was, l.b, (size_t)was_len) != 0 ? UI_LINE_CHANGED', ': *l.len != was_len ? UI_LINE_CHANGED'),   # (retargeted, step 8 A)
    ('ui/edit.c', '!did ? UI_LINE_NOT : *l.len != was_len || memcmp(was, l.b, (size_t)was_len) != 0 ? UI_LINE_CHANGED : UI_LINE_MOVED;', '!did ? UI_LINE_NOT : UI_LINE_CHANGED;'),   # (retargeted, step 8 A)
    ('ui/edit.c', '    if (pk && !pk->in_list) l = ui_pick_line(pk);', '    if (pk) l = ui_pick_line(pk);'),   # (retargeted, step E)
    ('ui/edit.c', '    else if (on == UI_ON_BM && u->bmp_naming) l = ui_bmp_line(u);', '    else if (on == UI_ON_BM) l = ui_bmp_line(u);'),
    ('ui/edit.c', '    else if (on == UI_ON_KEYS) l = ui_keys_line(u);\n', ''),
    ('ui/edit.c', '    else if (u->focus == UI_FOCUS_EDIT) l = ui_edit_line(u);', ''),
    ('ui/edit.c', '    else if (u->focus == UI_FOCUS_SEARCH && u->view == UI_VIEW_SEARCH) l = ui_search_line(u);', '    else if (u->view == UI_VIEW_SEARCH) l = ui_search_line(u);'),
    ('ui/edit.c', '    else if (u->focus == UI_FOCUS_FIND && u->finding) l = ui_find_line(u);', '    else if (u->finding) l = ui_find_line(u);'),
    ('ui/handle.c', "    if (h == UI_LINE_CHANGED) u->search_edited = true;", "    if (h) u->search_edited = true;"),
    ('ui/handle.c', "    if (h == UI_LINE_CHANGED) ui_find_update(u, names);", ""),
    ('ui/panels.c', "        if (ui_line_key(u, ui_keys_line(u), e, paste) == UI_LINE_CHANGED) ui_keys_filter(u);", "        ui_line_key(u, ui_keys_line(u), e, paste);"),
    ('ui/panels.c', '        if (ui_line_key(u, ui_bmp_line(u), e, paste)) return UI_NONE;', ''),
    ('ui/handle.c', '    case UK_SIDE_GO:   // (Ctrl+Left: a word in a text field, the sidebar here)\n', ''),
    (KY, "{ K('l', C_), UK_LOCATION }, { K(UKC_RIGHT, C_), UK_LOCATION },", "{ K('l', C_), UK_LOCATION },"),
]

# fs/paths.c (2026-10-28): the path helpers each fs/ file had its own copy of, and their callers.
MUTANTS += [
    ('fs/paths.c', 'return strcmp(dir, "/") == 0 || (strncmp(path, dir, n) == 0 && (path[n] == 0 || path[n] == \'/\'));', "return strncmp(path, dir, n) == 0 && (path[n] == 0 || path[n] == '/');"),
    ('fs/paths.c', 'return strcmp(dir, "/") == 0 || (strncmp(path, dir, n) == 0 && (path[n] == 0 || path[n] == \'/\'));', 'return strcmp(dir, "/") == 0 || (strncmp(path, dir, n) == 0 && path[n] == \'/\');'),
    ('fs/paths.c', 'return strcmp(dir, "/") == 0 || (strncmp(path, dir, n) == 0 && (path[n] == 0 || path[n] == \'/\'));', 'return strcmp(dir, "/") == 0 || strncmp(path, dir, n) == 0;'),
    ('fs/paths.c', "return strncmp(path, dir, n) == 0 && path[n] == '/' && path[n + 1];", "return strncmp(path, dir, n) == 0 && path[n] == '/';"),
    ('fs/paths.c', "return strncmp(path, dir, n) == 0 && path[n] == '/' && path[n + 1];", 'return strncmp(path, dir, n) == 0 && path[n + 1];'),
    ('fs/paths.c', 'dir, strcmp(dir, "/") == 0 ? "" : "/", STR_ARG(name));', 'dir, "/", STR_ARG(name));'),
    ('fs/paths.c', "STR_ARG(name));\n    return n >= 0 && (size_t)n < cap;", "STR_ARG(name));\n    return n >= 0;"),
    ('fs/paths.c', 'STR_ARG(d.len ? d : S("/")));\n    return n', 'STR_ARG(d));\n    return n'),
    ('fs/paths.c', '    if (x && x[0] == \'/\') n = snprintf(out, cap, "%s/%s", x, sub);', '    if (x && x[0]) n = snprintf(out, cap, "%s/%s", x, sub);'),
    ('fs/paths.c', "    else if (home && home[0] == '/') n = snprintf", '    else if (home) n = snprintf'),
    ('fs/paths.c', '    return n > 0 && (size_t)n < cap;\n}\n\n// mkdir -p', '    return n > 0;\n}\n\n// mkdir -p'),
    ('fs/paths.c', "        *s = '/';\n    }\n    if (whole", '    }\n    if (whole'),
    ('fs/paths.c', '    if (whole && mkdir(p, mode) != 0', '    if (false && mkdir(p, mode) != 0'),
    ('fs/paths.c', '        if (mkdir(p, mode) != 0 && errno != EEXIST) return err_set(err, "%s: %s", p, strerror(errno)), false;\n        *s = \'/\';', "        mkdir(p, mode);\n        *s = '/';"),
    ('fs/paths.c', "return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;", "return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;"),
    ('fs/paths.c', '            if (h < 0 || l < 0 || h * 16 + l == 0) return false;', '            if (h < 0 || l < 0) return false;'),
    ('fs/paths.c', '            if (h < 0 || l < 0 || h * 16 + l == 0) return false;', '            if (h * 16 + l == 0) return false;'),
    ('fs/paths.c', '            c = (char)(h * 16 + l), i += 2;', '            c = (char)(h * 16 + l), i += 1;'),
    ('fs/paths.c', '        if (o + 1 >= cap) return false;\n        out[o++] = c;\n    }\n    out[o] = 0;\n    return true;\n}\n\n// s %XX', '        out[o++] = c;\n    }\n    out[o] = 0;\n    return true;\n}\n\n// s %XX'),
    ('fs/paths.c', ' ||\n                     (c && strchr(keep, c));', ';'),
    ('fs/paths.c', '(c && strchr("-._~", c)) ||', ''),
    ('fs/paths.c', '    static const char HEX[] = "0123456789ABCDEF";', '    static const char HEX[] = "0123456789abcdef";'),
    ('fs/paths.c', '        if (n + (as_is ? 1 : 3) >= cap) return false;', '        if (n + (as_is ? 1 : 3) > cap) return false;'),
    ('fs/paths.c', '    else if (str_starts_with(uri, S("file://localhost/"))) rest = str_cut(uri, 16, uri.len);\n', ''),
    ('fs/paths.c', '    if (str_starts_with(uri, S("file:///"))) rest = str_cut(uri, 7, uri.len);', '    if (str_starts_with(uri, S("file://"))) rest = str_cut(uri, 7, uri.len);'),
    ('fs/paths.c', "            c = (char)((s.p[i + 1] - '0') * 64 + (s.p[i + 2] - '0') * 8 + (s.p[i + 3] - '0'));", "            c = (char)((s.p[i + 1] - '0') * 8 + (s.p[i + 2] - '0') * 8 + (s.p[i + 3] - '0'));"),
    ('fs/paths.c', "s.p[i + 1] >= '0' && s.p[i + 1] <= '3' && s.p[i + 2] >= '0' &&", "s.p[i + 2] >= '0' &&"),
    ('fs/paths.c', '    for (int k = 0; k < 4; k++) {', '    for (int k = 0; k < 3; k++) {'),
    ('fs/paths.c', '    *type = s1 < 0 ? rest : str_cut(rest, 0, s1);', '    *type = rest;'),
    ('fs/paths.c', '        *source = s2 < 0 ? src : str_cut(src, 0, s2);', '        *source = src;'),
    ('fs/paths.c', '    if (sp <= 0 || !octal_decode(str_cut(f, 0, sp), mp, cap)) return false;', '    if (!octal_decode(str_cut(f, 0, sp < 0 ? 0 : sp), mp, cap)) return false;'),
    ('fs/guard.c', '    if (ops_resolve(path, r, sizeof r) && path_below(r, ops_root)) return true;', '    if (ops_resolve(path, r, sizeof r) && path_within(r, ops_root)) return true;'),
    ('fs/guard.c', '    if (ops_resolve(path, r, sizeof r) && path_below(r, ops_root)) return true;', '    if (ops_resolve(path, r, sizeof r)) return true;'),
    ('fs/job.c', '    return path_within(d, s);', '    return path_below(d, s);'),
    ('fs/places.c', ' || path_below(mp, "/media") ||', ' ||'),
    ('fs/places.c', ' || (places_test_media[0] && path_below(mp, places_test_media)))', ')'),
    ('fs/places.c', '        if (pl->n == before) continue;\n', ''),
    ('fs/places.c', 'if (path_within(path, p) && (best < 0 || n > best_n)) best = i, best_n = n;', 'if (path_within(path, p) && best < 0) best = i, best_n = n;'),
    ('fs/places.c', '    if (!file_uri_path(*uri, path, cap)) return path[0] = 0, false;', '    file_uri_path(*uri, path, cap);'),
    ('fs/thumb.c', '    if (path_below(q->path, q->root)) return TG_NONE;', '    if (0) return TG_NONE;'),
    ('fs/paths.c', '(!best || ms->e[i].n >= best->n) && path_within', '(!best || ms->e[i].n > best->n) && path_within'),   # (retargeted, step C)
    ('fs/paths.c', '        if ((!best || ms->e[i].n >= best->n) && path_within(path, ms->e[i].mp)) best = &ms->e[i];', '        if (!best || ms->e[i].n >= best->n) best = &ms->e[i];'),   # (retargeted, step C)
    ('fs/paths.c', '#define URI_PATH_KEEP "!$&\'()*+,=@:/"', '#define URI_PATH_KEEP "!$\'()*+,=@:/"'),   # (retargeted, step F)
    ('fs/thumb.c', ' || !ops_allowed(file, &err) || !mkdir_p(file, 0700, false, &err)) return false;', ' || !mkdir_p(file, 0700, false, &err)) return false;'),
    ('fs/thumb.c', '!mkdir_p(file, 0700, false, &err)) return false;', '!mkdir_p(file, 0755, false, &err)) return false;'),
    ('fs/paths.c', '        if ((!best || ms->e[i].n >= best->n) && path_within(path, ms->e[i].mp)) best = &ms->e[i];', '        if (path_within(path, ms->e[i].mp)) best = &ms->e[i];'),   # (retargeted, step C)
    ('fs/trash.c', '    if (strcmp(top, "/") == 0) return res + 1;\n', ''),
    ('fs/trash.c', '    return path_below(res, top) ? res + strlen(top) + 1 : res;', '    return res + strlen(top) + 1;'),
    ('fs/trash.c', '    if (!top[0]) return res;\n', ''),
    ('fs/trash.c', '    if (stat(d, &st) != 0 && (!ops_allowed(d, err) || !mkdir_p(d, 0755, true, err))) return false;', '    if (stat(d, &st) != 0 && !mkdir_p(d, 0755, true, err)) return false;'),
    ('fs/trash.c', '    if (stat(d, &st) != 0 && (!ops_allowed(d, err) || !mkdir_p(d, 0755, true, err))) return false;', '    if (stat(d, &st) != 0 && !ops_allowed(d, err)) return false;'),
    ('fs/trash.c', '    if (snprintf(tr.file, sizeof tr.file, "%s", t->file) >= (int)sizeof tr.file ||', '    if (snprintf(tr.file, sizeof tr.file, "%s", t->file) < 0 ||'),
    ('fs/trash.c', "            if (dec[0] == '/' && top[0] && !path_within(dec, top)) continue;\n", ''),
    ('fs/trash.c', ' || !ops_resolve(t->from, res, sizeof res) || !path_within(res, top))', ' || !ops_resolve(t->from, res, sizeof res))'),
    ('fs/paths.c', '    if (!mkdir_p(path, 0700, true, err)) return false;\n', ''),
    ('fs/fm1.c', '        if (!file_uri_path(str_from_c(uris[i]), path, sizeof path) || stat(path, &st) != 0) {', '        if (!file_uri_path(str_from_c(uris[i]), path, sizeof path)) {'),
    ('fs/theme.c', '    if (!xdg_dir(buf, cap, "XDG_STATE_HOME", ".local/state", "omarchy/current/theme")) buf[0] = 0;', '    xdg_dir(buf, cap, "XDG_STATE_HOME", ".local/state", "omarchy/current/theme");'),
]

# One view value and what each view allows (ui_view_allows, 2026-10-28): the table, its callers in the keys and the menus.
MUTANTS += [
    ('ui/ui.c', 'case UA_COPY: case UA_COPY_PATH: case UA_CUT: case UA_MAKE_LINK:', 'case UA_COPY_PATH: case UA_CUT: case UA_MAKE_LINK:'),
    ('ui/ui.c', '    case UA_RESTORE: case UA_EMPTY_TRASH: return v == UI_VIEW_TRASH;\n', ''),
    ('ui/ui.c', '    case UA_CONTAINING: return v == UI_VIEW_SEARCH;\n', ''),
    ('ui/ui.c', 'u->view == UI_VIEW_SEARCH ? UA_NOT_IN_SEARCH : UA_NONE, 0, false };', 'UA_NOT_IN_SEARCH, 0, false };'),
    ('ui/ui.c', '{ u->view == UI_VIEW_TRASH ? UA_NOT_IN_TRASH : u->view', '{ false ? UA_NOT_IN_TRASH : u->view'),
    ('ui/handle.c', '    if (u->view != UI_VIEW_SEARCH) return ui_view_act(u, (UiAction){ UA_SEARCH, 0, false });', '    if (u->view != UI_VIEW_SEARCH) return (UiAction){ UA_SEARCH, 0, false };'),
    (KY, "{ T('t'), UK_TERMINAL },", ''),
    ('ui/handle.c', '{ UK_ADD_BOOKMARK, UA_ADD_BOOKMARK, 0, true, false },', '{ UK_ADD_BOOKMARK, UA_ADD_BOOKMARK, 0, false, false },'),
    ('ui/handle.c', '{ UK_COPY, UA_COPY, 0, true, true },', '{ UK_COPY, UA_COPY, 0, false, true },'),
    ('ui/handle.c', '{ UK_MAKE_LINK, UA_MAKE_LINK, 0, true, true },', '{ UK_MAKE_LINK, UA_MAKE_LINK, 0, false, true },'),
    ('ui/handle.c', '{ UK_CUT, UA_CUT, 0, true, true },', '{ UK_CUT, UA_CUT, 0, false, true },'),
    ('ui/handle.c', '{ UK_PASTE, UA_PASTE, 0, true, false },', '{ UK_PASTE, UA_PASTE, 0, false, false },'),
    ('ui/handle.c', '    { UK_NEWFOLDER, UA_NEWFOLDER, 0, true, false },', '    { UK_NEWFOLDER, UA_NEWFOLDER, 0, false, false },'),
    ('ui/handle.c', '{ UK_NEW_WINDOW, UA_NEW_WINDOW, 0, true, false },', '{ UK_NEW_WINDOW, UA_NEW_WINDOW, 0, false, false },'),
    ('ui/handle.c', '    { UK_TERMINAL, UA_TERMINAL, 0, true, false },', '    { UK_TERMINAL, UA_TERMINAL, 0, false, false },'),
    ('ui/menus.c', '    case UM_NEW_WINDOW: return ui_view_allows(u->view, UA_NEW_WINDOW);', '    case UM_NEW_WINDOW: return true;'),
    ('ui/menus.c', '    case UM_COPY: return here && ui_view_allows(u->view, UA_COPY);', '    case UM_COPY: return here;'),
    ('ui/menus.c', '    case UM_SEARCH: return ui_view_allows(u->view, UA_SEARCH);', '    case UM_SEARCH: return true;'),
    ('ui/menus.c', '    case UM_TERMINAL: return ui_view_allows(u->view, UA_TERMINAL);', '    case UM_TERMINAL: return true;'),
    ('ui/menus.c', '    case UM_ADD_BOOKMARK: return u->can_bookmark && ui_view_allows(u->view, UA_ADD_BOOKMARK);', '    case UM_ADD_BOOKMARK: return ui_view_allows(u->view, UA_ADD_BOOKMARK);'),
    ('ui/menus.c', '    case UM_ADD_BOOKMARK: return u->can_bookmark && ui_view_allows(u->view, UA_ADD_BOOKMARK);', '    case UM_ADD_BOOKMARK: return u->can_bookmark;'),
    ('ui/handle.c', 'ui_unfocus(u, UI_FOCUS_SIDE), ui_unfocus(u, UI_FOCUS_FIND);   // (the find bar', 'ui_unfocus(u, UI_FOCUS_SIDE);   // (the find bar'),
    ('ui/edit.c', '    int n = s.len < l.max ? (int)s.len : l.max;', '    int n = (int)s.len;'),
    ('ui/edit.c', '    *l.len = *l.caret = *l.anchor = n;', '    *l.len = *l.caret = n;'),
]

# ---- F9 step 3: inside files (2026-10-29)
MUTANTS += [
    # the address
    (SR, '    int n = inside.len ? snprintf(', '    int n = false ? snprintf('),
    (SR, '    return *p > at;', '    return true;'),
    (SR, "    if (*p == ',' && (p++, !search_len(&p, &tlen))) return false;", "    if (*p == ',') return false;"),
    (SR, '    if (tlen > rest.len || tlen == rest.len) return false;', '    if (tlen > rest.len) return false;'),
    (SR, '    *text = (Str){ rest.p, tlen };', '    *text = rest;'),
    (SR, '    *inside = (Str){ rest.p + tlen, rest.len - tlen };', '    *inside = rest;'),
    # a line, and the hit kept
    (SR, "    for (isize i = 0; i < n; i++) c += ((unsigned char)s[i] & 0xC0) != 0x80;", "    for (isize i = 0; i < n; i++) c++;"),
    (SR, "    if (piece.len && piece.p[piece.len - 1] == '\\r') piece.len--;   // (CRLF)", ''),
    (SR, '    if (sc->lit || !w->line(w->ctx, piece, &from, &to)) return;', '    if (!w->line(w->ctx, piece, &from, &to)) return;'),
    (SR, '    if (sc->lines++) return;', '    sc->lines++;'),
    (SR, '    isize a = from > SEARCH_SNIP_BEFORE ? from - SEARCH_SNIP_BEFORE : 0;', '    isize a = 0;'),
    (SR, "    while (a < from && ((unsigned char)piece.p[a] & 0xC0) == 0x80) a++;\n    bool cut", '    bool cut'),
    (SR, "    while (a < from && (piece.p[a] == ' ' || piece.p[a] == '\\t')) a++;", ''),
    (SR, '    isize b = a + SEARCH_SNIP < piece.len ? a + SEARCH_SNIP : piece.len;', '    isize b = piece.len < SEARCH_SNIP ? piece.len : SEARCH_SNIP;'),
    (SR, "        w->snip[i - a] = c < 0x20 || c == 0x7F ? ' ' : (char)c;", '        w->snip[i - a] = (char)c;'),
    (SR, '.col = sc->before + search_chars(piece.p, from) + 1,', '.col = search_chars(piece.p, from) + 1,'),
    (SR, '.col = sc->before + search_chars(piece.p, from) + 1,', '.col = sc->before + from + 1,'),
    (SR, '.at = from - a,', '.at = from,'),
    # reading a file
    (SR, '    int fd = openat(dfd, name, fl | O_NOATIME);', '    int fd = openat(dfd, name, fl);'),
    (SR, '    if (st.st_size > SEARCH_FILE_MAX) return close(fd), SR_BIG;', '    if (st.st_size >= SEARCH_FILE_MAX) return close(fd), SR_BIG;'),
    (SR, '    if (st.st_size > SEARCH_FILE_MAX) return close(fd), SR_BIG;', ''),
    (SR, '            if (memchr(buf, 0, (size_t)(have < SEARCH_HEAD ? have : SEARCH_HEAD))) {', '            if (memchr(buf, 0, (size_t)have)) {'),
    (SR, '            if (memchr(buf, 0, (size_t)(have < SEARCH_HEAD ? have : SEARCH_HEAD))) {', '            if (false) {'),
    (SR, '            sc.line++, sc.before = 0, sc.lit = false;', '            sc.line++, sc.before = 0;'),
    (SR, '            sc.line++, sc.before = 0, sc.lit = false;', '            sc.line++, sc.lit = false;'),
    (SR, '            if (start < have) search_scan(w, &sc, (Str){ buf + start, have - start }, hit);', ''),
    (SR, '            search_scan(w, &sc, (Str){ buf, have }, hit);\n            isize keep', '            isize keep'),
    (SR, '            isize keep = have - SEARCH_OVERLAP;', '            isize keep = have;'),
    (SR, '            sc.before += search_chars(buf, keep);', ''),
    (SR, '    if (r == SR_NONE && sc.lines) hit->more = sc.lines - 1, r = SR_HIT;', '    if (r == SR_NONE && sc.lines) hit->more = sc.lines, r = SR_HIT;'),
    (SR, '    if (r == SR_NONE && sc.lines) hit->more = sc.lines - 1, r = SR_HIT;', '    if (sc.lines) hit->more = sc.lines - 1, r = SR_HIT;'),
    (SR, '        if (search_stopped(w)) {\n            r = SR_STOPPED;', '        if (false) {\n            r = SR_STOPPED;'),
    # the walk inside files, and what it counts
    (SR, '        if (fstatat(dirfd(d), nm, &st, 0) == 0 && S_ISREG(st.st_mode)) w->s.file_links++;', ''),
    (SR, '    if (t != DT_REG) return true;\n    SearchHit hit;', '    SearchHit hit;'),
    (SR, '    case SR_HIT: w->s.files++; break;', '    case SR_HIT: break;'),
    (SR, '    case SR_NONE: w->s.files++; return true;', '    case SR_NONE: return true;'),
    (SR, '    case SR_BINARY: w->s.binary++; return true;', '    case SR_BINARY: return true;'),
    (SR, '    case SR_BIG: w->s.big++; return true;', '    case SR_BIG: return true;'),
    (SR, '    case SR_UNREAD: w->s.unread_files++; return true;', '    case SR_UNREAD: return true;'),
    # (not SR_STOPPED's false made true: the walk's next entry stops it, as the generation moved)
    (SR, '    if (w->found(w->ctx, (Str){ w->rel, w->rel_len }, str_from_c(nm), dirfd(d), &hit)) return true;\n    w->s.full = true;', '    if (w->found(w->ctx, (Str){ w->rel, w->rel_len }, str_from_c(nm), dirfd(d), &hit)) return true;'),
    (SR, '            if (w->line) go = search_inside(w, d, nm, t);', '            if (false) go = search_inside(w, d, nm, t);'),
    (SR, '    SEARCH_PART(s->binary, "binary", "binary")', ''),
    (SR, '    SEARCH_PART(s->big, big, big)', ''),
    (SR, '    SEARCH_PART(s->file_links, "link to a file", "links to files")', '    SEARCH_PART(s->file_links, "link to a file", "link to a file")'),
    (SR, '    SEARCH_PART(s->unread_files, "unreadable file", "unreadable files")', ''),
    # the matcher's spans and its quick starts
    ('ui/match.c', '    if (!m->o.word) return *at = all.rm_so, *end = all.rm_eo, true;', '    if (!m->o.word) return *at = 0, *end = all.rm_eo, true;'),
    ('ui/match.c', '            if (quick && c < 0x80 && fold(c) != fold(first)) continue;', '            if (quick && fold(c) != fold(first)) continue;'),
    ('ui/match.c', '    bool quick = first < 0x80;', '    bool quick = true;'),
    ('ui/match.c', '            if (quick && c < 0x80 && fold(c) != fold(first)) continue;', '            if (quick && c < 0x80 && c != first) continue;'),
    # the second field's keys
    ('ui/handle.c', '    if (on) u->focus = UI_FOCUS_INSIDE;', '    if (false) u->focus = UI_FOCUS_INSIDE;'),
    ('ui/handle.c', '    else if (u->focus == UI_FOCUS_INSIDE) u->focus = UI_FOCUS_SEARCH;', ''),
    ('ui/handle.c', '    u->search_inside = on, u->search_edited = true;', '    u->search_inside = on;'),
    ('ui/handle.c', '        UiMatchOpts *o = inside ? &u->inside_opts : &u->find_opts;', '        UiMatchOpts *o = &u->find_opts;'),
    ('ui/handle.c', '    if (e.key == UK_TAB && u->search_inside) return', '    if (e.key == UK_TAB) return'),
    ('ui/handle.c', '    if (e.key == UK_TAB && u->search_inside) return u->focus = inside ? UI_FOCUS_SEARCH : UI_FOCUS_INSIDE,', '    if (e.key == UK_TAB && u->search_inside) return u->focus = UI_FOCUS_INSIDE,'),
    ('ui/handle.c', '    UiLine l = inside ? ui_inside_line(u) : ui_search_line(u);', '    UiLine l = ui_search_line(u);'),
    ('ui/handle.c', "        if (u->view == UI_VIEW_SEARCH && e.key == UK_MENU && e.ch == 'i') {", "        if (u->view == UI_VIEW_SEARCH && e.key == UK_MENU && e.ch == 'x') {"),
    ('ui/handle.c', "        if (u->view == UI_VIEW_SEARCH && e.key == UK_MENU && e.ch == 'i') {", "        if (e.key == UK_MENU && e.ch == 'i') {"),
    ('ui/handle.c', '(u->focus == UI_FOCUS_INSIDE && u->search_inside)) && u->view', 'u->focus == UI_FOCUS_INSIDE) && u->view'),
    ('ui/handle.c', '    } else if (!prompt && !u->menu_on && u->view == UI_VIEW_SEARCH && in->inside_btn) {', '    } else if (!prompt && u->view == UI_VIEW_SEARCH && in->inside_btn) {'),
    ('ui/handle.c', '    } else if (!prompt && !u->menu_on && u->view == UI_VIEW_SEARCH && in->inside_btn) {', '    } else if (!prompt && !u->menu_on && in->inside_btn) {'),
    ('ui/handle.c', '    case UI_FIELD_INSIDE: return u->view == UI_VIEW_SEARCH && u->search_inside;', '    case UI_FIELD_INSIDE: return u->view == UI_VIEW_SEARCH;'),
    ('ui/handle.c', '        ui_search_inside(u, !u->search_inside);\n        return *out = UI_NONE, true;\n    }', '        ui_search_inside(u, true);\n        return *out = UI_NONE, true;\n    }'),
    ('ui/handle.c', '    return f == UI_FIELD_INSIDE ? &u->inside_opts : &u->find_opts;', '    return &u->find_opts;'),
    ('ui/edit.c', '    else if (u->focus == UI_FOCUS_INSIDE && u->view == UI_VIEW_SEARCH && u->search_inside) l = ui_inside_line(u);', ''),
    ('ui/edit.c', '    else if (u->focus == UI_FOCUS_INSIDE && u->view == UI_VIEW_SEARCH && u->search_inside) l = ui_inside_line(u);', '    else if (u->focus == UI_FOCUS_INSIDE && u->view == UI_VIEW_SEARCH) l = ui_inside_line(u);'),
    ('ui/cursor.c', ' in->field >= 0 || in->inside_btn ||', ' in->field >= 0 ||'),
]

# ---- F9 step 4: Open at Line (2026-10-29)
ED = 'fs/editors.c'
MUTANTS += [
    # the built-in recipes (the test checks each runs its name, with {file} and {line})
    (ED, '"+call setcursorcharpos({line},{col})", "{file}" } },\n    { "vim"', '"+call cursor({line},{col})", "{file}" } },\n    { "vim"'),
    (ED, '{ "nvim", "Neovim", true,', '{ "nvim", "Neovim", false,'),
    (ED, '{ "code", "VS Code", false, { "code", "--goto", "{file}:{line}:{col}" } },', '{ "code", "VS Code", false, { "code", "{file}:{line}:{col}" } },'),
    # the user's file
    (ED, "        while (i < line.len && (line.p[i] == ' ' || line.p[i] == '\\t')) i++;\n        if (i == line.len) break;", "        if (i == line.len) break;"),
    (ED, '        if (n == cap) return -1;', ''),
    (ED, "            if (line.p[i] == '\"') quoted = !quoted;", "            if (false) quoted = !quoted;"),
    (ED, '        if (quoted) return -1;', ''),
    (ED, "        if (line.len == 0 || line.p[0] == '#') continue;", "        if (line.len == 0) continue;"),
    (ED, '        bool term = n >= 3 && strcmp(w[1], "yes") == 0;', '        bool term = n >= 3;'),
    (ED, '        if (n < 3 || (!term && strcmp(w[1], "no") != 0) ||', '        if (n < 3 ||'),
    (ED, '        if (n < 3 || (!term', '        if (n < 2 || (!term'),
    (ED, '            if (!out->bad) out->bad = no;', '            out->bad = no;'),
    (ED, '        for (int k = 2; k < n; k++) ed->argv[k - 2] = w[k];', '        for (int k = 1; k < n; k++) ed->argv[k - 1] = w[k];'),
    (ED, '        line = str_trim(line), no++;', '        line = str_trim(line);'),
    # the default
    (ED, '        name = str_trim(str_cut(f, 0, e));\n    }', '        name = str_cut(f, 0, e);\n    }'),
    (ED, '    if (name.len == 0 && env) {', '    if (env) {'),
    (ED, "        while (e < s.len && s.p[e] != ' ' && s.p[e] != '\\t') e++;\n        name = str_cut(s, 0, e);", '        name = s;'),
    (ED, '    name = path_base(name);', ''),
    (ED, '    if (name.len == 0 || name.len >= (isize)cap) name = S("nvim");', '    if (name.len >= (isize)cap) name = S("nvim");'),
    # finding one
    (ED, '    for (int i = 0; user && i < user->n; i++)\n        if (strcmp(user->e[i].name, name) == 0) return &user->e[i];', ''),
    # the command filled in
    (ED, '        if (n >= words - 2) return -1;   // (room for the file, and the NULL)', ''),
    (ED, "        if (w[0] == '~' && w[1] == '/' && home && home[0] == '/') {", '        if (false) {'),
    (ED, '            m = (size_t)r, w++;', '            m = (size_t)r;'),
    (ED, '            if (strncmp(p, "{file}", 6) == 0) put = file, skip = 6, has_file = true;', '            if (strncmp(p, "{file}", 6) == 0) put = file, skip = 6;'),
    (ED, '            else if (strncmp(p, "{line}", 6) == 0) snprintf(num, sizeof num, "%lld", (long long)line), put = num, skip = 6;', ''),
    (ED, '            else if (strncmp(p, "{col}", 5) == 0) snprintf(num, sizeof num, "%lld", (long long)col), put = num, skip = 5;', ''),
    (ED, '            if (at + m + len + 1 > cap) return -1;', ''),
    (ED, '        at += m + 1;', '        at += m;'),
    (ED, '    if (!has_file) {', '    if (false) {'),
    (ED, '    argv[n] = NULL;\n    return n;', '    return n;'),
    # the launch
    (ED, "    if (strchr(prog, '/') ? access(prog, X_OK) != 0 : !on_path(prog)) return err_set(err, \"%s isn't installed (%s)\", e->label, prog), false;", ''),
    (ED, "    if (strchr(prog, '/') ? access(prog, X_OK) != 0 : !on_path(prog))", "    if (!on_path(prog))"),
    (ED, '    if (!e->term) return spawn_app(argv, n, err);', ''),
    (ED, '    const char *cmd[EDITOR_WORDS + 4] = { "xdg-terminal-exec", id, "-e" };', '    const char *cmd[EDITOR_WORDS + 4] = { "xdg-terminal-exec", id };'),
    (ED, '    int k = 3;', '    int k = 2;'),
    # ui/: the key, the entry
    ('ui/handle.c', '        if (u->view == UI_VIEW_SEARCH && files) return *out = (UiAction){ UA_OPEN_LINE, l->sel, false }, true;', '        if (files) return *out = (UiAction){ UA_OPEN_LINE, l->sel, false }, true;'),
    ('ui/handle.c', '        if (u->view == UI_VIEW_SEARCH && files) return *out = (UiAction){ UA_OPEN_LINE, l->sel, false }, true;', '        if (u->view == UI_VIEW_SEARCH && l->n > 0) return *out = (UiAction){ UA_OPEN_LINE, l->sel, false }, true;'),
    ('ui/handle.c', '        if (u->view == UI_VIEW_SEARCH && files) return *out = (UiAction){ UA_OPEN_LINE, l->sel, false }, true;', '        if (u->view == UI_VIEW_SEARCH && u->focus != UI_FOCUS_SIDE) return *out = (UiAction){ UA_OPEN_LINE, l->sel, false }, true;'),
]

# ---- after F9: the lighter milestone review's fixes, and Ctrl+K to the search field (2026-10-29)
MUTANTS += [
    ('ui/handle.c', '    u->focus = u->search_inside && u->search_was_inside ? UI_FOCUS_INSIDE : UI_FOCUS_SEARCH;', '    u->focus = u->search_was_inside ? UI_FOCUS_INSIDE : UI_FOCUS_SEARCH;'),
    ('ui/handle.c', '    u->focus = u->search_inside && u->search_was_inside ? UI_FOCUS_INSIDE : UI_FOCUS_SEARCH;', '    u->focus = u->search_inside ? UI_FOCUS_INSIDE : UI_FOCUS_SEARCH;'),
    ('ui/handle.c', '    u->search_was_inside = inside;\n', ''),
    ('ui/handle.c', '    if (e.cmd == UK_CRUMBS) return u->focus = UI_FOCUS_LIST, ui_crumbs_focus(u, in), true;', ''),
    ('ui/handle.c', '    if (e.cmd == UK_CRUMBS) return u->focus = UI_FOCUS_LIST, ui_crumbs_focus(u, in), true;', '    if (e.cmd == UK_CRUMBS) return ui_crumbs_focus(u, in), true;'),
    ('ui/handle.c', '        if (u->view == UI_VIEW_SEARCH) ui_search_back(u);\n        else ui_crumbs_focus(u, in);', '        ui_crumbs_focus(u, in);'),
    ('ui/handle.c', '    ui_edit_close(u);\n    ui_search_back(u);', '    ui_edit_close(u);\n    u->focus = UI_FOCUS_SEARCH;'),
    (SR, '    snprintf(big, sizeof big, "over %d MB", SEARCH_FILE_MAX_MB);', '    snprintf(big, sizeof big, "over %d MB", 1);'),
]
MUTANTS += [
    # the text bars' one table (2026-10-29, after F9's review)
    ('ui/handle.c', ' && in->field < UI_FIELDS && ui_field_shown(u, (UiField)in->field)) {', ' && in->field < UI_FIELDS) {'),
    ('ui/handle.c', '    case UI_FIELD_FIND: return u->finding;', '    case UI_FIELD_FIND: return true;'),
    ('ui/handle.c', '            if (u->finding) ui_find_update(u, names);', ''),
]
MUTANTS += [
    ('ui/handle.c', ', u->search_was_inside = !inside, true;', ', true;'),
]

# ---- the places, then the drives (2026-10-30: back from a day with the drives on top)
MUTANTS += [
    (PL, '    places_head(pl, "Drives");\n    Str mi = read_file(scratch, mountinfo);\n    places_add_mounts(pl, mi);\n', '    Str mi = read_file(scratch, mountinfo);\n'),
    (PL, '    ArenaMark mark = arena_mark(scratch);\n    places_head(pl, "Places");', '    ArenaMark mark = arena_mark(scratch);\n    places_head(pl, "Drives");\n    places_add_mounts(pl, read_file(scratch, mountinfo));\n    places_head(pl, "Places");'),
    ('fs/places.c', 'place_is_drive(pl->p[i].kind) == place_is_drive(kind) && place_is_net', 'pl->p[i].kind == kind && place_is_net'),   # (retargeted, step 8 A)
    (PL, '(best < 0 || n > best_n)) best = i', '(best < 0 || n >= best_n)) best = i'),
]

# ---- Tab on the folder shown's sidebar row (Michael, 2026-10-29)
SH = '        if (u->focus == UI_FOCUS_SIDE) u->side.sel = u->side_here > 0 && u->side_here < u->side.n ? u->side_here : 0, ui_side_settle(u, 1);'
MUTANTS += [
    ('ui/handle.c', SH, '        if (u->focus == UI_FOCUS_SIDE) ui_side_settle(u, 1);'),
    ('ui/handle.c', SH, SH.replace('u->side_here > 0 && u->side_here < u->side.n ? u->side_here : 0', '0')),
    ('ui/handle.c', SH, SH.replace(' && u->side_here < u->side.n', '')),
    ('ui/handle.c', SH, SH.replace('u->side_here > 0 && ', '')),
    # Not a mutant: `u->side_here >= 0` (row 0 and the first row settle the same).
]
MUTANTS += [
    # the nearest folder above with a row (Michael, 2026-10-30)
    ('fs/tree.c', '        if (i >= 0 || path.len <= 1) return i;', '        if (i >= 0) return i;\n        if (path.len <= 1) return 0;'),
    ('fs/tree.c', '        if (i >= 0 || path.len <= 1) return i;', '        if (i >= 0 || path.len <= 2) return i;'),
    ('fs/tree.c', '        path = up.len ? up : S("/");', '        path = up;'),
    ('fs/tree.c', '        int i = tree_find(t, path);\n        if (i >= 0 || path.len <= 1) return i;', '        int i = tree_find(t, path);\n        return i;'),
]
MUTANTS += [
    # the Trash place's icon, full or empty (2026-10-30)
    (PL, '        if (trash_full) names[n++] = S("user-trash-full");\n', ''),
    (PL, '        if (trash_full) names[n++] = S("user-trash-full");\n', '        names[n++] = S("user-trash-full");\n'),
    ('fs/icons.c', ' || str_eq(context, S("Status"));', ';'),
]

# ---- unmounted drives, mounted by a click (2026-10-30)
SP = 'fs/spawn.c'
MUTANTS += [
    (PL, '        if (o + 1 >= cap) return false;\n        out[o++] = c;\n    }\n    out[o] = 0;\n    return true;\n}\n\n// A small sysfs', '        out[o++] = c;\n    }\n    out[o] = 0;\n    return true;\n}\n\n// A small sysfs'),
    (PL, "s.p[i + 1] == 'x' && (hi", "(hi"),
    (PL, '            c = (char)(hi * 16 + lo), i += 3;', '            c = (char)(hi * 16 + lo), i += 2;'),
    (PL, " && line.p[2 + kn] == '=')", ")"),
    (PL, '    while (empty && (e = readdir(d))) empty = strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0;', '    while (empty && (e = readdir(d))) empty = strcmp(e->d_name, ".") == 0;'),
    (PL, ' && octal_decode(source, src, sizeof src) && strcmp(src, dev) == 0)', ' && octal_decode(source, src, sizeof src))'),
    (PL, '    qsort(names, (size_t)nn, sizeof names[0], (int (*)(const void *, const void *))strcmp);   // sda1 before sdb1\n', ''),
    (PL, 'i64 size = sectors.len ? strtoll(arena_cstr(scratch, sectors), NULL, 10) * 512 : 0;', 'i64 size = sectors.len ? strtoll(arena_cstr(scratch, sectors), NULL, 10) : 0;'),
    (PL, 'str_eq(sys_line(scratch, sysblock, name, "removable"), S("1")) || ', ''),
    (PL, ' || str_eq(sys_line(scratch, sysblock, name, "../removable"), S("1"))', ''),
    (PL, ' ||\n                         str_eq(udev_get(data, "ID_BUS"), S("usb"));', ';'),
    (PL, 'bool wanted = size > 0 && ', 'bool wanted = '),
    (PL, '&& str_eq(udev_get(data, "ID_FS_USAGE"), S("filesystem")) ', ''),
    (PL, ' && !str_eq(udev_get(data, "UDISKS_IGNORE"), S("1"))', ''),
    (PL, '                      removable && dir_empty(path) && !mounted_dev(mountinfo, dev);', '                      removable && !mounted_dev(mountinfo, dev);'),
    (PL, '                      removable && dir_empty(path) && !mounted_dev(mountinfo, dev);', '                      removable && dir_empty(path);'),
    (PL, '                      removable && dir_empty(path) && !mounted_dev(mountinfo, dev);', '                      dir_empty(path) && !mounted_dev(mountinfo, dev);'),
    (PL, 'if (wanted && !udev_unescape(udev_get(data, "ID_FS_LABEL_ENC"), label, sizeof label)) label[0] = 0;', 'if (wanted && !udev_unescape(udev_get(data, "ID_FS_LABEL"), label, sizeof label)) label[0] = 0;'),
    (PL, 'size >= 10000000000 ? "%.0f GB Drive" : "%.1f GB Drive"', '"%.0f GB Drive"'),
    (PL, '    if (sysblock && udev) places_add_unmounted(scratch, pl, sysblock, udev, mi);\n', ''),
    (PL, '    if (sysblock && udev) places_add_unmounted', '    if (udev) places_add_unmounted'),
    # (dropped, step 8 A: removing places_current's Trash skip is equivalent: the line above returns Trash for the
    # trash view, and trash:/// holds no real path. Removing the skip instead made that line the redundant one.)
    # (the trailing-dot strip of 01f20c3: replaced by a stat check below, review, 2026-10-30)
    (SP, "            if (p.len && p.p[0] == '/' && (size_t)p.len < atcap)", "            if (p.len && (size_t)p.len < atcap)"),
    (SP, "            if (p.len && p.p[0] == '/' && (size_t)p.len < atcap)", "            if (p.len && p.p[0] == '/')"),
    (SP, '        if (line.len && !why[0]) snprintf(why, whycap, "%.*s", STR_ARG(line));\n    }\n    return false;', '    }\n    return false;'),
    ('ui/menus.c', '    if (k == UI_CTX_BRINGS) return it == UM_MOUNT_IT;   // no folder yet (2026-10-30)\n', ''),   # (retargeted, step F)
]
MUTANTS += [
    # the review's LOWs on 01f20c3 (2026-10-30)
    (PL, '        for (int k = 0; k < COUNT(NOT) && !skip; k++) skip = strncmp(e->d_name, NOT[k], strlen(NOT[k])) == 0;\n', ''),
    (SP, "                if (n > 1 && at[n - 1] == '.' && stat(at, &st) != 0) {", "                if (n > 1 && at[n - 1] == '.') {"),
    (SP, "                    if (stat(at, &st) != 0 || !S_ISDIR(st.st_mode)) at[n - 1] = '.';", ''),
    ('fs/guard.c', '    if (prog_resolve(prog, full, sizeof full) && realpath(full, r) && path_below(r, ops_root)) return true;', '    if (prog_resolve(prog, full, sizeof full) && path_below(full, ops_root)) return true;'),
    ('fs/guard.c', '    if (prog_resolve(prog, full, sizeof full) && realpath(full, r) && path_below(r, ops_root)) return true;', '    if (prog_resolve(prog, full, sizeof full)) return true;'),
    ('fs/guard.c', '    if (!ops_root[0]) return true;\n    char full[PATH_MAX], r[PATH_MAX];', '    char full[PATH_MAX], r[PATH_MAX];'),
]
MUTANTS += [
    # sysfs's 4096-byte sizes (2026-10-30): read_file in sys_line's place gives nothing
    (PL, '    int fd = open(path, O_RDONLY | O_CLOEXEC);\n    if (fd < 0) return STR_EMPTY;\n    ssize_t n = read(fd, buf, sizeof buf - 1);\n    close(fd);\n    if (n <= 0) return STR_EMPTY;\n    isize at = 0;\n    Str line = STR_EMPTY;\n    next_line((Str){ buf, n }, &at, &line);',
         '    (void)buf;\n    isize at = 0;\n    Str line = STR_EMPTY;\n    next_line(read_file(a, path), &at, &line);'),
]
MUTANTS += [
    # Unmount (2026-10-30)
    ('fs/spawn.c', '    udisks_sh_argv("\\"$2\\" unmount -b \\"$1\\" && echo OFX-UNMOUNTED", dev, udisks, argv);', '    udisks_sh_argv("\\"$2\\" unmount -b \\"$1\\" && echo OFX-UNMOUNTED && \\"$2\\" power-off -b \\"$1\\"", dev, udisks, argv);'),
    ('ui/menus.c', '    case UM_UNMOUNT_IT: return k == UI_CTX_DRIVE || k == UI_CTX_SHARE;', '    case UM_UNMOUNT_IT: return k == UI_CTX_SHARE;'),   # (retargeted, step F)
]

# ---- F10 step 1: ofx.conf (2026-10-30)
CF = 'fs/conf.c'
MUTANTS += [
    (CF, '    if (i == 0 || (i < v.len && is_digit(v.p[i]))) return false;', '    if (i < v.len && is_digit(v.p[i])) return false;'),
    (CF, '{ "KiB", (i64)1 << 10 }', '{ "KiB", 1000 }'),
    (CF, '{ "MiB", (i64)1 << 20 }', '{ "MiB", 1000000 }'),
    (CF, '{ "GB", 1000000000 }', '{ "GB", (i64)1 << 30 }'),
    (CF, 'return n <= INT64_MAX / UNITS[k].mul && (*out = n * UNITS[k].mul, true);', 'return n <= INT64_MAX / UNITS[k].mul && (*out = n, true);'),
    (CF, '    if (str_eq(v, S("on")) || str_eq(v, S("yes")) || str_eq(v, S("true"))) return *out = true, true;', '    if (str_eq(v, S("on")) || str_eq(v, S("yes"))) return *out = true, true;'),
    (CF, '    if (str_eq(v, S("off")) || str_eq(v, S("no")) || str_eq(v, S("false"))) return *out = false, true;', '    if (str_eq(v, S("off")) || str_eq(v, S("no"))) return *out = false, true;'),
    (CF, '.preview_max = CONF_PREVIEW_MAX, .date = DATE_SHORT,', '.preview_max = CONF_PREVIEW_MAX, .date = DATE_LONG,'),
    (CF, '#define CONF_PREVIEW_MAX ((i64)10 << 20)', '#define CONF_PREVIEW_MAX ((i64)10 << 19)'),
    (CF, '#define CLEAN_CAP        ((i64)1 << 30)', '#define CLEAN_CAP        ((i64)1 << 29)'),
    (CF, 'static const SearchSkipList SEARCH_SKIP_DEFAULT = { { ".git", "node_modules" }, 2 };', 'static const SearchSkipList SEARCH_SKIP_DEFAULT = { { ".git", "node_modules" }, 1 };'),
    (CF, '        if (str_find(v, \'%\') < 0 || v.len >= CONF_DATE_MAX) return false;', '        if (v.len >= CONF_DATE_MAX) return false;'),
    (CF, " || str_find(v, '/') >= 0", ''),
    (CF, ' || str_eq(v, S(".")) || str_eq(v, S(".."))', ''),
    (CF, '        if (l->n == CONF_SKIP_MAX || v.len >= CONF_NAME_MAX) return *full = true, false;\n', ''),
    (CF, "        if (line.len == 0 || line.p[0] == '#') continue;\n        Str key", '        if (line.len == 0) continue;\n        Str key'),
    # Not a mutant: `eq >= 0 &&` dropped (without an = the value is empty, which no option takes).
    (CF, '        if (c->bad) continue;\n', ''),
    (CF, '        int k = snprintf(out + *at, cap - *at, n ? "# %.*s\\n" : "#\\n", n, p);', '        int k = snprintf(out + *at, cap - *at, "# %.*s\\n", n, p);'),
    (CF, '            while (o->kind == CK_NAMES && e < def.len && def.p[e] != \' \') e++;', '            while (o->kind == CK_NAMES && e < def.len) e++;'),
    (CF, '    return at < cap ? at : cap - 1;', '    return at;'),
    (CF, '    if (lstat(path, &st) == 0) return true;\n', ''),
    (CF, '    if (!ops_allowed(path, err)) return false;\n    char d', '    char d'),
    (CF, '    if (!path_parent(d, sizeof d, path) || !mkdir_p(d, 0700, true, err)) return false;', '    if (!path_parent(d, sizeof d, path)) return false;'),
    ('ui/helpers.c', '    if (f != DATE_SHORT) {', '    if (f == DATE_PATTERN) {'),
    ('ui/helpers.c', '        if (!strftime(buf, cap, p, &tm)) snprintf(buf, cap, "?");', '        strftime(buf, cap, p, &tm);'),
    ('ui/helpers.c', 'f == DATE_ISO ? "%Y-%m-%dT%H:%M:%S" : pattern;', 'f == DATE_ISO ? "%Y-%m-%d %H:%M:%S" : pattern;'),
    ('fs/search.c', '    const SearchSkipList *l = w->skips;', '    const SearchSkipList *l = &SEARCH_SKIP_DEFAULT;'),
    ('ui/menus.c', '    case UM_PREFERENCES: return (UiAction){ UA_PREFERENCES, 0, false };\n', ''),
    ('fs/thumb.c', 'file_read_at(a, AT_FDCWD, q->path, q->src_max, &st, 0)', 'file_read_at(a, AT_FDCWD, q->path, (i64)10 << 20, &st, 0)'),
]
MUTANTS += [
    # the review's LOWs on ab1ad1d (2026-10-30)
    (CF, 'return n <= INT64_MAX / UNITS[k].mul && (*out = n * UNITS[k].mul, true);', 'return (*out = n * UNITS[k].mul, true);'),
]
MUTANTS += [
    # search-skip is exactly the file's lines; the file written with them (Michael, 2026-11-01)
    (CF, '    conf_defaults(c);\n    c->skip.n = 0;', '    conf_defaults(c);'),
    (CF, 'o->kind == CK_NAMES ? "" : "#", o->key', '"#", o->key'),
]
MUTANTS += [
    # F10 step 2: ofx-clean's options, the editor lines, conf_load (2026-11-01)
    (CF, '.skip = SEARCH_SKIP_DEFAULT, .clean = true,', '.skip = SEARCH_SKIP_DEFAULT, .clean = false,'),
    (CF, '                 .clean_orphans = true,', '                 .clean_orphans = false,'),
    (CF, '.clean_unused_days = CLEAN_UNUSED_S / 86400,', '.clean_unused_days = CLEAN_UNUSED_S / 86400 + 1,'),
    (CF, '.clean_cap = CLEAN_CAP };', '.clean_cap = CLEAN_CAP / 2 };'),
    (CF, '{ "clean", CK_BOOL, offsetof(Conf, clean),', '{ "clean", CK_BOOL, offsetof(Conf, clean_orphans),'),
    (CF, '{ "clean-orphans", CK_BOOL, offsetof(Conf, clean_orphans),', '{ "clean-orphans", CK_BOOL, offsetof(Conf, clean),'),
    (CF, '{ "clean-cap", CK_SIZE, offsetof(Conf, clean_cap),', '{ "clean-cap", CK_SIZE, offsetof(Conf, preview_max),'),
    (CF, '{ "preview-max-size", CK_SIZE, offsetof(Conf, preview_max),', '{ "preview-max-size", CK_SIZE, offsetof(Conf, clean_cap),'),
    (CF, '{ "previews", CK_BOOL, offsetof(Conf, previews),', '{ "previews", CK_BOOL, offsetof(Conf, clean),'),
    (CF, '    case CK_BOOL: return conf_bool(v, f);', '    case CK_BOOL: return false;'),
    ('fs/conf.c', '        return n >= 0 && (*(i64 *)f = n, true);', '        return (*(i64 *)f = n, true);'),
    ('fs/conf.c', '        i64 n = str_num(v, CONF_DAYS_MAX);', '        i64 n = str_num(v, CONF_DAYS_MAX + 1);'),
    (CF, '        if (v.len >= CONF_LINE_MAX || c->neditor == EDITORS_USER) return *full = true, false;', '        if (v.len > CONF_LINE_MAX || c->neditor == EDITORS_USER) return *full = true, false;'),
    (CF, '        if (v.len >= CONF_LINE_MAX || c->neditor == EDITORS_USER) return *full = true, false;', '        if (v.len >= CONF_LINE_MAX) return *full = true, false;'),
    (CF, '        if (one.n != 1) return false;\n', ''),
    (CF, '    if (there) conf_parse(read_file(a, path), c);\n    else conf_defaults(c);', '    if (there) conf_parse(read_file(a, path), c);\n    else conf_parse(STR_EMPTY, c);'),
    (CF, ' && S_ISREG(st.st_mode) && access(path, R_OK) == 0;', ' && access(path, R_OK) == 0;'),
    (CL, '    o->unused_s = c->clean_unused_days * 86400, o->orphans = c->clean_orphans, o->cap = c->clean_cap;', '    o->unused_s = c->clean_unused_days * 86400, o->orphans = true, o->cap = c->clean_cap;'),
    (CL, '    o->unused_s = c->clean_unused_days * 86400, o->orphans = c->clean_orphans, o->cap = c->clean_cap;', '    o->unused_s = c->clean_unused_days * 86400, o->orphans = c->clean_orphans, o->cap = CLEAN_CAP;'),
    (CL, '    o->unused_s = c->clean_unused_days * 86400, o->orphans = c->clean_orphans, o->cap = c->clean_cap;', '    o->unused_s = CLEAN_UNUSED_S, o->orphans = c->clean_orphans, o->cap = c->clean_cap;'),
    (CL, '                    : o->unused_s > 0 && o->now - f->atime > o->unused_s ? CR_UNUSED', '                    : o->now - f->atime > o->unused_s ? CR_UNUSED'),
    (CL, '                    : o->orphans ? clean_judge(a, &ms, fds[f->dir], f->name) : CR_KEEP;', '                    : clean_judge(a, &ms, fds[f->dir], f->name);'),
    (CL, '    if (o->cap > 0 && total > o->cap) {', '    if (total > o->cap) {'),
    ('fs/editors.c', '    *out = (Editors){ 0 };\n    editors_add', '    editors_add'),
]
MUTANTS += [
    # the review's LOW on 87570e6: a word pushes its own bytes, not the rest of its line
    ('fs/editors.c', '        char *out = arena_try_push(a, len + 1, 1);', '        char *out = arena_try_push(a, line.len - i + 1, 1);'),
    # Not a mutant: `else len++` as `len++` (quotes counted too: two bytes more pushed, nothing seen).
]
MUTANTS += [
    # F10 review step 4: every launch through the test guard (2026-11-01)
    ('fs/spawn.c', '    if (!ops_program_allowed(check ? check : full, err)) return false;   // (the file found, not a second search)\n', '    (void)check;\n'),
    ('fs/guard.c', '    return strchr(prog, \'/\') ? snprintf(out, cap, "%s", prog) < (int)cap && exec_file(out) : path_find(prog, out, cap);', '    return path_find(prog, out, cap);'),
]
# ---- F11 step 1: per-folder view, zoom, sort; Show Hidden; the cursor; housekeeping (2026-11-01)
FO = 'fs/folders.c'
MUTANTS += [
    (FO, '        if (str_eq(w[2], S("icons"))) f->view = 1;', '        if (str_eq(w[2], S("icons"))) f->view = 0;'),
    (FO, '        if (str_eq(w[5], S("on"))) f->rev = 1;', '        if (str_eq(w[5], S("on"))) f->rev = 0;'),
    (FO, '        if (str_eq(w[5], S("off"))) f->rev = 0;\n', ''),
    (FO, " && sel[0] && !strchr(sel, '/')) f->sel", ' && sel[0]) f->sel'),
    (FO, '    return f->view >= 0 || f->zoom > 0 || f->sort >= 0 || f->rev >= 0 || f->sel;', '    return f->view >= 0 || f->zoom > 0 || f->sort >= 0 || f->rev >= 0;'),
    (FO, '    return f->view >= 0 || f->zoom > 0 || f->sort >= 0 || f->rev >= 0 || f->sel;', '    return f->view >= 0 || f->zoom > 0 || f->sort >= 0 || f->sel;'),
    (FO, ' || !clean_real(ms, path, real, sizeof real) || clean_absent_drive(ms, real)) return false;', ' || !clean_real(ms, path, real, sizeof real)) return false;'),
    (FO, "    if (path[0] != '/' || !clean_real(ms, path, real, sizeof real) ||", "    if (path[0] != '/' ||"),
    (FO, "    if (path[0] != '/' || !clean_real(", "    if (!clean_real("),
    (FO, '    return stat(path, &st) != 0 && (errno == ENOENT || errno == ENOTDIR);', '    return stat(path, &st) != 0;'),
    (FO, '    return a < b ? 1 : a > b ? -1 : 0;   // the most recent first', '    return a < b ? -1 : a > b ? 1 : 0;   // the most recent first'),
    (FO, '        if (folder_says(&t->f[i]) && !(ms && folder_gone(ms, t->f[i].path))) t->f[k++] = t->f[i];', '        if (!(ms && folder_gone(ms, t->f[i].path))) t->f[k++] = t->f[i];'),
    (FO, '        if (folder_says(&t->f[i]) && !(ms && folder_gone(ms, t->f[i].path))) t->f[k++] = t->f[i];', '        if (folder_says(&t->f[i])) t->f[k++] = t->f[i];'),
    (FO, '    if (t->n > FOLDERS_MAX) {', '    if (t->n > FOLDERS_MAX + 1) {'),
    # Not a mutant: `if (!m->changed) continue;` dropped (an unchanged entry merges no field; one the
    # file lacks is made empty and tidied out).

    (FO, '        if (m->changed & FP_VIEW) f->view = m->view;\n', ''),
    (FO, '        if (m->changed & FP_SORT) f->sort = m->sort;\n', ''),
    (FO, '        if (m->changed & FP_SEL) f->sel = m->sel ? folder_str(a, str_from_c(m->sel)) : NULL;\n', ''),
    (FO, '        if ((m->changed & FP_AT) && m->at > f->at) f->at = m->at;', '        if (m->changed & FP_AT) f->at = m->at;'),
    (FO, '        if (mask & FP_VIEW) f->view = -1;\n', ''),
    (FO, '        f->changed |= mask, n++;', '        n++;'),
    (FO, '        if (!had) continue;\n', ''),
    (CF, "        if (str_eq(v, S(\"list\")) || str_eq(v, S(\"icons\"))) return *(bool *)f = str_eq(v, S(\"icons\")), true;", "        if (str_eq(v, S(\"list\")) || str_eq(v, S(\"icons\"))) return *(bool *)f = true, true;"),
    (CF, '            if (n == CONF_ZOOMS[i]) return *(int *)f = (int)n, true;', '            if (n >= 32) return *(int *)f = (int)n, true;'),
    (CF, '    *c = (Conf){ .zoom = CONF_ZOOM, .sort = SORT_NAME,', '    *c = (Conf){ .zoom = 64, .sort = SORT_NAME,'),
    ('fs/state.c', '        if (str_eq(key, S("hidden"))) s.hidden = str_eq(val, S("on"));\n', ''),
    ('fs/state.c', '        if (str_eq(key, S("folderdefaults")) && val.len < (isize)sizeof s.folder_defaults)', '        if (str_eq(key, S("folderdefaults")))'),
]
# ---- F11 step 2: the tree's open folders (tree=), state_value (2026-11-01)
MUTANTS += [
    ('fs/tree.c', "        if (i && n + 1 < cap) out[n++] = ' ', out[n] = 0;", "        if (n + 1 < cap) out[n++] = ' ', out[n] = 0;"),
    ('fs/tree.c', '        if (!percent_encode(str_from_c(t->open[i]), "/", out + n, cap - n)) return false;', '        percent_encode(str_from_c(t->open[i]), "/", out + n, cap - n);'),
    ('fs/tree.c', "        if (at > b && percent_decode(str_cut(text, b, at), p, sizeof p) && p[0] == '/') tree_open(t, str_from_c(p));", "        if (at > b && percent_decode(str_cut(text, b, at), p, sizeof p)) tree_open(t, str_from_c(p));"),
    ('fs/tree.c', "        while (at < text.len && text.p[at] == ' ') at++;\n        isize b", '        isize b'),
]
# the review's LOWs on F11 step 1 (2026-11-01): a partial read not written over; the lock; autofs
MUTANTS += [
    ('fs/state.c', '    if (text->len == 0 && stat(path, &st) == 0 && st.st_size > 0) return err_set', '    if (false) return err_set'),
    ('fs/state.c', '    if (fd >= 0 && flock(fd, LOCK_EX) != 0) close(fd), fd = -1;', '    if (fd >= 0 && 0) close(fd), fd = -1;'),
    ('fs/paths.c', '"lustre", "fuse", "autofs" };', '"lustre", "fuse" };'),   # (retargeted, step C)
]
MUTANTS += [
    # the review's LOW on f3092a0: tree='s room from the paths themselves
    ('fs/tree.c', '    for (int i = 0; i < t->nopen; i++) cap += 3 * strlen(t->open[i]) + 1;', '    for (int i = 0; i < t->nopen; i++) cap += strlen(t->open[i]) + 1;'),
    ('fs/tree.c', '    for (int i = 0; i < t->nopen; i++) cap += 3 * strlen(t->open[i]) + 1;', '    for (int i = 0; i < t->nopen; i++) cap += 3 * strlen(t->open[i]);'),
]
# ---- F11 step 3: the window lines (fs/session.c), restore-tabs, the question in ui/ (2026-11-01)
SE = 'fs/session.c'
MUTANTS += [
    (SE, '    if (id <= 0 || when < 0 || shown < 0) return false;', '    if (id <= 0 || when < 0) return false;'),
    (SE, '    if (id <= 0 || when < 0 || shown < 0) return false;', '    if (when < 0 || shown < 0) return false;'),
    (SE, '!str_starts_with(str_from_c(p), S(TRASH_URI))', '!str_starts_with(str_from_c(p), S(SEARCH_URI))'),
    (SE, '    if (w->shown >= w->n) w->shown = 0;\n', ''),
    (SE, '    return w->n > 0;', '    return true;'),
    # Not a mutant: percent_encode's false ignored in session_line (a folder cut short there leaves k
    # past cap - 1, which the check after the loop refuses).
    (SE, '    if (k <= 0 || (size_t)k + 1 >= cap) return 0;', '    if (k <= 0) return 0;'),
    (SE, ' && !session_alive(&w, boot) && (w.at > at || (w.at == at && here > self))) newer++;', ' && (w.at > at || (w.at == at && here > self))) newer++;'),
    (SE, '    return w->open && boot && strcmp(w->live.boot, boot) == 0 && win_started((pid_t)w->id) == w->live.start;', '    return w->open && boot && strcmp(w->live.boot, boot) == 0;'),
    (SE, '    return w->open && boot && strcmp(w->live.boot, boot) == 0 && win_started((pid_t)w->id) == w->live.start;', '    return w->open && boot && win_started((pid_t)w->id) == w->live.start;'),
    (SE, '    return me && w->open && w->id == id && strcmp(w->live.boot, me->boot) == 0 && w->live.start == me->start;', '    return me && w->open && w->id == id;'),
    # Not a mutant: `else if (!str_eq(how, "closed"))` as `else` ("closed" read again as a first
    # folder is skipped: not an absolute path).
    (SE, '        if (c < 1 || c >= (isize)sizeof w->live.boot || start < 0) return false;', '        if (start < 0) return false;'),
    (SE, '    return newer < SESSION_KEEP;', '    return newer <= SESSION_KEEP;'),
    (SE, '    int newer = extra && extra_at >= at;', '    int newer = 0;'),
    (SE, '    int newer = extra && extra_at >= at;', '    int newer = extra && extra_at > at;'),
    (SE, ' && (w.at > at || (w.at == at && here > self))) newer++;', ' && w.at > at) newer++;'),




    (CF, '{ "restore-tabs", CK_BOOL, offsetof(Conf, restore_tabs), "off",', '{ "restore-tabs", CK_BOOL, offsetof(Conf, sort_rev), "off",'),
    ('ui/ui.c', '    if (u->restore_n > 0) return UI_ON_RESTORE;\n', ''),
    ('ui/prompts.c', 'u->restore_n = 0; return (UiAction){ UA_RESTORE_WINDOWS, 0, yes };', 'u->restore_n = 0; return (UiAction){ UA_RESTORE_WINDOWS, 0, true };'),
    ('ui/prompts.c', ' || on == UI_ON_RESTORE || on == UI_ON_CLEAR || on == UI_ON_AIRPLANE;', ' || on == UI_ON_CLEAR || on == UI_ON_AIRPLANE;'),
]
MUTANTS += [
    # F11 milestone review, step 1 (2026-11-02)
    ('fs/clean.c', '    return !ms->unknown && path_walk(', '    return path_walk('),   # (retargeted, step C)
    ('fs/paths.c', '        if (!p) return (Mounts){ NULL, 0, true };', '        if (!p) return (Mounts){ e, n, false };'),   # (retargeted, step C)
    (SE, '        const char *s = a ? folder_str(a, str_from_c(p)) : "";', '        const char *s = "";'),
]
MUTANTS += [
    # F11 milestone review, step 2: the guard's gaps (2026-11-02)
    ('fs/spawn.c', '    if (!ops_program_allowed(check ? check : full, err)) return false;   // (the file found, not a second search)\n', '    if (!ops_program_allowed(full, err)) return false;\n'),
    ('fs/trash.c', ' || !ops_allowed(t->file, err) || !ops_allowed(t->info, err)) return false;', ') return false;'),
    # Not a mutant: state_write's O_EXCL | O_NOFOLLOW as O_TRUNC (the unlink before it takes a link
    # out; only a race between the two would reach one).
    ('fs/paths.c', '    unlink(tmp);\n    int fd = open(tmp', '    int fd = open(tmp'),
]
MUTANTS += [
    # F11 milestone review, step 3: one rewrite for the state file's writers (2026-11-02)
    ('fs/state.c', '    bool ok = state_read(a, path, &text, err) && (!e->prepare || e->prepare(e->ctx, a, text, &room, err));', '    bool ok = (state_read(a, path, &text, err), true) && (!e->prepare || e->prepare(e->ctx, a, text, &room, err));'),
    ('fs/state.c', '    bool ok = state_read(a, path, &text, err) && (!e->prepare || e->prepare(e->ctx, a, text, &room, err));', '    bool ok = state_read(a, path, &text, err);'),
    ('fs/state.c', '    int lock = state_lock(path);\n    bool ok = state_read(a', '    int lock = -1;\n    bool ok = state_read(a'),
    ('fs/state.c', '        if (!line.len || (e->keep && !e->keep(e->ctx, text, line, &out))) continue;', '        if (!line.len) continue;'),
    ('fs/state.c', '    isize t = ok && e->tail ? e->tail(e->ctx, body + n, text.len + room + 2 - n) : 0;', '    isize t = 0;'),
    ('fs/state.c', '    if (p->put) return false;   // (a key said twice: once)', '    if (p->put) return true;'),
    ('fs/state.c', '    if (p->put) return 0;\n    if (p->line.len + 1 > cap)', '    if (p->line.len + 1 > cap)'),
    # F11 milestone cleanup, step 4: a rewrite past its room fails (2026-11-02)
    ('fs/state.c', '            err_set(err, "%s: too big", path), ok = false;\n            break;', '            break;'),
    ('fs/state.c', '    if (t < 0) err_set(err, "%s: too big", path), ok = false;', ''),
    # F11 milestone cleanup, step 7: one panel at most (2026-11-02)
    ('ui/ui.c', '    if (u->panel == p) u->panel = UI_PANEL_NONE;', '    u->panel = UI_PANEL_NONE;'),
    # Not mutants: the three tails' -1 (state_put's, session_edit's, folders_write's): each writer's
    # room covers its own tail, so no test can reach it; the check above is tested with a tail of its own.
    # Not mutants: state_write's fsync dropped, and session_edit's live writes synced too (only a power
    # cut, or a stall behind a big write, shows either); session_edit_tail's room check (its room is
    # that line's: never short).
    ('fs/folders.c', '        if (!folder_line_read(line, w, path, sizeof path) || folders_find(t, path) >= 0) continue;', '        if (!folder_line_read(line, w, path, sizeof path)) continue;'),
    ('fs/folders.c', '    return w[6].len && percent_decode(w[0], path, cap) && (path[0] == \'/\' || strcmp(path, TRASH_URI) == 0);', '    return w[6].len && percent_decode(w[0], path, cap) && path[0] == \'/\';'),
    ('fs/folders.c', '    return w[6].len && percent_decode(w[0], path, cap) && (path[0] == \'/\' || strcmp(path, TRASH_URI) == 0);', '    return percent_decode(w[0], path, cap) && (path[0] == \'/\' || strcmp(path, TRASH_URI) == 0);'),
    ('fs/folders.c', '    return !folder_line_read(line, w, path, sizeof path);   // (the folder lines', '    return !str_starts_with(line, S("folder "));   // (the folder lines'),
    ('fs/folders.c', '    *mine = folders_new;\n', ''),
    ('fs/session.c', '    return !(session_parse_line(NULL, l, &w) &&\n             (session_mine(&w, e->id, e->me) ||', '    return !(session_parse_line(NULL, l, &w) &&\n             (false ||'),
    ('fs/session.c', ' || (e->key && w.id == e->key->id && w.at == e->key->at) ||', ' || (e->key && w.id == e->key->id) ||'),
    ('fs/session.c', '              (!session_alive(&w, e->boot) && !session_kept(', '              (!session_kept('),
    ('fs/session.c', ' && !e->mine.open;   // (a running', ';   // (a running'),
    ('fs/session.c', '(t->id ? c.id == t->id && (!t->found || c.at > t->w->at) : !t->found || c.at > t->w->at)', '(t->id ? c.id == t->id : !t->found || c.at > t->w->at)'),
    ('fs/session.c', '(t->id ? c.id == t->id && (!t->found || c.at > t->w->at) : !t->found || c.at > t->w->at)', '(t->id ? c.id == t->id && (!t->found || c.at > t->w->at) : !t->found)'),
    ('fs/session.c', 'if (session_parse_line(NULL, l, &c) && !session_alive(&c, boot) && (t->id ?', 'if (session_parse_line(NULL, l, &c) && (t->id ?'),
    ('fs/session.c', '        t->found = session_parse_line(a, str_cut(text, t->pick, e), t->w);', '        t->found = true;'),
    ('fs/session.c', '        if ((isize)(l.p - text.p) != t->pick && *t->nrest < t->cap && session_parse_line(NULL, l, &c) && !session_alive(&c, boot))', '        if (*t->nrest < t->cap && session_parse_line(NULL, l, &c) && !session_alive(&c, boot))'),
    ('fs/session.c', '        if ((isize)(l.p - text.p) != t->pick && *t->nrest < t->cap && session_parse_line(NULL, l, &c) && !session_alive(&c, boot))', '        if ((isize)(l.p - text.p) != t->pick && session_parse_line(NULL, l, &c) && !session_alive(&c, boot))'),
    ('fs/session.c', '            t->rest[(*t->nrest)++] = (SessionKey){ c.id, c.at, c.open };', '            t->rest[(*t->nrest)++] = (SessionKey){ c.id, c.at, false };'),
    ('fs/session.c', '    return (isize)(l.p - text.p) != ((SessionTake *)ctx)->pick;', '    return true;'),
    # ---- the system's thumbnailers (F12 step 3)
    ('fs/thumbers.c', '    if (!exec.len || exec.len >= THUMBER_EXEC_MAX || !types.len || t->n == THUMBERS_MAX) return;', '    if (!exec.len || exec.len >= THUMBER_EXEC_MAX || t->n == THUMBERS_MAX) return;'),
    ('fs/thumbers.c', '    if (!exec.len || exec.len >= THUMBER_EXEC_MAX || !types.len || t->n == THUMBERS_MAX) return;', '    if (exec.len >= THUMBER_EXEC_MAX || !types.len || t->n == THUMBERS_MAX) return;'),
    ('fs/thumbers.c', '    if (tryx.len) snprintf(prog, sizeof prog, "%.*s", STR_ARG(tryx));', '    if (false) snprintf(prog, sizeof prog, "%.*s", STR_ARG(tryx));'),
    ('fs/thumbers.c', "    if (prog[0] == '/' ? access(prog, X_OK) != 0 : !on_path(prog)) return;\n", ''),
    ('fs/thumbers.c', "    if (prog[0] == '/' ? access(prog, X_OK) != 0 : !on_path(prog)) return;", "    if (prog[0] == '/' ? false : !on_path(prog)) return;"),
    ('fs/thumbers.c', "    if (prog[0] == '/' ? access(prog, X_OK) != 0 : !on_path(prog)) return;", "    if (prog[0] == '/' ? access(prog, X_OK) != 0 : false) return;"),
    ('fs/thumbers.c', '        if (!ty.len || ty.len >= THUMBER_TYPE_MAX || t->ntypes == THUMBER_TYPES_MAX) continue;', '        if (ty.len >= THUMBER_TYPE_MAX || t->ntypes == THUMBER_TYPES_MAX) continue;'),
    ('fs/thumbers.c', '        if (have) continue;\n', ''),
    ('fs/thumbers.c', '        if (str_ends_with(str_from_c(de->d_name), S(".thumbnailer"))) names[n++]', '        names[n++]'),
    ('fs/thumbers.c', '    qsort(names, (size_t)n, sizeof names[0], thumbers_name_cmp);\n', ''),
    ('fs/thumbers.c', '    if (ops_root[0]) {\n        const char *d = getenv("OFX_TEST_THUMBNAILERS");', '    if (false) {\n        const char *d = getenv("OFX_TEST_THUMBNAILERS");'),
    ('fs/thumbers.c', '        if (str_eq(type, str_from_c(t->types[k].type))) return t->exec[t->types[k].at];', '        if (str_eq(type, str_from_c(t->types[k].type))) return t->exec[0];'),
    ('fs/thumbers.c', '    if (n <= 0 || n >= cap) return -1;', '    if (n < 0 || n >= cap) return -1;'),
    ('fs/thumbers.c', '    if (n <= 0 || n >= cap) return -1;', '    if (n <= 0 || n > cap) return -1;'),
    ('fs/thumbers.c', '            if (!put) return -1;\n', ''),
    ('fs/thumbers.c', "    case 'i': return path;", "    case 'i': return uri;"),
    ('fs/thumbers.c', "    case 'u': return uri;", "    case 'u': return path;"),
    ('fs/thumbers.c', "    case 'o': return out;", "    case 'o': return path;"),
    ('fs/thumbers.c', "    case 's': return size;", '    case \'s\': return "128";'),
    ('fs/thumbers.c', '    case \'%\': return "%";\n', ''),
    ('fs/thumbers.c', "            if (*p != '%') { len++; continue; }", "            if (*p != '%') { continue; }"),
    ('fs/sandbox.c', '    if (nargs >= BOX_ARGS_MAX || !ops_program_allowed(argv[0], &err)) {', '    if (nargs >= BOX_ARGS_MAX) {'),
    ('fs/sandbox.c', '    box_argv(j, box_sandbox ? bw : NULL, prog, av);', '    box_argv(j, bw, prog, av);'),
    ('fs/sandbox.c', '    box_argv(j, box_sandbox ? bw : NULL, prog, av);', '    box_argv(j, NULL, prog, av);'),
    ('fs/sandbox.c', '        if (!path_below(prog, "/usr")) av[n++] = "--ro-bind", av[n++] = prog, av[n++] = prog;', '        if (false) av[n++] = "--ro-bind", av[n++] = prog, av[n++] = prog;'),
    ('fs/sandbox.c', '        setpgid(0, 0);\n', ''),
    ('fs/sandbox.c', '    if (!ended) {\n        kill(-pid, SIGKILL);', '    if (false) {\n        kill(-pid, SIGKILL);'),
    ('fs/sandbox.c', '        kill(-pid, SIGKILL);   // (its group', '        kill(pid, SIGKILL);   // (its group'),
    ('fs/sandbox.c', '    return ended ? BOX_ENDED : BOX_KILLED;', '    return BOX_ENDED;'),
    (TH, '    if (!ops_allowed(dir, &err) || !mkdir_p(dir, 0700, false, &err) || mkdir(dir, 0700) != 0) return TG_NONE;', '    if (!mkdir_p(dir, 0700, false, &err) || mkdir(dir, 0700) != 0) return TG_NONE;'),
    ('fs/thumb.c', 'file_read_at(a, AT_FDCWD, out, THUMB_CACHED_MAX, &st, O_NOFOLLOW)', 'file_read_at(a, AT_FDCWD, out, THUMB_CACHED_MAX, &st, 0)'),
    (TH, 'png_decode(a, png, side, t) ? TG_MADE', 'png_decode(a, png, side * 2, t) ? TG_MADE'),
    (TH, '        if (stat(q->path, &st) != 0 || !S_ISREG(st.st_mode)) return TG_NONE;', '        if (stat(q->path, &st) != 0) return TG_NONE;'),
    (TH, '        if (stat(q->path, &st) != 0 || !S_ISREG(st.st_mode)) return TG_NONE;\n', ''),
    (TH, '    if (q->kind == TK_EXEC) {   // (the thumbnailer', '    if (false) {   // (the thumbnailer'),
    (TH, '    if (thumbers_argv(a, q->exec, q->path, uri, out, side, argv, COUNT(argv)) > 0) {', '    if (thumbers_argv(a, q->exec, q->path, uri, out, 128, argv, COUNT(argv)) > 0) {'),
    # ---- the F12 step 3 review: the environment, a session, the network; a timeout writes nothing; ofx-clean's run folders
    ('fs/sandbox.c', '            "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--unshare-all", "--new-session",   // (a /tmp of its own: hooks and filters expect one)', '            "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--new-session",   // (a /tmp of its own: hooks and filters expect one)'),
    ('fs/sandbox.c', '            "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--unshare-all", "--new-session",   // (a /tmp of its own: hooks and filters expect one)', '            "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--unshare-all",   // (a /tmp of its own: hooks and filters expect one)'),
    ('fs/sandbox.c', '        execve(av[0], (char *const *)av, (char *const *)envp);', '        execv(av[0], (char *const *)av);'),
    ('fs/sandbox.c', '        const char *v = i == COUNT(keep) - 1 ? "local" : getenv(keep[i]);', '        const char *v = getenv(keep[i]);'),
    ('fs/sandbox.c', '    bool ended = gone && !open_out;', '    bool ended = gone;'),
    (TH, '        if (g == TG_NONE) return TG_NONE;\n', ''),
    (TH, '        got = r != BOX_ENDED ? TG_NONE : png.len', '        got = r == BOX_NOT_RUN ? TG_NONE : png.len'),
    ('fs/clean.c', '    clean_runs(rfd, root, o, s);\n', ''),
    ('fs/clean.c', '            (!o->all && o->now - st.st_mtim.tv_sec <= CLEAN_TMP_S))', '            (!o->all && o->now - st.st_mtim.tv_sec < 0))'),
    ('fs/clean.c', '        if (!clean_run_name(e->d_name) || fstatat', '        if (fstatat'),
    ('fs/clean.c', '!S_ISDIR(st.st_mode) ||\n            (!o->all', 'false ||\n            (!o->all'),
    ('fs/clean.c', '        if (!o->dry && !delete_tree(path, &gone, &err)', '        if (!delete_tree(path, &gone, &err)'),
    ('fs/clean.c', '        s->n[CR_TEMP]++;\n        if (o->say) o->say(o->ctx, CR_TEMP, path, 0);', '        if (o->say) o->say(o->ctx, CR_TEMP, path, 0);'),
    ('fs/clean.c', "        if (p == d || p - d > 10 || *p != (part ? 0 : '-')) return false;", "        if (p == d || *p != (part ? 0 : '-')) return false;"),
    ('fs/clean.c', "        if (p == d || p - d > 10 || *p != (part ? 0 : '-')) return false;", "        if (p - d > 10 || *p != (part ? 0 : '-')) return false;"),
    ('fs/clean.c', '    if (strncmp(n, ".ofx-run-", 9) != 0) return false;\n', ''),
    ('fs/sandbox.c', '    if (box_sandbox && !path_find("bwrap", bw, sizeof bw)) {', '    if (false) {'),
    # ---- Clear Previews (F12 step 4)
    ('fs/clean.c', '        CleanRule r = o->all ? CR_ALL', '        CleanRule r = false ? CR_ALL'),
    ('fs/clean.c', '            (!o->all && o->now - st.st_mtim.tv_sec <= CLEAN_TMP_S))', '            (o->now - st.st_mtim.tv_sec <= CLEAN_TMP_S))'),
    ('ui/menus.c', '    case UM_CLEAR_PREVIEWS: return !u->clearing;', '    case UM_CLEAR_PREVIEWS: return true;'),
    ('ui/menus.c', '    case UM_CLEAR_PREVIEWS: return u->clear_asking = true, UI_NONE;', '    case UM_CLEAR_PREVIEWS: return UI_NONE;'),
    ('ui/prompts.c', '    case UI_ON_CLEAR: u->clear_asking = false; return (UiAction){ yes ? UA_CLEAR_PREVIEWS : UA_NONE, 0, false };', '    case UI_ON_CLEAR: u->clear_asking = false; return (UiAction){ UA_CLEAR_PREVIEWS, 0, false };'),
    ('ui/prompts.c', '    case UI_ON_CLEAR: u->clear_asking = false; return (UiAction){ yes ? UA_CLEAR_PREVIEWS : UA_NONE, 0, false };', '    case UI_ON_CLEAR: return (UiAction){ yes ? UA_CLEAR_PREVIEWS : UA_NONE, 0, false };'),
    ('ui/ui.c', '    if (u->clear_asking) return UI_ON_CLEAR;\n', ''),
    ('ui/prompts.c', ' || on == UI_ON_CLEAR || on == UI_ON_AIRPLANE;', ' || on == UI_ON_AIRPLANE;'),   # (F16 step 7d: the case list there is ui_is_question now)
    ('ui/ui.c', '    UM_VIEW_LIST, UM_VIEW_ICONS, UM_SEP, UM_CLEAR_PREVIEWS,\n};', '    UM_VIEW_LIST, UM_VIEW_ICONS,\n};'),
    (TH, '        if (got == TG_FAILED && !png.len && access(dir, F_OK) != 0) got = TG_NONE;', '        if (got == TG_FAILED && !png.len) got = TG_NONE;'),
    (TH, '        if (got == TG_FAILED && !png.len && access(dir, F_OK) != 0) got = TG_NONE;', '        if (false) got = TG_NONE;'),
    # ---- git status per file (F12 step 5), and the sandbox it runs in
    ('fs/git.c', "    static const char L[GS_N] = { [GS_UNTRACKED] = 'U', [GS_STAGED] = 'A', [GS_RENAMED] = 'R', [GS_MODIFIED] = 'M', [GS_CONFLICT] = '!' };", "    static const char L[GS_N] = { [GS_UNTRACKED] = '?', [GS_STAGED] = 'A', [GS_RENAMED] = 'R', [GS_MODIFIED] = 'M', [GS_CONFLICT] = '!' };"),
    ('fs/git.c', "    static const char L[GS_N] = { [GS_UNTRACKED] = 'U', [GS_STAGED] = 'A', [GS_RENAMED] = 'R', [GS_MODIFIED] = 'M', [GS_CONFLICT] = '!' };", "    static const char L[GS_N] = { [GS_UNTRACKED] = 'U', [GS_STAGED] = 'M', [GS_RENAMED] = 'R', [GS_MODIFIED] = 'M', [GS_CONFLICT] = '!' };"),
    ('fs/git.c', "    static const char L[GS_N] = { [GS_UNTRACKED] = 'U', [GS_STAGED] = 'A', [GS_RENAMED] = 'R', [GS_MODIFIED] = 'M', [GS_CONFLICT] = '!' };", "    static const char L[GS_N] = { [GS_UNTRACKED] = 'U', [GS_STAGED] = 'A', [GS_RENAMED] = 'A', [GS_MODIFIED] = 'M', [GS_CONFLICT] = '!' };"),
    ('fs/git.c', "    static const char L[GS_N] = { [GS_UNTRACKED] = 'U', [GS_STAGED] = 'A', [GS_RENAMED] = 'R', [GS_MODIFIED] = 'M', [GS_CONFLICT] = '!' };", "    static const char L[GS_N] = { [GS_UNTRACKED] = 'U', [GS_STAGED] = 'A', [GS_RENAMED] = 'R', [GS_MODIFIED] = '*', [GS_CONFLICT] = '!' };"),
    ('fs/git.c', "    static const char L[GS_N] = { [GS_UNTRACKED] = 'U', [GS_STAGED] = 'A', [GS_RENAMED] = 'R', [GS_MODIFIED] = 'M', [GS_CONFLICT] = '!' };", "    static const char L[GS_N] = { [GS_UNTRACKED] = 'U', [GS_STAGED] = 'A', [GS_RENAMED] = 'R', [GS_MODIFIED] = 'M', [GS_CONFLICT] = 'U' };"),
    ('fs/git.c', '        if (str_eq(path_base(s), S(".git"))) return false;\n', ''),
    ('fs/git.c', '(S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))', '(S_ISDIR(st.st_mode))'),
    ('fs/git.c', '    if (path.len && path.p[path.len - 1] == \'/\') path.len--;   // (a folder git names whole: "newdir/")\n', ''),
    ('fs/git.c', '    if (s == GS_IGNORED) return;\n', ''),
    ('fs/git.c', '        if (strmap_get(&g->under, d, &was) && was >= (int)s) continue;\n', ''),
    ('fs/git.c', '            if (str_eq(v, S("(detached)"))) g->head[0] = 0;', '            if (false) g->head[0] = 0;'),
    ('fs/git.c', '            g->has_ab = sscanf(l.p + 12, "+%d -%d", &g->ahead, &g->behind) == 2;', '            g->has_ab = true;'),
    ('fs/git.c', "    int fields = l.p[0] == '1' ? 8 : l.p[0] == '2' ? 9 : l.p[0] == 'u' ? 10 : l.p[0] == '?' || l.p[0] == '!' ? 1 : -1;", "    int fields = l.p[0] == '1' ? 7 : l.p[0] == '2' ? 9 : l.p[0] == 'u' ? 10 : l.p[0] == '?' || l.p[0] == '!' ? 1 : -1;"),
    ('fs/git.c', "    int fields = l.p[0] == '1' ? 8 : l.p[0] == '2' ? 9 : l.p[0] == 'u' ? 10 : l.p[0] == '?' || l.p[0] == '!' ? 1 : -1;", "    int fields = l.p[0] == '1' ? 8 : l.p[0] == '2' ? 8 : l.p[0] == 'u' ? 10 : l.p[0] == '?' || l.p[0] == '!' ? 1 : -1;"),
    ('fs/git.c', "    int fields = l.p[0] == '1' ? 8 : l.p[0] == '2' ? 9 : l.p[0] == 'u' ? 10 : l.p[0] == '?' || l.p[0] == '!' ? 1 : -1;", "    int fields = l.p[0] == '1' ? 8 : l.p[0] == '2' ? 9 : l.p[0] == 'u' ? 9 : l.p[0] == '?' || l.p[0] == '!' ? 1 : -1;"),
    ('fs/git.c', "    int fields = l.p[0] == '1' ? 8 : l.p[0] == '2' ? 9 : l.p[0] == 'u' ? 10 : l.p[0] == '?' || l.p[0] == '!' ? 1 : -1;", "    int fields = l.p[0] == '1' ? 8 : l.p[0] == '2' ? 9 : l.p[0] == 'u' ? 10 : l.p[0] == '?' || l.p[0] == '!' ? 2 : -1;"),
    ('fs/git.c', "    GitState s = l.p[0] == '?' ? GS_UNTRACKED : l.p[0] == '!' ? GS_IGNORED : l.p[0] == 'u' ? GS_CONFLICT : l.p[0] == '2' ? GS_RENAMED", "    GitState s = l.p[0] == '?' ? GS_UNTRACKED : l.p[0] == '!' ? GS_IGNORED : l.p[0] == 'u' ? GS_CONFLICT : false ? GS_RENAMED"),
    ('fs/git.c', "    GitState s = l.p[0] == '?' ? GS_UNTRACKED : l.p[0] == '!' ? GS_IGNORED : l.p[0] == 'u' ? GS_CONFLICT : l.p[0] == '2' ? GS_RENAMED", "    GitState s = l.p[0] == '?' ? GS_UNTRACKED : l.p[0] == '!' ? GS_IGNORED : false ? GS_CONFLICT : l.p[0] == '2' ? GS_RENAMED"),
    ('fs/git.c', "               : x == 'A' ? GS_STAGED : GS_MODIFIED;", '               : GS_MODIFIED;'),
    ('fs/git.c', "    if (l.p[0] == '2') *skip = true;\n", ''),
    ('fs/git.c', '    if (s != GS_IGNORED) g->changed++;', '    g->changed++;'),
    ('fs/git.c', '        if (strmap_get(&g->of, d, &s) && s == GS_IGNORED) return true;', '        if (strmap_get(&g->of, d, &s)) return true;'),
    ('fs/git.c', '    if (dir) strmap_get(&g->under, k, &under);\n', ''),
    ('fs/git.c', '    return (GitState)(own > under ? own : under);', '    return (GitState)own;'),
    ('fs/git.c', '    else j->ro[3] = top, j->ro[4] = common;', '    else j->ro[3] = top;'),
    ('fs/git.c', '    const char *argv[] = { git, "--no-optional-locks", "-c", "core.fsmonitor=false", "status", "--porcelain=v2", "-z", "--ignored", "--branch", "--", ".", NULL };', '    const char *argv[] = { git, "-c", "core.fsmonitor=false", "status", "--porcelain=v2", "-z", "--ignored", "--branch", "--", ".", NULL };'),
    ('fs/git.c', '    const char *argv[] = { git, "--no-optional-locks", "-c", "core.fsmonitor=false", "status", "--porcelain=v2", "-z", "--ignored", "--branch", "--", ".", NULL };', '    const char *argv[] = { git, "--no-optional-locks", "status", "--porcelain=v2", "-z", "--ignored", "--branch", "--", ".", NULL };'),
    ('fs/git.c', '    const char *argv[] = { git, "--no-optional-locks", "-c", "core.fsmonitor=false", "status", "--porcelain=v2", "-z", "--ignored", "--branch", "--", ".", NULL };', '    const char *argv[] = { git, "--no-optional-locks", "-c", "core.fsmonitor=false", "status", "--porcelain=v2", "-z", "--branch", "--", ".", NULL };'),
    ('fs/git.c', '    if (!j.out || box_run(&j) != BOX_ENDED || len == 0) return false;', '    if (!j.out || box_run(&j) != BOX_ENDED) return false;'),
    ('fs/sandbox.c', '        av[n++] = "--chdir", av[n++] = j->cwd ? j->cwd : "/";\n', ''),
    ('fs/sandbox.c', '        if (cwd && chdir(cwd) != 0) _exit(127);\n', ''),
    ('fs/sandbox.c', '        for (int i = 0; i < BOX_BINDS_MAX && j->ro[i]; i++) av[n++] = "--ro-bind-try", av[n++] = j->ro[i], av[n++] = j->ro[i];\n', ''),
    ('fs/sandbox.c', '        if (j->out) dup2(pipe_fd[1], 1);\n', ''),
    ('fs/sandbox.c', '        if (p[1].revents && !box_read(j, fd)) open_out = false;', '        if (p[1].revents) open_out = false;'),
    # F12 step 5b: the branch menu's list (fs/git.c), its ui/ entries, the timer's minimum
    ('fs/git.c', '        if (!str_starts_with(m, MOVE)) continue;\n', ''),
    ('fs/git.c', '        if (from > 0) {   // (the first line is cut: dropped)', '        if (false) {   // (the first line is cut: dropped)'),
    ('fs/git.c', '        off_t from = st.st_size > cap ? st.st_size - cap : 0;', '        off_t from = 0;'),
    ('fs/git.c', '    if (box_run(&j) != BOX_ENDED || len == 0) return false;\n    bool cut;', '    if (box_run(&j) != BOX_ENDED) return false;\n    bool cut;'),
    ('fs/git.c', '"for-each-ref", "--sort=-committerdate",', '"for-each-ref",'),
    ('fs/git.c', '    git_branches_merge(b, head, git_reflog_tail(gitdir, tail, sizeof tail), refs, cut);', '    git_branches_merge(b, head, STR_EMPTY, refs, cut);'),
    ('fs/git.c', '    git_gitdir(top, gitdir, sizeof gitdir);\n    char head[GIT_HEAD_MAX];', '    snprintf(gitdir, sizeof gitdir, "%s/.git", top);\n    char head[GIT_HEAD_MAX];'),
    ('ui/helpers.c', '        if (d[k] && (!due || d[k] < due)) due = d[k];', '        if (d[k] && !due) due = d[k];'),
    ('ui/helpers.c', '        if (d[k] && (!due || d[k] < due)) due = d[k];', '        if (!due || d[k] < due) due = d[k];'),
    ('fs/git.c', 'O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);   // (a FIFO', 'O_RDONLY | O_CLOEXEC | O_NOFOLLOW);   // (a FIFO'),
    ('fs/git.c', 'O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);   // (a FIFO', 'O_RDONLY | O_CLOEXEC | O_NONBLOCK);   // (a FIFO'),
    # F12 step 5c: switching (fs/git.c), BoxJob's second writable folder and error output, the keys
    ('fs/git.c', '''    if (!name[0] || name[0] == '-' || strlen(name) >= GIT_HEAD_MAX) return''', '''    if (!name[0] || strlen(name) >= GIT_HEAD_MAX) return'''),
    ('fs/git.c', '    if (!ops_allowed(top, &err)) return snprintf(msg, cap, "%s", err.msg), GSW_REFUSED;\n', ''),
    ('fs/git.c', '    if (writes) j->rw[0] = top, j->rw[1] = common;', '    if (writes) j->rw[0] = top;'),
    ('fs/git.c', ', j.err = true, j.outlive = true, j.why = &why;', ', j.outlive = true, j.why = &why;'),
    ('fs/git.c', '    if (git_head_is(gitdir, name)) return GSW_DONE;', '    if (r == BOX_ENDED) return GSW_DONE;'),
    ('fs/git.c', '        if (str_starts_with(l, S("error: ")) || str_starts_with(l, S("fatal: "))) bad = str_trim(str_cut(l, 7, l.len));', '        if (str_starts_with(l, S("error: "))) bad = str_trim(str_cut(l, 7, l.len));'),
    ('fs/git.c', '        if (l.len) last = l;\n', ''),
    ('fs/git.c', '    Str s = bad.len ? bad : last;', '    Str s = last;'),
    ('fs/git.c', '    if (strncmp(line, "ref: refs/heads/", 16) == 0) snprintf(name, cap, "%s", line + 16);', '    snprintf(name, cap, "%s", line + 16);'),
    ('fs/git.c', '    return now[0] && strcmp(now, name) == 0;', '    return strcmp(now, name) == 0;'),
    ('fs/git.c', '    git_head_name(gitdir, head, sizeof head);\n    isize len = 0;', '    head[0] = 0;\n    isize len = 0;'),
    ('fs/sandbox.c', '            if (j->rw[i]) av[n++] = "--bind", av[n++] = j->rw[i], av[n++] = j->rw[i];', '            if (j->rw[i] && i == 0) av[n++] = "--bind", av[n++] = j->rw[i], av[n++] = j->rw[i];'),
    ('fs/sandbox.c', '        if (j->out && j->err) dup2(pipe_fd[1], 2);\n', ''),
    ('ui/prompts.c', '    case UK_SHIFT_ENTER: if (u->crumb_at < in->crumb_n) *a =', '    case UK_SHIFT_ENTER: *a ='),
    ('ui/handle.c', '    if (u->focus == UI_FOCUS_CRUMBS && u->crumb_at > in->crumb_n - 1 + u->branch_on) u->crumb_at = in->crumb_n - 1 + u->branch_on;', '    if (u->focus == UI_FOCUS_CRUMBS && u->crumb_at > in->crumb_n) u->crumb_at = in->crumb_n;'),
    ('fs/sandbox.c', '        if (!j->outlive) av[n++] = "--die-with-parent";', '        av[n++] = "--die-with-parent";'),
    ('fs/sandbox.c', '        if (!j->outlive) prctl(PR_SET_PDEATHSIG, SIGKILL);', '        prctl(PR_SET_PDEATHSIG, SIGKILL);'),
    ('fs/git.c', ', j.err = true, j.outlive = true, j.why = &why;', ', j.err = true, j.why = &why;'),
    ('fs/sandbox.c', '    if (j->out && j->outlive) pipe_fd[0] = pipe_fd[1] = memfd_create(', '    if (false) pipe_fd[0] = pipe_fd[1] = memfd_create('),
    ('fs/sandbox.c', '    if (j->outlive) return box_wait_out(j, pfd, fd, pfd < 0 && errno != ESRCH);', '    if (false) return box_wait_out(j, pfd, fd, pfd < 0 && errno != ESRCH);'),
    ('fs/sandbox.c', '    return gone ? BOX_ENDED : BOX_GOING;', '    return BOX_ENDED;'),
    ('fs/git.c', '"switch", "-q", "--no-guess", name, NULL };', '"switch", "-q", name, NULL };'),
    ('fs/git.c', '    git_said((Str){ out, len }, msg, cap);\n    if (git_head_is(gitdir, name)) return GSW_DONE;', '    if (git_head_is(gitdir, name)) return GSW_DONE;\n    git_said((Str){ out, len }, msg, cap);'),
    ('fs/git.c', '    if (r == BOX_GOING) return snprintf(', '    if (false) return snprintf('),
    # F12 code-health step 1: fds, the program that runs, the second writable bind
    ('fs/sandbox.c', '        close_range(3, ~0U, 0);', '        (void)0;'),
    ('fs/spawn.c', '    posix_spawn_file_actions_addclosefrom_np(&fa, 3);\n', ''),
    ('fs/guard.c', '    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;', '    return access(path, X_OK) == 0;'),
    ('fs/spawn.c', '    int rc = posix_spawn(pid, full, &fa, &attr, argv, environ);', '    int rc = posix_spawnp(pid, argv[0], &fa, &attr, argv, environ);'),
    ('fs/git.c', '    if (binds.common[0] && !ops_allowed(binds.common, &err)) return', '    if (false) return'),
    ('fs/spawn.c', '    udisks_sh_argv("\\"$2\\" unmount -b \\"$1\\" && echo OFX-UNMOUNTED && \\"$2\\" power-off -b \\"$1\\" && echo OFX-EJECTED", dev, udisks, argv);', '    udisks_sh_argv("udisksctl unmount -b \\"$1\\" && echo OFX-UNMOUNTED && udisksctl power-off -b \\"$1\\" && echo OFX-EJECTED", dev, udisks, argv);'),
    # F12 code-health step 3: menus and keys agree, the branch menu's ways in, End
    ('ui/prompts.c', '        if (u->crumb_at >= in->crumb_n) return ui_branch_open(u, true), true;', '        if (u->crumb_at >= in->crumb_n) return ui_branch_open(u, false), true;'),
    ('ui/handle.c', '        if (files && !ui_allows(u, UA_RENAME)) return *out = ui_view_act(u, (UiAction){ UA_RENAME, 0, false }), true;\n', ''),
    ('ui/handle.c', 'return *out = ui_allows(u, UA_OPEN_WITH) ? ui_ow_open(u) : ui_view_act(u, (UiAction){ UA_OPEN_WITH, 0, false }), true;', 'return *out = ui_ow_open(u), true;'),
    ('ui/handle.c', "            if (u->focus == UI_FOCUS_CRUMBS && u->crumb_at >= in->crumb_n) ui_branch_open(u, true);   // (the branch button's)\n            else if", '            if'),
    ('ui/menus.c', '    if (!u->branch_on) return;\n    u->ctx_key = keys;', '    u->ctx_key = keys;'),
    ('ui/menus.c', '    case UM_NEW_FOLDER: return ui_allows(u, UA_NEWFOLDER);', '    case UM_NEW_FOLDER: return true;'),
    ('ui/menus.c', '    case UM_NEW_FILE: return ui_allows(u, UA_NEWFILE);', '    case UM_NEW_FILE: return true;'),
    ('ui/menus.c', '    case UM_OPEN_WITH: return files && ui_allows(u, UA_OPEN_WITH);', '    case UM_OPEN_WITH: return files;'),
    ('ui/menus.c', '    case UM_PASTE: return u->can_paste && ui_allows(u, UA_PASTE);', '    case UM_PASTE: return u->can_paste;'),
    ('ui/menus.c', '    case UM_PASTE: return u->can_paste && ui_allows(u, UA_PASTE);', '    case UM_PASTE: return ui_allows(u, UA_PASTE);'),
    ('ui/menus.c', '    case UM_RENAME: return here && ui_allows(u, UA_RENAME);', '    case UM_RENAME: return here;'),
    ('ui/menus.c', '    case UM_DUPLICATE: return here && ui_allows(u, UA_DUPLICATE);', '    case UM_DUPLICATE: return here;'),
    ('ui/menus.c', '    case UM_MAKE_LINK: return here && ui_allows(u, UA_MAKE_LINK);', '    case UM_MAKE_LINK: return here;'),
    ('ui/ui.c', '    case UA_RENAME: case UA_DUPLICATE: case UA_OPEN_WITH: return v != UI_VIEW_TRASH;', '    case UA_RENAME: case UA_DUPLICATE: return v != UI_VIEW_TRASH;'),
    ('ui/ui.c', '    case UA_PASTE: case UA_NEWFOLDER: case UA_NEWFILE: case UA_ADD_BOOKMARK: return v == UI_VIEW_FOLDER;', '    case UA_PASTE: case UA_NEWFOLDER: case UA_ADD_BOOKMARK: return v == UI_VIEW_FOLDER;'),
    ('ui/ui.c', '    case UA_PASTE: case UA_NEWFOLDER: case UA_NEWFILE: return true;', '    case UA_PASTE: case UA_NEWFILE: return true;'),
    ('ui/ui.c', '    case UA_RENAME: case UA_DUPLICATE: case UA_MAKE_LINK: case UA_TRASH: case UA_CUT: return v != UI_VIEW_SEARCH;', '    case UA_RENAME: case UA_DUPLICATE: case UA_MAKE_LINK: case UA_TRASH: case UA_CUT: return true;'),
    ('ui/ui.c', '    return ui_view_allows(u->view, k) && (u->can_write || !ui_view_writes(u->view, k));', '    return ui_view_allows(u->view, k);'),
    ('ui/ui.c', '    if (ui_view_allows(u->view, a.kind)) return (UiAction){ UA_READ_ONLY, 0, false };', '    if (ui_view_allows(u->view, a.kind)) return (UiAction){ UA_NONE, 0, false };'),
    # F12 code-health step 4: the vi keys, Shift off, the slots, the panels' keys; Trash, Delete, Cut under the rule
    ('ui/ui.c', "    case 'h': e.key = UK_LEFT; break;\n    case 'j': e.key = UK_DOWN; break;", "    case 'j': e.key = UK_DOWN; break;"),
    ('ui/ui.c', "    case 'l': e.key = UK_RIGHT; break;\n    default: break;\n    }\n    return e;", '    default: break;\n    }\n    return e;'),
    ('ui/ui.c', '    case UK_SEL_HOME: return UK_HOME;\n', ''),
    ('ui/ui.c', '    case UK_SEL_END: return UK_END;\n', ''),
    ('ui/ui.c', '    return it >= first && it < first + n ? (int)(it - first) : -1;', '    return it >= first && it <= first + n ? (int)(it - first) : -1;'),
    ('ui/handle.c', '        if (e.key == UK_ESC || e.key == UK_ENTER) u->panel = UI_PANEL_NONE;\n        return true;', '        return true;'),
    ('ui/ui.c', 'case UA_MAKE_LINK: case UA_TRASH: case UA_CUT: return v != UI_VIEW_SEARCH;', 'case UA_MAKE_LINK: case UA_CUT: return v != UI_VIEW_SEARCH;'),
    ('ui/ui.c', 'case UA_MAKE_LINK: case UA_TRASH: case UA_CUT: return v != UI_VIEW_SEARCH;', 'case UA_MAKE_LINK: case UA_TRASH: return v != UI_VIEW_SEARCH;'),
    ('ui/ui.c', '    case UA_DELETE: return v == UI_VIEW_FOLDER;', '    case UA_DELETE: return v != UI_VIEW_SEARCH;'),
    ('ui/handle.c', '        if (files && !ui_allows(u, UA_DELETE)) return *out = ui_view_act(u, (UiAction){ UA_DELETE, 0, false }), true;\n', ''),
    ('ui/menus.c', '    case UM_TRASH: return here && ui_allows(u, UA_TRASH);', '    case UM_TRASH: return here;'),
    ('ui/menus.c', '    case UM_CUT: return here && ui_allows(u, UA_CUT);', '    case UM_CUT: return here;'),
    ('ui/menus.c', '    case UM_DELETE: return here && ui_allows(u, UA_DELETE);', '    case UM_DELETE: return here;'),
    # F12 code-health step 5 (K): the readers, commondir, git_job's writes, box_run's reasons, a run folder whole
    ('fs/paths.c', '    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);\n    if (fd < 0) return -1;', '    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);\n    if (fd < 0) return -1;'),
    ('fs/paths.c', ' || st->st_size > max ||', ' ||'),
    ('fs/git.c', '    if (rel[0] && realpath(at, r)) snprintf(out, cap, "%s", r);', '    (void)r;'),
    ('fs/git.c', '    if (writes) j->rw[0] = top, j->rw[1] = common;\n    else j->ro[3] = top, j->ro[4] = common;', '    j->ro[3] = top, j->ro[4] = common;'),
    ('fs/sandbox.c', '    if (nargs >= BOX_ARGS_MAX || !ops_program_allowed(argv[0], &err)) {', '    if (!ops_program_allowed(argv[0], &err)) {'),
    ('fs/sandbox.c', '        if (j->why) err_set(j->why, "bubblewrap isn\'t installed");\n', ''),
    ('fs/sandbox.c', '        if (j->why) *j->why = err;\n', ''),
    ('fs/thumb.c', '    delete_tree(dir, &gone, &derr);', '    rmdir(dir), (void)gone, (void)derr;'),
    ('fs/git.c', '    if (r == BOX_NOT_RUN) snprintf(msg, cap, "%s", why.msg[0] ? why.msg : "git didn\'t run");', '    if (r == BOX_NOT_RUN) snprintf(msg, cap, "git didn\'t run");'),
    # F12 step 6: the branches panel, the full list with times
    ('ui/menus.c', '    if (it == UM_BR_ALL) return ui_brp_open(u);\n', ''),
    ('ui/panels.c', '        isize i = k.idx[k.pick->list.sel];', '        isize i = k.pick->list.sel;'),   # (retargeted, step E)
    # (dropped, step E: the pickers share one path now, whose own mutant is kept)
    ('ui/handle.c', '        if ((d = ui_pick_press(ui_pick_of(u, pk), in))) return *out = ui_picker_do(u, pk, d, in), true;', ''),   # (retargeted, step E)
    # (dropped, step E: the pickers share one path now, whose own mutant is kept)
    ('ui/panels.c', '    } else if (d == UI_PICK_CLOSE) ui_picker_close(u, p);', '    }'),   # (retargeted, step E)
    ('ui/helpers.c', '    else if (d < 14 * 86400) snprintf(out, cap, "%lld %s", (long long)(d / 86400), d / 86400 == 1 ? "day" : "days");', '    else if (d < 14 * 86400) snprintf(out, cap, "%lld days", (long long)(d / 86400));'),
    ('ui/helpers.c', '    if (d < 60) snprintf(out, cap, "now");', '    if (d <= 0) snprintf(out, cap, "now");'),
    ('fs/git.c', '        r->name[r->n] = str_cut(l, sp + 1, l.len), r->when[r->n] = t, r->taken[r->n] = false, r->n++;', '        r->name[r->n] = str_cut(l, sp + 1, l.len), r->when[r->n] = 0, r->taken[r->n] = false, r->n++;'),
    ('fs/git.c', '    b->n = 0, b->cur = -1, b->total = r.n, b->more = r.more || cut;', '    b->n = 0, b->cur = -1, b->total = 0, b->more = r.more || cut;'),
    # F12 code-health B (2026-11-03): the pickers shared by Open With and the branches
    ('ui/panels.c', '    case UK_ESC: return UI_PICK_CLOSE;', ''),
    ('ui/panels.c', '    case UK_ENTER: case UK_SHIFT_ENTER: return UI_PICK_CHOOSE;', ''),
    # (dropped, step E: the pickers share one path now, whose own mutant is kept)
    ('ui/panels.c', '    else if (d == UI_PICK_CHOOSE && k.pick->list.n > 0) {', '    else if (0) {'),   # (retargeted, step E)
    ('ui/panels.c', '    if (d == UI_PICK_FILTER) ui_picker_filter(u, p, in);', '    if (d == UI_PICK_FILTER) {}'),   # (retargeted, step E)
    ('ui/handle.c', '    ui_picks_fresh(u, in);\n    return a;', '    return a;'),
    # F12 code-health A (2026-11-03): the cut output, long names, more, a panel press acts
    ('fs/git.c', '        if (k < 0 && b->more) snprintf(', '        if (k < 0 && false) snprintf('),
    ('fs/git.c', '        if (k < 0 && b->more) snprintf(', '        if (k < 0) snprintf('),
    ('fs/git.c', ', b->n = 1, b->cur = 0, b->total++;', ', b->n = 1, b->cur = 0;'),
    ('fs/git.c', '    b->n = 0, b->cur = -1, b->total = r.n, b->more = r.more || cut;', '    b->n = 0, b->cur = -1, b->total = r.n, b->more = r.more;'),
    ('fs/git.c', '    b->n = 0, b->cur = -1, b->total = r.n, b->more = r.more || cut;', '    b->n = 0, b->cur = -1, b->total = r.n, b->more = cut;'),
    ('fs/git.c', '    r->more = at < refs.len;', '    r->more = false;'),
    ('fs/git.c', ' || l.len - sp - 1 >= GIT_HEAD_MAX) continue;', ') continue;'),
    ('fs/git.c', ' || l.len - sp - 1 >= GIT_HEAD_MAX) continue;', ' || l.len - sp - 1 > GIT_HEAD_MAX) continue;'),
    ('fs/git.c', '    *cut = out.len >= cap;', '    *cut = out.len > cap;'),
    ('fs/git.c', "    while (e > 0 && out.p[e - 1] != '\\n') e--;\n", ''),
    ('fs/git.c', '    if (k < 0 || r->taken[k]) return true;', '    if (k < 0) return true;'),
    ('fs/git.c', '        if (!git_branch_add(b, &r, git_refs_find(&r, str_cut(m, to + 4, m.len)))) break;\n', ''),
    ('fs/git.c', '        if (!git_branch_add(b, &r, git_refs_find(&r, str_cut(m, 0, to)))) break;\n', ''),
    ('fs/git.c', '    for (int k = 0; k < r.n && b->n < GIT_BRANCHES_MAX; k++) git_branch_add(b, &r, k);\n', ''),
    ('fs/git.c', '        git_branch_add(b, &r, k), b->cur = k >= 0 ? 0 : -1;', '        git_branch_add(b, &r, k), b->cur = 0;'),
    ('fs/git.c', '        if (str_eq(r->name[k], name)) return k;', '        if (str_starts_with(r->name[k], name)) return k;'),
    ('fs/git.c', '        if (sp <= 0 || sp + 1 >= l.len || l.len - sp - 1 >= GIT_HEAD_MAX) continue;\n', ''),
]
# F13 step 1 (2026-11-04): Copy Path
MUTANTS += [
    ('ui/handle.c', '        if (u->focus == UI_FOCUS_SIDE) return *out = (UiAction){ UA_COPY_PATH', '        if (false) return *out = (UiAction){ UA_COPY_PATH'),
    ('ui/handle.c', '        return *out = ui_view_act(u, (UiAction){ UA_COPY_PATH, 0, false }), true;\n', '        break;\n'),
    ('ui/handle.c', 'ui_view_act(u, (UiAction){ UA_COPY_PATH, 0, false })', '(UiAction){ UA_COPY_PATH, 0, false }'),
    ('ui/ui.c', '    case UA_COPY: case UA_COPY_PATH: case UA_CUT:', '    case UA_COPY: case UA_CUT:'),
    ('ui/menus.c', '    case UM_COPY_PATH: return u->focus == UI_FOCUS_SIDE ? u->side.n > 0 : ui_view_allows(u->view, UA_COPY_PATH);', '    case UM_COPY_PATH: return ui_view_allows(u->view, UA_COPY_PATH);'),
    ('ui/menus.c', '    case UM_COPY_PATH: return u->focus == UI_FOCUS_SIDE ? u->side.n > 0 : ui_view_allows(u->view, UA_COPY_PATH);', '    case UM_COPY_PATH: return u->focus == UI_FOCUS_SIDE ? true : ui_view_allows(u->view, UA_COPY_PATH);'),
    ('ui/menus.c', '    case UM_COPY_PATH: return u->focus == UI_FOCUS_SIDE ? u->side.n > 0 : ui_view_allows(u->view, UA_COPY_PATH);', '    case UM_COPY_PATH: return u->focus != UI_FOCUS_SIDE && ui_view_allows(u->view, UA_COPY_PATH);'),
    ('ui/menus.c', '    case UM_COPY_PATH_IT: return !trash;', '    case UM_COPY_PATH_IT: return true;'),   # (retargeted, step F)
    ('ui/menus.c', '    case UM_COPY_PATH_IT: return !trash;', '    case UM_COPY_PATH_IT: return k == UI_CTX_DIR;'),   # (retargeted, step F)
    ('ui/ui.c', '    UM_UNDO, UM_SEP, UM_CUT, UM_COPY, UM_COPY_PATH, UM_PASTE,', '    UM_UNDO, UM_SEP, UM_CUT, UM_COPY, UM_PASTE,'),
    ('ui/ui.c', 'UM_SEP, UM_CUT, UM_COPY, UM_COPY_PATH, UM_PASTE_INTO,', 'UM_SEP, UM_CUT, UM_COPY, UM_PASTE_INTO,'),
    ('ui/ui.c', 'UM_SEP, UM_CUT_IT, UM_COPY_IT, UM_COPY_PATH_IT, UM_PASTE_INTO,', 'UM_SEP, UM_CUT_IT, UM_COPY_IT, UM_PASTE_INTO,'),
    ('ui/ui.c', '"copy-path", .cmd = UK_COPY_PATH, .runs = true }', '"copy-path", .cmd = UK_COPY, .runs = true }'),
    ('ui/ui.c', '    { KS_CHANGE, "Copy Path", UK_COPY_PATH, NULL, NULL },\n', ''),
]
# F13 step 1 review (2026-11-04): the sidebar's key, the empty space's entry, shell_quote
MUTANTS += [
    ('ui/handle.c', '        if (u->focus == UI_FOCUS_SIDE) return *out = (UiAction){ UA_COPY_PATH, u->side.sel, true }, u->side.n > 0;\n', '        if (u->focus == UI_FOCUS_SIDE) break;\n'),
    ('ui/handle.c', '(UiAction){ UA_COPY_PATH, u->side.sel, true }, u->side.n > 0;', '(UiAction){ UA_COPY_PATH, u->side.sel, true }, true;'),
    ('ui/handle.c', '(UiAction){ UA_COPY_PATH, u->side.sel, true }, u->side.n > 0;', '(UiAction){ UA_COPY_PATH, u->side.sel, false }, u->side.n > 0;'),
    ('ui/menus.c', '    case UM_COPY_PATH_HERE: return true;', '    case UM_COPY_PATH_HERE: return u->list.n > 0;'),
    ('ui/ui.c', 'UM_SEP, UM_PASTE, UM_COPY_PATH_HERE, UM_SELECT_ALL,', 'UM_SEP, UM_PASTE, UM_SELECT_ALL,'),
    ('fs/clip.c', '    return cp < 0x20 || cp == 0x7f || (cp >= 0x80', '    return cp < 0x20 || (cp >= 0x80'),
    ('fs/clip.c', 'strchr("/._-+,:@%=", c)', 'strchr("/._-+,:@%= ", c)'),
    ('fs/clip.c', '    bool plain = s.len > 0, ctrl = false;', '    bool plain = true, ctrl = false;'),
    ('fs/clip.c', '            if (s.p[i] == \'\\\'\') PUT("\'\\\\\'\'");\n            else PUT', '            PUT'),
    ('fs/clip.c', '            if (c == \'\\n\') PUT("\\\\n");\n', '            if (0) PUT("\\\\n");\n'),
    ('fs/clip.c', '            else if (c == \'\\\\\' || c == \'\\\'\') PUT("\\\\%c", c);\n', ''),
]
# F13 step 1 re-review (2026-11-04): a byte of no valid UTF-8 spelled \xHH
MUTANTS += [
    ('fs/clip.c', '(cp >= 0x80 && cp <= 0x9f) || hidden', '(cp >= 0x80 && cp <= 0xbf) || hidden'),
    ('fs/clip.c', ' || c >= 0x80;\n        if (bad)', ';\n        if (bad)'),
    ('fs/clip.c', '        if (!safe || bad) plain = false;', '        if (!safe) plain = false;'),
    ('fs/clip.c', '            else if (bad)\n                for', '            else if (false)\n                for'),
    ('fs/clip.c', '            else PUT("%.*s", len, s.p + i);', '            else PUT("%c", c);'),
]
# F13 step 2 (2026-11-04): the sidebar's width, sidewidth=
MUTANTS += [
    ('ui/helpers.c', '    int hi = w / 2 < UI_SIDE_MAX ? w / 2 : UI_SIDE_MAX;', '    int hi = UI_SIDE_MAX;'),
    ('ui/helpers.c', '    int hi = w / 2 < UI_SIDE_MAX ? w / 2 : UI_SIDE_MAX;', '    int hi = w / 2;'),
    ('ui/helpers.c', '    if (want > hi) want = hi;', '    if (want > hi + 1) want = hi;'),
    ('ui/helpers.c', '    return want < UI_SIDE_MIN ? UI_SIDE_MIN : want;', '    return want;'),
    ('ui/helpers.c', '    return want < UI_SIDE_MIN ? UI_SIDE_MIN : want;', '    return want <= UI_SIDE_MIN ? UI_SIDE_MIN + 1 : want;'),
    ('fs/state.c', '        if (str_eq(key, S("sidewidth"))) s.side_px = state_px(val);\n', ''),
]
# F13 step 3 (2026-11-04): drag and drop's threshold and Nemo's rule
MUTANTS += [
    ('ui/helpers.c', '    return dx * dx + dy * dy >= (float)(UI_DRAG_PX * UI_DRAG_PX);', '    return dx * dx + dy * dy > (float)(UI_DRAG_PX * UI_DRAG_PX);'),
    ('ui/helpers.c', '    return dx * dx + dy * dy >= (float)(UI_DRAG_PX * UI_DRAG_PX);', '    return dx * dx >= (float)(UI_DRAG_PX * UI_DRAG_PX);'),
    ('ui/helpers.c', 'enum { UI_DRAG_PX = 6, UI_SPRING_MS', 'enum { UI_DRAG_PX = 5, UI_SPRING_MS'),
    ('ui/helpers.c', '    if (mods & UI_MOD_CTRL) return UI_DROP_COPY;\n', ''),
    ('ui/helpers.c', '    if (mods & UI_MOD_SHIFT) return UI_DROP_MOVE;\n', ''),
    ('ui/helpers.c', '    return same_drive ? UI_DROP_MOVE : UI_DROP_COPY;', '    return UI_DROP_MOVE;'),
]
# F13 code-health step D (2026-11-04): the invisible characters escaped
MUTANTS += [
    ('fs/clip.c', ' || (cp >= 0x80 && cp <= 0x9f) || hidden ? n : 0;', ' || (cp >= 0x80 && cp <= 0x9f) ? n : 0;'),
    ('fs/clip.c', '    bool hidden = cp == 0x061c || ', '    bool hidden = '),
    ('fs/clip.c', '(cp >= 0x200b && cp <= 0x200f)', '(cp >= 0x200a && cp <= 0x200f)'),
    ('fs/clip.c', '(cp >= 0x200b && cp <= 0x200f)', '(cp >= 0x200c && cp <= 0x200f)'),
    ('fs/clip.c', '(cp >= 0x2028 && cp <= 0x202e)', '(cp >= 0x2029 && cp <= 0x202e)'),
    ('fs/clip.c', '(cp >= 0x2028 && cp <= 0x202e)', '(cp >= 0x2028 && cp <= 0x202d)'),
    ('fs/clip.c', '(cp >= 0x2060 && cp <= 0x2069)', '(cp >= 0x2060 && cp <= 0x2068)'),
    ('fs/clip.c', '(cp >= 0x2060 && cp <= 0x2069)', '(cp >= 0x2060 && cp <= 0x2070)'),
    ('fs/clip.c', ' || cp == 0xfeff;', ';'),
]
# F13 code-health step C2 (2026-11-04): ui_menu_shown as a switch (each entry's rule negated), the width rule
MUTANTS += [
    ('ui/menus.c', '    case UM_BM_MORE: return u->bm_more > 0;', '    case UM_BM_MORE: return !(u->bm_more > 0);'),
    ('ui/menus.c', '    case UM_BR_NOTE: return u->br_note;', '    case UM_BR_NOTE: return !(u->br_note);'),
    ('ui/menus.c', '    case UM_BR_ALL: return u->br_total > UI_MENU_BRANCHES;', '    case UM_BR_ALL: return !(u->br_total > UI_MENU_BRANCHES);'),
    ('ui/menus.c', '    case UM_OPEN_TAB: return !ctx || (u->ctx_dir && !trash);', '    case UM_OPEN_TAB: return !(!ctx || (u->ctx_dir && !trash));'),
    ('ui/menus.c', '    case UM_OPEN_TAB: return !ctx || (u->ctx_dir && !trash);', '    case UM_OPEN_TAB: return (u->ctx_dir && !trash);'),
    ('ui/menus.c', '    case UM_RENAME: return !ctx || u->marked <= 1;', '    case UM_RENAME: return !(!ctx || u->marked <= 1);'),
    ('ui/menus.c', '    case UM_RENAME: return !ctx || u->marked <= 1;', '    case UM_RENAME: return u->marked <= 1;'),
    ('ui/menus.c', '    case UM_NEW_FOLDER: case UM_NEW_FILE: return !ctx || !trash;', '    case UM_NEW_FOLDER: case UM_NEW_FILE: return !(!ctx || !trash);'),
    ('ui/menus.c', '    case UM_NEW_FOLDER: case UM_NEW_FILE: return !ctx || !trash;', '    case UM_NEW_FOLDER: case UM_NEW_FILE: return !trash;'),
    ('ui/menus.c', '    case UM_OPEN_WINDOW: case UM_PASTE_INTO: case UM_TERMINAL_IN: case UM_SEARCH_IN: return u->ctx_dir && !trash;', '    case UM_OPEN_WINDOW: case UM_PASTE_INTO: case UM_TERMINAL_IN: case UM_SEARCH_IN: return !(u->ctx_dir && !trash);'),
    ('ui/menus.c', '    case UM_CONTAINING: return ui_view_allows(u->view, UA_CONTAINING);', '    case UM_CONTAINING: return !(ui_view_allows(u->view, UA_CONTAINING));'),
    ('ui/menus.c', '    case UM_OPEN_LINE: return u->ctx_one && u->sel_hit;', '    case UM_OPEN_LINE: return !(u->ctx_one && u->sel_hit);'),
    ('ui/menus.c', '    case UM_BOOKMARK_IT: return u->ctx_dir && !trash && !u->sel_bm;', '    case UM_BOOKMARK_IT: return !(u->ctx_dir && !trash && !u->sel_bm);'),
    ('ui/menus.c', '    case UM_FOLLOW: return u->ctx_one && u->sel_link && !trash;', '    case UM_FOLLOW: return !(u->ctx_one && u->sel_link && !trash);'),
    ('ui/menus.c', '    case UM_EMPTY_TRASH: return ui_view_allows(u->view, UA_EMPTY_TRASH);', '    case UM_EMPTY_TRASH: return !(ui_view_allows(u->view, UA_EMPTY_TRASH));'),
    ('ui/menus.c', '    case UM_COPY_PATH_HERE: return ui_view_allows(u->view, UA_COPY_PATH);', '    case UM_COPY_PATH_HERE: return !(ui_view_allows(u->view, UA_COPY_PATH));'),
    ('ui/menus.c', '    case UM_TRASH: return !trash;', '    case UM_TRASH: return !(!trash);'),
    ('ui/menus.c', '    case UM_RESTORE: return ui_view_allows(u->view, UA_RESTORE);', '    case UM_RESTORE: return !(ui_view_allows(u->view, UA_RESTORE));'),
    ('ui/menus.c', '    if ((k = ui_slot(it, UM_BM_0, UI_MENU_BOOKMARKS)) >= 0) return k < u->bm_n;', '    if ((k = ui_slot(it, UM_BM_0, UI_MENU_BOOKMARKS)) >= 0) return k <= u->bm_n;'),
    ('ui/menus.c', '    if ((k = ui_slot(it, UM_BR_0, UI_MENU_BRANCHES)) >= 0) return k < u->br_n;', '    if ((k = ui_slot(it, UM_BR_0, UI_MENU_BRANCHES)) >= 0) return k <= u->br_n;'),
    ('ui/menus.c', '    bool ctx = u->menu >= UI_MENUS, trash', '    bool ctx = u->menu > UI_MENUS, trash'),
    ('ui/menus.c', '    case UM_COPY_PATH_HERE: return (UiAction){ UA_COPY_PATH, UI_COPY_SHOWN, true };', '    case UM_COPY_PATH_HERE: return (UiAction){ UA_COPY_PATH, 0, true };'),
]
# F13 code-health step C3 (2026-11-04): one number parser (str_num), state_kv, utf8_at over base's
# utf8_next, buf_put
MUTANTS += [
    ('fs/paths.c', "        if (w.p[i] < '0' || w.p[i] > '9' || n > (max - (w.p[i] - '0')) / 10) return -1;", "        if (n > (max - (w.p[i] - '0')) / 10) return -1;"),
    ('fs/paths.c', "        if (w.p[i] < '0' || w.p[i] > '9' || n > (max - (w.p[i] - '0')) / 10) return -1;", "        if (w.p[i] < '0' || w.p[i] > '9' || n > max / 10) return -1;"),
    ('fs/paths.c', "        if (w.p[i] < '0' || w.p[i] > '9' || n > (max - (w.p[i] - '0')) / 10) return -1;", "        if (w.p[i] > '9' || n > (max - (w.p[i] - '0')) / 10) return -1;"),
    ('fs/paths.c', "    if (!w.len) return -1;\n    for (isize i = 0; i < w.len; i++) {   // (n checked", "    for (isize i = 0; i < w.len; i++) {   // (n checked"),
    ('fs/state.c', '    i64 n = v.len > 4 ? -1 : str_num(v, 9999);', '    i64 n = str_num(v, 9999);'),
    ('fs/state.c', '    return n < 0 ? 0 : (int)n;', '    return (int)n;'),
    ('fs/paths.c', '    *key = str_trim(str_cut(line, 0, eq)), *val = str_trim(str_cut(line, eq + 1, line.len));', '    *key = str_trim(str_cut(line, 0, eq)), *val = str_cut(line, eq + 1, line.len);'),
    ('fs/clip.c', '    return *cp == 0xfffd && j - i == 1 ? 0 : (int)(j - i);', '    return *cp == 0xfffd ? 0 : (int)(j - i);'),
    ('fs/clip.c', '    return *cp == 0xfffd && j - i == 1 ? 0 : (int)(j - i);', '    return (int)(j - i);'),
    ('fs/clip.c', '    if (k < 0 || k >= cap - *n) return false;', '    if (k < 0) return false;'),
    ('fs/clip.c', '        u32 cp;\n        i += bad ? bad : utf8_at(s, i, &cp);', '        i++;'),
]

# Code-health step D (2026-11-06): move_merge doesn't visit an entry it left again (test_f3's
# asks), and knows it by its name as well as its inode (the hard links that must still move).
MUTANTS += [
    ('fs/job.c', '            if (seen) continue;\n', ''),
    ('fs/job.c', ' && left[k].name == name;', ';'),
]

# 2026-11-07: theme_fit, every colour ofx draws text in read on its backgrounds (test_theme_fit.c).
TF = 'fs/theme.c'   # (T is fs/trash.c by here)
MUTANTS += [
    (TF, "return v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);", "return pow((v + 0.055) / 1.055, 2.4);"),
    (TF, "return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);", "return (la + 0.05) / (lb + 0.05);"),
    (TF, "return (Rgb){ (u8)lround(a.r * t + b.r * (1 - t)),", "return (Rgb){ (u8)lround(b.r * t + a.r * (1 - t)),"),
    (TF, "f[i] = xyz[i] > 0.008856 ? cbrt(xyz[i]) : 7.787 * xyz[i] + 16.0 / 116;", "f[i] = xyz[i] > 0.008856 ? xyz[i] : 7.787 * xyz[i] + 16.0 / 116;"),
    (TF, "return sqrt(l[1] * l[1] + l[2] * l[2]);", "return sqrt(l[1] * l[1]);"),
    (TF, "for (int i = 0; i < n; i++) w = fmin(w, rgb_contrast(c, bg[i]));", "for (int i = 0; i < n; i++) w = fmax(w, rgb_contrast(c, bg[i]));"),
    (TF, "if (rgb_worst(x, bg, n) >= min && (!marked_bg ||", "if ((!marked_bg ||"),
    (TF, "if (rgb_worst(x, bg, n) >= min && (!marked_bg ||", "if (rgb_worst(x, bg, n) >= min && (1 ||"),
    (TF, "Rgb x = k == 0 ? c : oklch_at(o, o[0] + (ot[0] - o[0]) * k / 40.0);", "Rgb x = k == 0 ? c : oklch_at(ot, ot[0] + (o[0] - ot[0]) * k / 40.0);"),
    (TF, "        if (rgb_worst(x, bg, n) >= min && (!marked_bg || rgb_contrast(x, rgb_mix(x, *marked_bg, 0.28)) >= min)) return x;\n    }\n    return to;", "        if (rgb_worst(x, bg, n) >= min && (!marked_bg || rgb_contrast(x, rgb_mix(x, *marked_bg, 0.28)) >= min)) return x;\n    }\n    return c;"),
    (TF, "                t->have |= 1u << k;\n", ""),
    (TF, "    if (theme_has(t, TK_BRIGHT_RED)) warm[nw++] = &t->bright_red;\n", ""),
    (TF, "    if (theme_has(t, TK_ORANGE)) warm[nw++] = &t->orange;\n", ""),
    (TF, "if (rgb_chroma(*warm[i]) >= 20 && (!best ||", "if ((!best ||"),
    (TF, "    if (rgb_chroma(t->red) >= 20 && rgb_worst(t->red, side, 3) >= THEME_TEXT_MIN) best = &t->red;\n", ""),
    (TF, "    if (rgb_delta(t->accent, t->drive_warn) < THEME_UNLIKE_MIN) {", "    if (rgb_delta(t->accent, t->drive_warn) > THEME_UNLIKE_MIN) {"),
    (TF, "t->drive_warn = !best ? t->fg : rgb_push(", "t->drive_warn = best ? t->fg : rgb_push("),
    (TF, "(rgb_delta(*other[i], t->drive_warn) >= THEME_UNLIKE_MIN) * 100", "(rgb_delta(*other[i], t->drive_warn) >= THEME_UNLIKE_MIN) * 0"),
    (TF, "    t->drive_used = rgb_push(*used, t->fg, side, 3, THEME_TEXT_MIN, NULL);", "    t->drive_used = *used;"),
    (TF, "    t->accent = rgb_push(t->accent, t->fg, (Rgb[]){ t->bg, t->bg_light, t->selection }, 3, text_min, &t->bg);\n", ""),
    (TF, "3, text_min, &t->bg);", "3, text_min, NULL);"),
    (TF, "const Rgb list[] = { t->bg, t->bg_light, t->selection, rgb_mix(t->accent, t->bg, 0.28) };", "const Rgb list[] = { t->bg, t->bg_light, t->selection, t->bg };"),
    (TF, "t->fg_dim = rgb_push(t->fg_dim, t->fg, list, 4, dim_min, NULL);", "t->fg_dim = rgb_push(t->fg_dim, t->fg, list, 4, text_min, NULL);"),
    (TF, "t->fg_dim = rgb_push(t->fg_dim, t->fg, list, 4, dim_min, NULL);", "t->fg_dim = rgb_push(t->fg_dim, t->fg, list, 3, dim_min, NULL);"),
    (TF, "    for (int i = 0; i < COUNT(text); i++) *text[i] = rgb_push(*text[i], t->fg, list, 4, text_min, NULL);\n", ""),
    # 2026-11-08: OKLCH (lightness only), and the files area's rescue (theme_fit_list).
    (TF, "double l = cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);", "double l = cbrt(0.4122214708 * r + 0.5363325363 * g);"),
    (TF, "o[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;", "o[1] = 1.9779984951 * l - 2.4285922050 * m;"),
    (TF, "    return v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1 / 2.4) - 0.055;", "    return 1.055 * pow(v, 1 / 2.4) - 0.055;"),
    (TF, "        if (e < -0.0005 || e > 1.0005) return false;\n", ""),
    (TF, "    l = l * l * l, m = m * m * m, s = s * s * s;", "    l = l * l, m = m * m, s = s * s;"),
    (TF, "    for (int i = 0; i < 20; i++) {   // the most chroma that fits, by halving", "    for (int i = 0; i < 0; i++) {"),
    (TF, "        if (oklab_rgb(o, &x)) lo = k, fits = x;\n        else hi = k;", "        if (oklab_rgb(o, &x)) hi = k, fits = x;\n        else lo = k;"),
    (TF, "if (oklab_rgb(o, &x)) lo = k, fits = x;", "if (oklab_rgb(o, &x)) lo = k;"),
    (TF, "Rgb x = k == 0 ? c : oklch_at(o,", "Rgb x = k == 0 ? to : oklch_at(o,"),
    (TF, "    theme_fit_text(t, THEME_RESCUE_MIN, THEME_RESCUE_MIN);", "    theme_fit_text(t, THEME_TEXT_MIN, THEME_RESCUE_MIN);"),
    (TF, "    theme_fit_text(t, THEME_RESCUE_MIN, THEME_RESCUE_MIN);", "    theme_fit_text(t, THEME_RESCUE_MIN, THEME_DIM_MIN);"),
    (TF, "    theme_fit_text(t, THEME_TEXT_MIN, THEME_DIM_MIN);\n", ""),
    (TF, "Rgb *text[] = { &t->red, &t->yellow,", "Rgb *text[] = { &t->yellow, &t->yellow,"),
    (TF, "t->drive_free = rgb_push(t->drive_track, t->fg, side, 3, THEME_DIM_MIN, NULL);", "t->drive_free = t->drive_track;"),
    # The theme-fit review's three (2026-11-07), and the track's own shade:
    (TF, "rgb_worst(*warm[i], side, 3) > rgb_worst(*best, side, 3))) best = warm[i];", "rgb_worst(*warm[i], side, 3) < rgb_worst(*best, side, 3))) best = warm[i];"),
    (TF, "t->drive_track = rgb_mix(t->fg_dim, t->bg_light, 0.75);", "t->drive_track = rgb_mix(t->fg_dim, t->bg_light, 0.25);"),
    (TF, "* 100 + rgb_worst(*other[i], side, 3);", "* 100;"),
    (TF, "    t->drive_track = rgb_mix(t->fg_dim, t->bg_light, 0.75);\n    t->drive_free = rgb_push(t->drive_track, t->fg, side, 3, THEME_DIM_MIN, NULL);", "    t->drive_free = rgb_push(rgb_mix(t->fg_dim, t->bg_light, 0.75), t->fg, side, 3, THEME_DIM_MIN, NULL);\n    t->drive_track = t->drive_free;"),
]

# F15 step 1 (2026-11-08): the one key table (ui/keys.c), GLFW's keys as its chords
# (gui/glfwkeys.c), and the files' commands read from the event's command.
MUTANTS += [
    (KY, "if (c.mods == UI_MOD_ALT && c.code >= 'a' && c.code <= 'z') return", "if (c.mods == UI_MOD_ALT && c.code >= 'a' && c.code <= 'y') return"),
    (KY, "if (c.mods == UI_MOD_ALT && c.code >= '1' && c.code <= '9')", "if (c.mods == UI_MOD_ALT && c.code >= '0' && c.code <= '9')"),
    (KY, "return *ch = c.code - '0', UK_TAB_JUMP;", "return *ch = c.code, UK_TAB_JUMP;"),
    (KY, "    if (e.key == UK_TAB_JUMP) e.cmd = UK_TAB_JUMP;\n", ""),
    (KY, "e.key && e.key == ui_fixed_of(plain, &ch)", "e.key == ui_fixed_of(plain, &ch)"),
    (KY, "    if (!e.key && e.cmd) e.key = UK_CMD;", "    if (!e.key) e.key = UK_CMD;"),
    (KY, "    if (c.mods & UI_MOD_TYPED) return (UiEvent){ UK_NONE, c.code, ui_bound(c) };", "    if (c.mods & UI_MOD_TYPED) return (UiEvent){ UK_NONE, c.code, UK_NONE };"),
    (KY, "    return a.code == b.code && a.mods == b.mods;", "    return a.code == b.code;"),
    (KY, 'if (ui_binds.e[i].b.cmd == cmd && k-- == 0)', 'if (ui_binds.e[i].b.cmd == cmd && --k == 0)'),
    (CF, '        else if (full) snprintf(c->why', '        else if (0) snprintf(c->why'),
    (KY, '{ UI_MOD_SHIFT, "Shift+" }', '{ UI_MOD_SHIFT, "Shift-" }'),
    (KY, '"F%u", c.code - UKC_F1 + 1);', '"F%u", c.code - UKC_F1);'),
    (KY, '        if (c.code == \' \') snprintf(out, (size_t)cap, "Space");\n        else {', '        if (0) snprintf(out, (size_t)cap, "Space");\n        else {'),
    (KY, "(char)(c.code - 'a' + 'A') : (char)c.code);", "(char)c.code : (char)c.code);"),
    (KY, " { K(UKC_ESC, UI_MOD_SHIFT), UK_ESC },", ""),
    (KY, "{ K('x', UI_MOD_CTRL), UK_CUT }, ", ""),
    (KY, "{ K(UKC_DOWN, UI_MOD_ALT), UK_ALT_DOWN },", ""),
    (KY, "    if (*text || !UI_KEYS[r].cmd) return -1;", "    if (!UI_KEYS[r].cmd) return -1;"),
    (KY, "    if (UI_MENU_ITEM[it].hint) return UI_MENU_ITEM[it].hint;\n", ""),
    (CF, '        c->key_no[c->nkey] = no;\n', ''),
    (KY, "{ K(UKC_F1, 0), UK_SHORTCUTS }, { K(UKC_F1, C_), UK_SHORTCUTS },", "{ K(UKC_F1, C_), UK_SHORTCUTS }, { K(UKC_F1, 0), UK_SHORTCUTS },"),
    (KY, "{ T('N'), UK_FIND_PREV },", "{ T('N'), UK_FIND_NEXT },"),
    (GK, "c = (u32)('a' + key - GLFW_KEY_A);", "c = (u32)key;"),
    (GK, "{ GLFW_KEY_KP_ENTER, UKC_ENTER }, ", ""),
    (GK, "(mods & GLFW_MOD_SUPER ? UI_MOD_SUPER : 0)", "0"),
    (GK, "if (action != GLFW_PRESS && (e.cmd == UK_QUIT_ALL ||", "if (0 && (e.cmd == UK_QUIT_ALL ||"),
    (GK, "c = UKC_KP0 + (u32)(key - GLFW_KEY_KP_0);", "c = (u32)('0' + key - GLFW_KEY_KP_0);"),
    ('ui/handle.c', "    case UK_SORT_REVERSE: u->sort.reverse = !u->sort.reverse;", "    case UK_SORT_REVERSE: u->sort.reverse = true;"),
    ('ui/handle.c', "    case UK_FIND_PREV: ui_find_again(u, names, -1); return false;", "    case UK_FIND_PREV: ui_find_again(u, names, 1); return false;"),
    ('ui/handle.c', "        bool shifted = ui_unshift(e.cmd) != e.cmd;", "        bool shifted = false;"),
    ('ui/handle.c', "        UiKey cmd = shift_enter ? UK_ENTER : ui_unshift(e.cmd);", "        UiKey cmd = shift_enter ? UK_ENTER : e.cmd;"),
    ('ui/handle.c', "    if (e.cmd == UK_FIND) return true;\n", ""),
]

# F15 step 2 (2026-11-08): ofx.conf's key lines, read by ui/keys.c; menu entries as commands.
CF = 'fs/conf.c'
MUTANTS += [
    (KY, "        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');\n", ""),
    (KY, "        if (!mods) return *c = (UiChord){ cp, UI_MOD_TYPED }, true;", "        if (0) return *c = (UiChord){ cp, UI_MOD_TYPED }, true;"),
    (KY, "        if (mods == UI_MOD_SHIFT) return snprintf(", "        if (0) return snprintf("),
    (KY, "        code = cp >= 'A' && cp <= 'Z' ? cp - 'A' + 'a' : cp;", "        code = cp;"),
    (KY, "    if (code == ' ' && !mods) return *c = (UiChord){ ' ', UI_MOD_TYPED }, true;\n", ""),
    (KY, "    if (mods == UI_MOD_ALT && ((code >= 'a' && code <= 'z') || (code >= '1' && code <= '9')))", "    if (0)"),
    (KY, "        if (cp >= 0x7F) return snprintf(", "        if (0) return snprintf("),
    (KY, "    if (!code && ui_same_nocase(s, \"Space\")) code = ' ';", ""),
    (KY, "        if (sp < 0 || str_find(name, ' ') >= 0) snprintf(", "        if (sp < 0) snprintf("),
    (KY, "            if (!r.bad) r.bad = no[k], snprintf(", "            r.bad = no[k], snprintf("),
    (KY, '            if (same_key && ui_binds.e[i].b.cmd != cmd) {\n                r.clashes++;', '            if (same_key) {\n                r.clashes++;'),
    (KY, '                    if (ui_binds.e[i].user) snprintf(r.clash, sizeof r.clash, "lines', '                    if (0) snprintf(r.clash, sizeof r.clash, "lines'),
    (KY, "        if (none) continue;\n", ""),
    (KY, "        users++, ui_binds.n++;", "        ui_binds.n++;"),
    (KY, '    for (int i = 0; i < was_n && !r.changed; i++) r.changed = !ui_chord_eq(was[i].c, ui_binds.e[i].b.c) || was[i].cmd != ui_binds.e[i].b.cmd;\n', ''),
    (KY, "        bool wrap = !first && at - start + 2 + strlen(UI_CMDS[i].name) > 79;", "        bool wrap = false;"),
    (KY, "        if (has) continue;\n", ""),
    (KY, "    if (!ui_binds.ready) ui_binds_reset();", "    if (!ui_binds.n) ui_binds_reset();"),
    (CF, '        if (c->nkey == CONF_KEYS_MAX || v.len >= CONF_KEY_LEN) return *full = true, false;', '        if (v.len >= CONF_KEY_LEN) return *full = true, false;'),
    (CF, "    if (n == sizeof text - 1) return err_set(err, \"%s: the text is past %d bytes\", path, CONF_TEXT_MAX), false;", ""),
    (CF, "        if (o->kind == CK_KEY && keys && at < cap) {", "        if (0) {"),
    ('ui/menus.c', "            if (!ui_menu_enabled(u, it)) {   // (greyed: what its view says, as a key's action would)", "            if (0) {"),
    ('ui/menus.c', "            u->menu = m;   // (what is shown and enabled is the bar menu's, for this pick only)\n", ""),
    ('ui/menus.c', "            u->menu = was;   // (no menu is open: the last one's stays, as before)\n", ""),
    ('ui/handle.c', "    if (key >= UK_ITEM0) return ui_item_cmd(u, (UiMenuItem)(key - UK_ITEM0), out);\n", ""),
    (KY, "                if (!seen && !r.fresh++) {", "                if (!r.fresh++) {"),
    (KY, "    memcpy(ui_clashed.next, now, sizeof now[0] * (size_t)now_n), ui_clashed.next_n = now_n;", ""),
    (KY, "    memcpy(ui_clashed.c, ui_clashed.next, sizeof ui_clashed.c[0] * (size_t)ui_clashed.next_n), ui_clashed.n = ui_clashed.next_n;", ""),
    (KY, '            if (ui_binds.e[i].user) users--;\n', ''),
    ('ui/menus.c', "    static const int MENUS[] = { 0, 1, 2, 3, 4, 5, UI_CTX_TAB };", "    static const int MENUS[] = { 0, 1, 2, 3, 4, 5 };"),
    # F15 step 3: the shortcuts panel's rows of commands, yours marked, found by name.
    (KY, '    return i >= 0 && ui_binds.e[i].user;', '    return i >= 0;'),
    (KY, '    return text ? text : i < 0 ? "" : ui_binds.e[i].name;', '    return text ? text : "";'),
    (KY, "    return UI_KEYS[r].cmd ? ui_cmd_name(UI_KEYS[r].cmd) : \"\";", "    return \"\";"),
    ('ui/panels.c', " ||\n                                                      ui_match(&u->keys_m, str_from_c(ui_keys_name(r)))))", "))"),
    # F15's code-health review, A: typed characters in fields, greyed menu commands, Ctrl+C unbound.
    ('ui/handle.c', "        if (e.key == UK_NONE && e.ch && (ui_line_active(u, NULL) || ui_on_top(u) != UI_ON_NONE || u->menu_on)) e.cmd = UK_NONE;", ""),
    ('ui/handle.c', "(ui_line_active(u, NULL) || ui_on_top(u) != UI_ON_NONE || u->menu_on)) e.cmd = UK_NONE;", "(ui_line_active(u, NULL) || u->menu_on)) e.cmd = UK_NONE;"),
    ('ui/handle.c', "(ui_line_active(u, NULL) || ui_on_top(u) != UI_ON_NONE || u->menu_on)) e.cmd = UK_NONE;", "(ui_line_active(u, NULL) || ui_on_top(u) != UI_ON_NONE)) e.cmd = UK_NONE;"),
    ('ui/menus.c', "                *out = kind && !ui_allows(u, kind) ? ui_view_act(u, (UiAction){ kind, 0, false }) : UI_NONE;", "                *out = UI_NONE;"),
    ('ui/edit.c', "    return (e.key == UK_COPY || e.key == UK_CUT) && e.cmd && lo == hi;", "    return (e.key == UK_COPY || e.key == UK_CUT) && lo == hi;"),
    # F15's code-health review, B: keys from the table in tooltips, hints and the panel.
    (KY, "    return ui_cmd_bind(UI_KEYS[r].cmd, k - (k == 1 && UI_KEYS[r].key));", "    return ui_cmd_bind(UI_KEYS[r].cmd, k);"),
    (KY, "    snprintf(out, cap, k[0] ? \"%s (%s)\" : \"%s\", what, k);", "    snprintf(out, cap, \"%s (%s)\", what, k);"),
    # F15's code-health review, C: menu entries run their commands, Close All from a menu pick.
    ('ui/ui.c', '[UM_NEW_TAB] = { "New Tab", "new-tab", .cmd = UK_TAB_NEW, .runs = true }', '[UM_NEW_TAB] = { "New Tab", "new-tab", .cmd = UK_TAB_NEW }'),
    ('ui/ui.c', '[UM_CLOSE_ALL] = { "Close All Windows", "close-all", .cmd = UK_QUIT_ALL, .runs = true }', '[UM_CLOSE_ALL] = { "Close All Windows", "close-all", .cmd = UK_QUIT_ALL }'),
    # F15's code-health review, D (fs/): file_replace, kv_split, the bookmark line, box_argv.
    ('fs/paths.c', "    bool ok = fchmod(fd, mode) == 0 && fd_write_all(fd, body, n)", "    bool ok = fd_write_all(fd, body, n)"),
    ('fs/paths.c', "    if (eq < 0) return false;\n    *key", "    if (eq < 0) eq = 0;\n    *key"),
    ('fs/places.c', "    *uri = sp < 0 ? line : str_cut(line, 0, sp), *label = sp < 0 ? STR_EMPTY : str_trim(str_cut(line, sp + 1, line.len));", "    *uri = line, *label = STR_EMPTY;"),
    ('fs/sandbox.c', "        if (!j->outlive) av[n++] = \"--die-with-parent\";\n", ""),
    # (the group D review's pin: test_write_all's slow pipe and timer signal)
    ('fs/paths.c', "        if (k < 0 && errno == EINTR) continue;\n", ""),
    ('fs/paths.c', "        at += k;\n    }\n    return true;", "        return k == n;\n    }\n    return true;"),
    # ---- F16 step 2: a far folder's lister (fs/farlist.c), the tree, the footer, spawn_fds's input
    ('fs/farlist.c', "if (line.len < 2 || line.len >= FAR_LINE_MAX || line.p[1] != ' ') return false;", 'if (line.len < 2 || line.len >= FAR_LINE_MAX) return false;'),
    ('fs/farlist.c', "if (*p != '-' && (*p < '0' || *p > '9')) return false;", ''),
    ('fs/farlist.c', 'if (errno || end == p || x < lo || x > hi) return false;', 'if (errno || end == p || x < lo) return false;'),
    ('fs/farlist.c', 'if (errno || end == p || x < lo || x > hi) return false;', 'if (errno || end == p || x > hi) return false;'),
    ('fs/farlist.c', "if (i < n - 1 && *p++ != ' ') return false;", 'if (i < n - 1) p++;'),
    ('fs/farlist.c', '    return *p == 0;\n}', '    return true;\n}'),
    ('fs/farlist.c', 'if (nl == b.len) return nl - s >= FAR_LINE_MAX ? -1 : 0;', 'if (nl == b.len) return 0;'),
    ('fs/farlist.c', 'if (nl == b.len) return nl - s >= FAR_LINE_MAX ? -1 : 0;', 'if (nl == b.len) return nl - s > FAR_LINE_MAX ? -1 : 0;'),
    ('fs/farlist.c', 'if (!far_nums(line, m->v, 1, 0, 1)) return -1;', ''),
    ('fs/farlist.c', 'if (!far_nums(line, m->v, 1, 0, INT64_MAX)) return -1;', ''),
    ('fs/farlist.c', 'if (line.len != 1) return -1;', ''),
    ('fs/farlist.c', "        if (c + 2 >= line.len || line.p[c + 1] != ' ' || !far_nums((Str){ line.p, c }, m->v, 1, 1, 4095)) return -1;", "        if (c + 2 >= line.len || line.p[c + 1] != ' ' || !far_nums((Str){ line.p, c }, m->v, 1, 0, 4095)) return -1;"),   # (retargeted, step D review)
    ('fs/farlist.c', 'if (!far_nums(line, m->v, 3, -1, INT64_MAX) || m->v[0] < 0 || m->v[1] < 0) return -1;', 'if (!far_nums(line, m->v, 3, -1, INT64_MAX)) return -1;'),
    ('fs/farlist.c', 'if (!far_nums(line, m->v, 3, -1, INT64_MAX) || m->v[0] < 0 || m->v[1] < 0) return -1;', 'if (!far_nums(line, m->v, 3, -1, INT64_MAX) || m->v[0] < 0) return -1;'),
    ('fs/farlist.c', 'if (!ok || v[0] > K_OTHER ||', 'if (!ok ||'),   # (retargeted, step 8 A)
    ('fs/farlist.c', 'v[3] > UINT32_MAX || v[4] > UINT32_MAX', 'v[4] > UINT32_MAX'),
    ('fs/farlist.c', 'v[6] < 1 || v[6] > NAME_MAX || v[7] >= PATH_MAX)', 'v[6] > NAME_MAX || v[7] >= PATH_MAX)'),
    ('fs/farlist.c', 'v[6] < 1 || v[6] > NAME_MAX || v[7] >= PATH_MAX)', 'v[6] < 1 || v[7] >= PATH_MAX)'),
    ('fs/farlist.c', 'v[6] < 1 || v[6] > NAME_MAX || v[7] >= PATH_MAX)', 'v[6] < 1 || v[6] > NAME_MAX || v[7] > PATH_MAX)'),
    ('fs/farlist.c', 'if (b.len - next < v[6] + v[7]) return 0;', 'if (b.len - next < v[6]) return 0;'),
    ('fs/farlist.c', 'm->target = (Str){ b.p + next + v[6], v[7] };', 'm->target = (Str){ b.p + next, v[7] };'),
    ('fs/farlist.c', "if (memchr(m->name.p, '/', (size_t)m->name.len) || memchr(m->name.p, 0, (size_t)m->name.len)) return -1;", 'if (memchr(m->name.p, 0, (size_t)m->name.len)) return -1;'),
    ('fs/farlist.c', "if (memchr(m->name.p, '/', (size_t)m->name.len) || memchr(m->name.p, 0, (size_t)m->name.len)) return -1;", "if (memchr(m->name.p, '/', (size_t)m->name.len)) return -1;"),
    ('fs/farlist.c', '        next += v[6] + v[7];\n', '        next += v[6];\n'),
    ('fs/farlist.c', '    *at = next;\n    return 1;', '    *at = nl + 1;\n    return 1;'),
    ('fs/farlist.c', '        if (r < 0) break;\n', ''),
    ('fs/farlist.c', "        if (m.type == 'L' || m.type == 'X') {", "        if (m.type == 'L') {"),
    ('fs/farlist.c', 'if (at < b.len && far_next(b, &at, &m) < 0) return *ended = true,', 'if (false) return *ended = true,'),
    ('fs/farlist.c', 'if (m.type == \'X\') return err_set(err, "%s: %.*s", dir, STR_ARG(m.text)), false;', ''),
    ('fs/farlist.c', "for (; far_next(b, &at, &m) == 1 && m.type != 'L';) {", 'for (; far_next(b, &at, &m) == 1;) {'),
    ('fs/farlist.c', "if (m.type == 'W') *can_write = m.v[0] == 1;", ''),
    ('fs/farlist.c', "if (m.type == 'H') out->hidden = m.v[0];", ''),
    ('fs/farlist.c', '            memcpy(grown, e, (size_t)n * sizeof *e);\n            e = grown, cap *= 2;', '            e = grown, cap *= 2;'),
    ('fs/farlist.c', '.target = target ? (Str){ target, m.target.len } : STR_EMPTY,', '.target = STR_EMPTY,'),
    ('fs/farlist.c', '.mode = (u32)m.v[3], .uid = (u32)m.v[4], .gid = (u32)m.v[5],', '.mode = (u32)m.v[3], .uid = (u32)m.v[5], .gid = (u32)m.v[4],'),
    ('fs/farlist.c', '    out->e = e, out->n = n, *after = at;', '    out->e = e, out->n = n, *after = 0;'),
    ('fs/farlist.c', '    fwrite(e->target.p, 1, (size_t)e->target.len, f);\n', ''),
    ('fs/farlist.c', '            dot++;\n', ''),
    ('fs/farlist.c', '            if (!hidden) continue;\n        }\n        ArenaMark', '        }\n        ArenaMark'),
    ('fs/farlist.c', '    fprintf(out, "W %d\\n", access(dir, W_OK) == 0);', '    fprintf(out, "W %d\\n", 1);'),
    ('fs/farlist.c', "|| line[used] != '\\n' || len < 1 || len > NAME_MAX) break;", '|| len < 1 || len > NAME_MAX) break;'),
    ('fs/farlist.c', 'if (fread(name, 1, (size_t)len, in) != (size_t)len) break;', 'if (fread(name, 1, (size_t)len, in) != (size_t)len) {}'),
    ('fs/farlist.c', "isize n = strlen(name) == (size_t)len && !strchr(name, '/') && snprintf", "isize n = !strchr(name, '/') && snprintf"),
    ('fs/farlist.c', "isize n = strlen(name) == (size_t)len && !strchr(name, '/') && snprintf", 'isize n = strlen(name) == (size_t)len && snprintf'),
    ('fs/farlist.c', '                      ? count_entries(path, hidden)', '                      ? count_entries(path, true)'),
    ('fs/farlist.c', 'if (n < 0 || (size_t)n + (size_t)name.len > cap || name.len < 1 || name.len > NAME_MAX) return 0;', 'if (n < 0 || name.len < 1 || name.len > NAME_MAX) return 0;'),
    ('fs/farlist.c', 'if (n < 0 || (size_t)n + (size_t)name.len > cap || name.len < 1 || name.len > NAME_MAX) return 0;', 'if (n < 0 || (size_t)n + (size_t)name.len > cap || name.len > NAME_MAX) return 0;'),
    ('fs/farlist.c', 'if (n < 0 || (size_t)n + (size_t)name.len > cap || name.len < 1 || name.len > NAME_MAX) return 0;', 'if (n < 0 || (size_t)n + (size_t)name.len > cap || name.len < 1) return 0;'),
    ('fs/tree.c', '    if (t->mounts.n && (!far_real(&t->mounts, dir, real, sizeof real, &far) || far)) return;', ''),   # (retargeted, step C)
    ('ui/helpers.c', '    if (in->opening) {\n        snprintf(buf, cap, "Opening\\xE2\\x80\\xA6");\n        return;\n    }\n', ''),
    ('fs/spawn.c', '    if (in >= 0) posix_spawn_file_actions_adddup2(&fa, in, 0);\n    else posix_spawn_file_actions_addopen', '    posix_spawn_file_actions_addopen'),
    ('fs/spawn.c', '    else if (errs == SPAWN_ERR_NULL) posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);\n', ''),
    ('fs/spawn.c', '    if (errs >= 0) posix_spawn_file_actions_adddup2(&fa, errs, 2);', '    if (errs >= 0) {}'),
    # ---- F16 step 2b: links into far drives, far mount points
    ('fs/list.c', '    if (look && !mounts_local(ms, p)) return *far = true, *st = (struct stat){ .st_mode = S_IFDIR }, K_DIR;\n', ''),   # (retargeted, step C)
    ('fs/list.c', '        if (n > 0 && link_abs(dir, (Str){ t, n }, abs, sizeof abs) && far_real(ms, abs, real, sizeof real, &f) && f) return *far = true, K_LINK_DIR;\n', ''),   # (retargeted, step C)
    ('fs/list.c', '        .far = far,\n', ''),
    ('fs/list.c', '    int n = target.len && target.p[0] == \'/\' ? snprintf(buf, sizeof buf, "%.*s", STR_ARG(target)) :', '    int n = 0 ? snprintf(buf, sizeof buf, "%.*s", STR_ARG(target)) :'),
    ('fs/list.c', "        if (len == 0 || (len == 1 && seg[0] == '.')) continue;\n        if (len == 2 && seg[0] == '.' && seg[1] == '.') {   // (the last part off", "        if (len == 0) continue;\n        if (len == 2 && seg[0] == '.' && seg[1] == '.') {   // (the last part off"),
    ('fs/list.c', "            while (o > 0 && out[o - 1] != '/') o--;\n            if (o > 0) o--;\n", "            while (o > 0 && out[o - 1] != '/') o--;\n"),
    ('fs/list.c', '        if (o + 1 + len + 1 > cap) return false;', '        if (o + 1 + len > cap) return false;'),
    ('fs/list.c', "    if (o == 0) out[o++] = '/';\n", ''),
    ('fs/tree.c', '    if (t->mounts.n && (!far_real(&t->mounts, dir, real, sizeof real, &far) || far)) return;', '    if (t->mounts.n && !far_real(&t->mounts, dir, real, sizeof real, &far)) return;'),   # (retargeted, step C)
    ('fs/tree.c', '    return kind_is_dir(kind_of_far(dirfd, dir, de->d_name, ms, &st, &far));', '    return kind_is_dir(kind_of_far(dirfd, dir, de->d_name, NULL, &st, &far));'),   # (retargeted, step C)
    # (the step 2b review: the far checks from the folder's real path)
    ('fs/list.c', '    const char *at = ms && ms->n && far_real(ms, dir, real, sizeof real, &f) ? real : dir;', '    const char *at = dir;'),   # (retargeted, step C)
    ('fs/tree.c', '    const char *at = t->mounts.n ? real : dir;', '    const char *at = dir;'),   # (retargeted, step C)
    # ---- F16 step 3: far_real, the search walk
    ('fs/list.c', '    if (path[0] != \'/\' && !getcwd(cwd, sizeof cwd)) return false;', '    if (0) return false;'),
    ('fs/list.c', '        if (len == 0 || (len == 1 && seg[0] == \'.\')) continue;\n        if (len == 2 && seg[0] == \'.\' && seg[1] == \'.\') {   // (res is real', '        if (len == 0) continue;\n        if (len == 2 && seg[0] == \'.\' && seg[1] == \'.\') {   // (res is real'),
    ('fs/list.c', '            while (rl > 0 && res[rl - 1] != \'/\') rl--;\n            if (rl > 0) rl--;\n            res[rl] = 0;', '            while (rl > 0 && res[rl - 1] != \'/\') rl--;\n            res[rl] = 0;'),
    ('fs/list.c', '        if (rl + 1 + len + 1 > sizeof cand) return errno = ENAMETOOLONG, false;\n', ''),
    ('fs/list.c', '        if (!mounts_local(ms, cand)) {   // far from here', '        if (0) {   // far from here'),   # (retargeted, step C)
    ('fs/list.c', "            while (*p == '/') p++;   // (relative to cand", "            {}   // (relative to cand"),
    ('fs/list.c', '            return *far = !mounts_local(ms, out), true;', '            return *far = true, true;'),   # (retargeted, step C)
    ('fs/list.c', '        if (!there && !(flags & WALK_MISSING_OK)) return false;', '        if (0) return false;'),   # (retargeted, step C)
    ('fs/list.c', '            if (++links > LINKS_MAX) return errno = ELOOP, false;', '            if (++links > 400000) return errno = ELOOP, false;'),
    ('fs/list.c', '            if (t[0] == \'/\') rl = 0, res[0] = 0;', '            if (0) rl = 0, res[0] = 0;'),
    ('fs/list.c', '        if (*p && !S_ISDIR(st.st_mode) && !(flags & WALK_MISSING_OK)) return errno = ENOTDIR, false;', '        if (0) return errno = ENOTDIR, false;'),   # (retargeted, step C)
    ('fs/list.c', '    if ((rl ? rl : 1) + 1 > cap) return errno = ENAMETOOLONG, false;', '    if (0) return errno = ENAMETOOLONG, false;'),
    ('fs/list.c', '    snprintf(out, cap, "%s", rl ? res : "/");', '    snprintf(out, cap, "%s", res);'),
    ('fs/search.c', '    int n = snprintf(out, cap, "%s%s%s", w->root, w->rel_len && strcmp(w->root, "/") != 0 ? "/" : "", w->rel);', '    int n = snprintf(out, cap, "%s/%s", w->root, w->rel);'),
    # (Dropped as inert: the type filter in search_far, and `t == DT_DIR` returning early. Without
    # them a file only costs a path_put, a parse of the mount list and a failing readlinkat; the
    # answer is the same unless a regular file is itself a far mount point, which a share isn't.)
    ('fs/search.c', '    if (!mounts_local(&w->mounts, p)) return true;', ''),   # (retargeted, step C)
    ('fs/search.c', 'link_abs(dir, (Str){ tg, n }, abs, sizeof abs) && far_real(&w->mounts, abs, real, sizeof real, &far) && far;', 'link_abs(dir, (Str){ tg, n }, abs, sizeof abs) && !mounts_local(&w->mounts, abs);'),   # (retargeted, step C)
    ('fs/search.c', '        if (search_far(w, dirfd(d), nm, t)) {   // (found by its name only', '        if (0) {   // (found by its name only'),
    ('fs/search.c', '            w->s.far++;\n            if (!w->line', '            if (!w->line'),
    ('fs/search.c', '            if (!w->line && w->match(w->ctx, name) && !w->found(', '            if (w->match(w->ctx, name) && !w->found('),
    ('fs/search.c', '    else if (!far_real(&w->mounts, root, w->root, sizeof w->root, &far) || far) {', '    else if (!far_real(&w->mounts, root, w->root, sizeof w->root, &far)) {'),   # (retargeted, step C)
    ('fs/search.c', '        if (far) w->s.far++;\n        else w->s.unread++;', '        w->s.unread++;'),
    ('fs/search.c', '    SEARCH_PART(s->far, "network folder", "network folders")\n', ''),
    ('ui/helpers.c', '    *was = other[--*no];', '    *was = other[*no - 1];'),
    # ---- F16 step 3b: ofx-list --do (fs/fardo.c)
    ('fs/fardo.c', '        fwrite(f[i].p, 1, (size_t)f[i].len, out);\n', ''),
    ('fs/fardo.c', '    fprintf(out, "%s %d\\n", ok ? "ok" : "no", n);', '    fprintf(out, "%s %d\\n", "ok", n);'),
    ('fs/fardo.c', '    if (line.len < 1 || line.len > 18) return false;', '    if (line.len > 18) return false;'),
    ('fs/fardo.c', "        if (line.p[i] < '0' || line.p[i] > '9') return false;", ''),
    ('fs/fardo.c', '    return x <= hi && (*v = x, true);', '    return (*v = x, true);'),
    ('fs/fardo.c', '    if (!next_line(b, &at, &line) || line.len < 4) return false;', '    if (!next_line(b, &at, &line)) return false;'),
    ('fs/fardo.c', '    if (no && v < 1) return false;   // (a refusal says why)', ''),
    ('fs/fardo.c', '    return at == b.len;\n}', '    return true;\n}'),
    ('fs/fardo.c', '        if (!op_rename(argv[1], str_from_c(argv[2]), str_from_c(argv[3]), &err)) return far_reply_err(out, err.msg), 0;', '        if (!op_rename(argv[1], str_from_c(argv[2]), str_from_c(argv[3]), &err)) {}'),
    ('fs/fardo.c', "        bool ok = op[2] == 'd' ? op_mkdir(", "        bool ok = op[2] == 'f' ? op_mkdir("),
    ('fs/fardo.c', "bool ok = name_free(argv[i], str_from_c(argv[i + 2]), argv[i + 1][0] == '1', true, true, names[k], sizeof names[k]);", "bool ok = name_free(argv[i], str_from_c(argv[i + 2]), argv[i + 1][0] == '1', false, true, names[k], sizeof names[k]);"),
    ('fs/fardo.c', '            broken = stat(argv[1], &st) != 0;', '            broken = false;'),
    ('fs/fardo.c', 'broken ? STR_EMPTY : far_bytes(&st, sizeof st)', 'far_bytes(&st, sizeof st)'),
    ('fs/fardo.c', '        Str f[3] = { str_from_c(t.from), str_from_c(t.file), str_from_c(t.info) };', '        Str f[3] = { str_from_c(t.from), str_from_c(t.info), str_from_c(t.file) };'),
    ('fs/fardo.c', '        if (argc < 2 || (argc - 2) % 3 != 0 ||', '        if (argc < 2 ||'),
    ('fs/fardo.c', '        snprintf(was, sizeof was, "%o", (unsigned)old);', '        snprintf(was, sizeof was, "%u", (unsigned)old);'),
    ('fs/fardo.c', '        Str f[2] = { str_from_c(ok ? "done" : err.msg), str_from_c(d) };\n        far_reply(out, ok, 2, f);', '        Str f[2] = { str_from_c(ok ? "done" : err.msg), str_from_c(d) };\n        far_reply(out, true, 2, f);'),
    ('fs/fardo.c', '    if (argc != (n) + 1) {', '    if (argc < (n) + 1) {'),
    # (step 3 review: the batches, far_drive)
    ('fs/fardo.c', '            f[i - 1] = lstat(argv[i], &st) != 0 ? S("-") : S_ISDIR(st.st_mode) ? S("d") : S("o");', '            f[i - 1] = lstat(argv[i], &st) != 0 ? S("-") : S("o");'),
    ('fs/fardo.c', '        if (argc < 4 || (argc - 1) % 3 != 0 ||', '        if (argc < 4 ||'),
    # (Justified: frees' "" for a name not free, as free's was: 10,000 names taken.)
    # (dropped, step C: far_drive asks mounts_at now, whose own mutants cover this)
    ('fs/list.c', '    *gvfs = m->gvfs;\n    if (!*gvfs) return true;', '    *gvfs = m->gvfs;\n    return true;'),   # (retargeted, step C)
    ('fs/list.c', '    while (*rest == \'/\') rest++;\n    size_t k = strcspn', '    size_t k = strcspn'),
    ('fs/list.c', '    if (k && o + 1 + k < cap) out[o] =', '    if (k) out[o] ='),
    # (Dropped as equivalent: a field's length capped at what is left of the answer (past it, the
    # last check that the answer ends there fails too); "" or NULL for an undo item's empty parts
    # (undo_check and undo_do take both as none). Justified: "no free name" needs 10,000 names taken.)
    ('fs/fardo.c', '        far_reply_err(out, "ofx-list --do: no such action");\n        return 2;', '        return 2;'),
    ('ui/helpers.c', '    UiPlace *other = dir < 0 ? h->fwd : h->back;\n    int *no = dir < 0 ? &h->nfwd : &h->nback;\n    if (*no == 0) return false;', '    UiPlace *other = dir < 0 ? h->fwd : h->back;\n    int *no = dir < 0 ? &h->nfwd : &h->nback;'),
    # F16 step 5: gvfs's share folders' names and addresses, the Network places, a share's crumbs, a network bookmark
    ('fs/connect.c', "        if (part.len > k.len && memcmp(part.p, k.p, (size_t)k.len) == 0 && part.p[k.len] == '=') {", '        if (part.len > k.len && memcmp(part.p, k.p, (size_t)k.len) == 0) {'),
    # (dropped, step 8 A: gvfs_key's own %XX loop is percent_decode now, which has its own)
    ('fs/connect.c', 'return percent_decode(str_cut(part, k.len + 1, part.len), out, cap) && out[0];', 'return percent_decode(str_cut(part, k.len + 1, part.len), out, cap);'),   # (retargeted, step 8 A: percent_decode's)
    ('fs/connect.c', '    if (colon < 0 || cap == 0) return false;\n    Str rest', '    if (cap == 0) return false;\n    Str rest'),
    ('fs/connect.c', '    bool share = gvfs_key(name, "share", what, sizeof what) || gvfs_key(name, "volume", what, sizeof what);', '    bool share = gvfs_key(name, "share", what, sizeof what);'),
    ('fs/connect.c', '    bool server = gvfs_key(name, "server", host, sizeof host) || gvfs_key(name, "host", host, sizeof host);', '    bool server = gvfs_key(name, "server", host, sizeof host);'),
    ('fs/connect.c', '    else if (server && gvfs_key(name, "user", user, sizeof user)) snprintf(out, cap, "%s@%s", user, host);', ''),
    ('fs/connect.c', '    while (k < COUNT(KIND) && !str_starts_with(name, str_from_c(KIND[k].type))) k++;', '    while (k < COUNT(KIND) - 1 && !str_starts_with(name, str_from_c(KIND[k].type))) k++;'),   # (retargeted, step F)
    ('fs/connect.c', '    bool hd = hu && gvfs_key(name, "domain", domain, sizeof domain);', '    bool hd = gvfs_key(name, "domain", domain, sizeof domain);'),   # (retargeted, step F)
    ('fs/connect.c', "        else first[0] = '/';", '        else {}'),   # (retargeted, step F)
    ('fs/connect.c', 'hp ? port : "", esc);\n    return n > 0 && (size_t)n < cap;\n}', 'hp ? port : "", esc);\n    return n > 0;\n}'),   # (retargeted, step 7a)
    ('fs/places.c', "        if (line.len == 0 || line.len >= PATH_MAX || line.p[0] != '/') continue;", '        if (line.len == 0 || line.len >= PATH_MAX) continue;'),
    ('fs/places.c', '        if (label.len == 0) snprintf(name, sizeof name, share[0] ? "%s on %s" : "%s%s", share, host), label = str_from_c(name);', ''),   # (retargeted, step 7a)
    ('fs/places.c', '    if (pl->n == head + 1) pl->n = head;   // (nothing in it: no heading)', ''),
    ('fs/places.c', '&& place_is_net(pl->p[i].kind) == place_is_net(kind) && strcmp', '&& strcmp'),
    ('fs/places.c', '    return place_ejectable(p) || p->kind == PL_SHARE;', '    return place_ejectable(p);'),
    ('fs/places.c', '    case PL_SHARE:\n    case PL_NETBM: names[n++] = S("folder-remote"); break;', ''),
    ('ui/helpers.c', '    if (sh.len > 1 && sh.p[0] == \'/\' && ui_under(cw, sh) && label) {', '    if (sh.len > 1 && sh.p[0] == \'/\' && label) {'),
    ('ui/helpers.c', '    if (sh.len > 1 && sh.p[0] == \'/\' && ui_under(cw, sh) && label) {', '    if (sh.len > 1 && sh.p[0] == \'/\' && ui_under(cw, sh)) {'),
    ('ui/helpers.c', '    if (c->seg[i].share) return str_from_c(c->share_label);', ''),
    # (Dropped, not observable: places_load_net's skip of an address connect_uri refuses. Its buffer
    # then still holds the last good address, which places_has drops as a second row.)
    ('fs/connect.c', "        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')) return false;", ''),
    ('fs/connect.c', "    return strchr(colon, '=') != NULL;", '    return true;'),
    ('fs/connect.c', '    if (!colon || colon == n) return false;', '    if (!colon) return false;'),
    ('fs/connect.c', '        if (!mountinfo_line(line, mp, sizeof mp, &type, &src) || !str_eq(type, S("fuse.gvfsd-fuse"))) continue;', '        if (!mountinfo_line(line, mp, sizeof mp, &type, &src)) continue;'),
    ('fs/connect.c', '    qsort(line_of, (size_t)k, sizeof *line_of, gvfs_cmp);', ''),
    ('fs/connect.c', '    fprintf(out, ".\\n");\n    return true;', '    return true;'),
    ('fs/connect.c', '    if (mi.len == 0) return false;\n    char *line_of', '    char *line_of'),
    # F16 step 4: Connect to Server's address and what gio says (fs/connect.c), the question's keys (ui/)
    ('fs/connect.c', '        if (tolower((unsigned char)a.p[i]) != (unsigned char)b[i]) return false;', '        if ((unsigned char)a.p[i] != (unsigned char)b[i]) return false;'),
    ('fs/connect.c', '        if ((unsigned char)text.p[i] < 0x20 || text.p[i] == 0x7F) return', '        if ((unsigned char)text.p[i] < 0x20) return'),
    ('fs/connect.c', '    if ((size_t)text.len >= ucap) return', '    if (0) return'),
    ('fs/connect.c', "        if (str_find(str_cut(auth, 0, at), ':') >= 0) return", '        if (0) return'),
    ('fs/connect.c', '        bool digits = port.len > 0 && port.len <= 5;', '        bool digits = port.len <= 5;'),
    ('fs/connect.c', "        for (isize i = 0; i < port.len; i++) digits = digits && port.p[i] >= '0' && port.p[i] <= '9';", ''),
    ('fs/connect.c', '    if (name.len == 0) return', '    if (0) return'),
    ('fs/connect.c', '    if (sh.len == 0 && (!s->server || path.len > 0)) return', '    if (0) return'),   # (retargeted, step F)
    ('fs/connect.c', '    if ((size_t)name.len >= hcap || (size_t)sh.len >= scap) return', '    if ((size_t)name.len >= hcap) return'),
    ('fs/connect.c', '    if ((size_t)name.len >= hcap || (size_t)sh.len >= scap) return', '    if ((size_t)sh.len >= scap) return'),
    ('fs/connect.c', '        sh = end < 0 ? path : str_cut(path, 0, end);', '        sh = path;'),   # (retargeted, step 6)
    ('fs/connect.c', '; i += k + HEAD.len) ++*rounds, last = i + k;', '; i += k + HEAD.len) *rounds = 1, last = i + k;'),   # (retargeted, step F)
    ('fs/connect.c', '    if (*rounds == 0) return GIO_ASK_NONE;', ''),
    ('fs/connect.c', 'S("passphrase")) >= 0 ? GIO_ASK_KEY : GIO_ASK_PASSWORD;', 'S("passphrase")) >= 0 ? GIO_ASK_KEY : GIO_ASK_USER;'),   # (retargeted, step F)
    ('fs/connect.c', '    if (open < 1 || line.p[open - 1] != \' \') return GIO_ASK_NONE;', '    if (open < 1) return GIO_ASK_NONE;'),
    ('fs/connect.c', '    GioAsk k = str_ends_with(label, S("User")) ? GIO_ASK_USER : str_ends_with(label, S("Domain")) ? GIO_ASK_DOMAIN : GIO_ASK_NONE;', '    GioAsk k = str_ends_with(label, S("User")) ? GIO_ASK_USER : GIO_ASK_DOMAIN;'),
    ('fs/connect.c', '    if (k != GIO_ASK_NONE && cap) snprintf(def, cap, "%.*s", STR_ARG(d));', ''),
    ('fs/connect.c', 'S("\\ngio: "))) >= 0; i += k + 1) at = i + k + 1;', 'S("\\ngio: "))) >= 0; i += k + 1) at = at < 0 ? i + k + 1 : at;'),   # (retargeted, step F)
    ('fs/connect.c', '    isize at = str_starts_with(err, S("gio: ")) ? 0 : -1;', '    isize at = -1;'),   # (retargeted, step F)
    ('fs/connect.c', '    if (end >= 0) line = str_cut(line, sch + 3 + end + 2, line.len);\n', ''),   # (retargeted, step F)
    ('fs/connect.c', '    if (str_starts_with(line, SMB) && line.len > SMB.len) line = str_cut(line, SMB.len, line.len);', ''),
    ('fs/connect.c', '    if (str_starts_with(line, SMB) && line.len > SMB.len) line', '    if (str_starts_with(line, SMB)) line'),
    ('fs/connect.c', "    if (nl >= 0) line = str_cut(line, 0, nl);\n    // \"smb", "    // \"smb"),
    ('fs/connect.c', '    isize i = str_starts_with(info, KEY) ? 0 : str_find_str(info, S("\\nlocal path: "));', '    isize i = str_starts_with(info, KEY) ? 0 : str_find_str(info, KEY);'),   # (retargeted, step F)
    ('fs/connect.c', "    if (p.len == 0 || p.p[0] != '/' || (size_t)p.len >= cap) return false;\n    snprintf(path, cap,", '    if (p.len == 0 || (size_t)p.len >= cap) return false;\n    snprintf(path, cap,'),   # (retargeted, step F)
    ('fs/connect.c', "    if (p.len == 0 || p.p[0] != '/' || (size_t)p.len >= cap) return false;\n    snprintf(path, cap,", "    if (p.len == 0 || p.p[0] != '/') return false;\n    snprintf(path, cap,"),   # (retargeted, step F)
    ('ui/edit.c', '    case UK_COPY: if (!l.secret) ui_line_copy(u, l); return true;', '    case UK_COPY: ui_line_copy(u, l); return true;'),
    ('ui/edit.c', '        if (!l.secret && ui_line_copy(u, l)) ui_line_cut(l, lo, hi);', '        if (ui_line_copy(u, l)) ui_line_cut(l, lo, hi);'),
    ('ui/edit.c', '    if (l.secret) return dir < 0 ? 0 : *l.len;', ''),
    ('ui/edit.c', '    l.secret = true;\n    return l;', '    return l;'),
    ('ui/edit.c', '    else if (on == UI_ON_LOGIN) l = ui_login_line(u, u->login_at);', ''),
    ('ui/panels.c', '    while (!ui_login_shown(u, u->login_at));', '    while (0);'), # (retargeted, step 7c)
    ('ui/panels.c', '    case UK_TAB: case UK_DOWN: ui_login_step(u, 1); break;', '    case UK_DOWN: ui_login_step(u, 1); break;'),
    ('ui/panels.c', "    case UK_MENU: if (e.ch == 'g' && ui_login_guest(u)) return ui_login_close(u, UA_LOGIN_GUEST); break;", '    case UK_MENU: return ui_login_close(u, UA_LOGIN_GUEST);'),   # (retargeted, step E)
    ('ui/panels.c', '    ui_panel_close(u, UI_PANEL_LOGIN);\n    return (UiAction){ k, 0, false };', '    return (UiAction){ k, 0, false };'),
    ('ui/handle.c', ' && ui_login_shown(u, in->login_field)) u->login_at = in->login_field;', ') u->login_at = in->login_field;'),   # (retargeted, step 8 A)
    ('ui/handle.c', '        if (in->modal == UI_MODAL_GUEST && ui_login_guest(u)) return *out = ui_login_close(u, UA_LOGIN_GUEST), true;', ''),   # (retargeted, step E)
    ('ui/menus.c', '        u->edit.anchor = u->edit.caret;\n        return UI_NONE;', '        return UI_NONE;'),
    # F16 step 6: nfs:// addresses, an export's name and address
    ('fs/connect.c', '    { "nfs", false, false, false, NET_NAMES_FOLDER },   // (step 6)\n', ''),   # (retargeted, step F)
    ('fs/connect.c', '        if (!s->user) return snprintf(why, wcap, "No user in an NFS address', '        if (0) return snprintf(why, wcap, "No user in an NFS address'),   # (retargeted, step F)
    ('fs/connect.c', '        if (!s->port) return', '        if (0) return'),   # (retargeted, step F)
    ('fs/connect.c', 'kind, s->names == NET_NAMES_FOLDER ? "folder" : s->names == NET_NAMES_NONE ? "" : "share"', 'kind, s->names == NET_NAMES_NONE ? "" : "share"'),   # (retargeted, step F)
    ('fs/connect.c', "        while (path.len > 0 && path.p[path.len - 1] == '/') path.len--;\n", ''),
    ('fs/connect.c', "        sh = str_cut(path, str_find_last(path, '/') + 1, path.len);", '        sh = path;'),
    ('fs/connect.c', '    if (!share && gvfs_key(name, "prefix", prefix, sizeof prefix)) {', '    if (!share && false && gvfs_key(name, "prefix", prefix, sizeof prefix)) {'),
    ('fs/connect.c', "        while (pre.len > 1 && pre.p[pre.len - 1] == '/') pre.len--;\n", ''),
    ('fs/connect.c', '        share = base.len > 0 && (size_t)base.len < sizeof what;', '        share = (size_t)base.len < sizeof what;'),
    ('fs/connect.c', '        { "nfs:", "nfs", "host", "prefix", false },\n', ''),   # (retargeted, step F)
    ('fs/connect.c', "        if (prefix && first[1] != '/') return false;\n", ''),   # (retargeted, step F)
    ('fs/connect.c', "    while (fl > 0 && first[fl - 1] == '/') first[--fl] = 0;", '    while (0) first[--fl] = 0;'),   # (retargeted, step F)
    ('fs/connect.c', 'fl == 0 && !rest[0] ? "/" : rest)', 'rest)'),   # (retargeted, step F)
    ('fs/connect.c', 'hp ? port : "", esc);\n    return n > 0 && (size_t)n < cap;\n}', 'hp ? port : "", esc);\n    return n > 0;\n}'),   # (retargeted, step F)
    # F16 step 7a: a server's address, its shares, the panel
    ('fs/connect.c', '    if (sh.len == 0 && (!s->server || path.len > 0)) return', '    if (sh.len == 0) return'),   # (retargeted, step F)
    ('fs/connect.c', '    if (sh.len == 0 && (!s->server || path.len > 0)) return', '    if (sh.len == 0 && !s->server) return'),   # (retargeted, step F)
    ('fs/connect.c', '        if (t1 <= 0) continue;', '        if (t1 < 0) continue;'),
    ('fs/connect.c', '        if (t3 >= 0) kind = str_cut(kind, 0, t3);', ''),
    ('fs/connect.c', "        if (!str_eq(kind, S(\"(mountable)\")) || name.p[name.len - 1] == '$' ||", "        if (name.p[name.len - 1] == '$' ||"),
    ('fs/connect.c', "        if (!str_eq(kind, S(\"(mountable)\")) || name.p[name.len - 1] == '$' ||", "        if (!str_eq(kind, S(\"(mountable)\")) ||"),
    ('fs/connect.c', "        for (isize i = 0; i < name.len; i++) ctl = ctl || (unsigned char)name.p[i] < 0x20 || name.p[i] == 0x7F;", ''),
    ('fs/connect.c', '    qsort(names, (size_t)n, SERVER_SHARE_NAME, server_share_cmp);', ''),
    ('fs/connect.c', '    return strcasecmp((const char *)a, (const char *)b);', '    return strcmp((const char *)a, (const char *)b);'),
    ('fs/connect.c', "    for (Str line; n < max && next_line(out, &at, &line);) {\n        isize t1", "    for (Str line; next_line(out, &at, &line);) {\n        isize t1"),   # (retargeted, step 7b)
    ('fs/connect.c', "    while (s.len > 0 && s.p[s.len - 1] == '/') s.len--;\n    int n = snprintf(out, cap, \"%.*s/%s\"", "    int n = snprintf(out, cap, \"%.*s/%s\""),
    ('fs/connect.c', '    int n = snprintf(out, cap, "%.*s/%s", STR_ARG(s), share);\n    return n > 0 && (size_t)n < cap;', '    int n = snprintf(out, cap, "%.*s/%s", STR_ARG(s), share);\n    return n > 0;'),
    ('fs/places.c', 'share[0] ? "%s on %s" : "%s%s", share, host)', '"%s on %s", share, host)'),
    # (dropped, step E: the pickers share one path now, whose own mutant is kept)
    ('ui/panels.c', '        ui_picker_close(u, p);\n        return (UiAction){ k.chose', '        return (UiAction){ k.chose'),   # (retargeted, step E)
    # (dropped, step E: the pickers share one path now, whose own mutant is kept)
    # (dropped, step E: the pickers share one path now, whose own mutant is kept)
    # (dropped, step E: the pickers share one path now, whose own mutant is kept)
    ('ui/handle.c', '    case UI_ON_BRANCHES: case UI_ON_SHARES: *out = ui_picker_do(', '    case UI_ON_BRANCHES: *out = ui_picker_do('),   # (retargeted, step E)
    # (dropped, step E: the pickers share one path now, whose own mutant is kept)
    ('ui/ui.c', 'on == UI_ON_SHARES ? UI_PANEL_SHARES : UI_PANEL_NONE;', 'UI_PANEL_NONE;'),   # (retargeted, step E)
    # F16 step 7b: servers on the network
    ('fs/connect.c', "        if (f.p[i] != '\\\\' || i + 1 >= f.len) { out[o++] = f.p[i]; continue; }", "        if (f.p[i] != '\\\\') { out[o++] = f.p[i]; continue; }"),
    ('fs/connect.c', "            if (v > 0 && v < 256) out[o++] = (char)v;", "            out[o++] = (char)v;"),
    ('fs/connect.c', "            i += 3;\n        } else out[o++] = f.p[++i];", "            i += 2;\n        } else out[o++] = f.p[++i];"),
    ('fs/connect.c', "        } else out[o++] = f.p[++i];", "        } else out[o++] = f.p[i];"),
    ('fs/connect.c', "    return strcasecmp(((const NetServer *)a)->name, ((const NetServer *)b)->name);", "    return strcmp(((const NetServer *)a)->name, ((const NetServer *)b)->name);"),
    ('fs/connect.c', "        if (k < 9 || !str_eq(f[4], S(\"_smb._tcp\")) ||", "        if (k < 9 ||"),
    ('fs/connect.c', "        if (k < 9 || !str_eq(f[4], S(\"_smb._tcp\")) ||", "        if (!str_eq(f[4], S(\"_smb._tcp\")) ||"),
    # (Dropped from the code, shown redundant by surviving mutants: the "=" check (a resolved line alone has
    # nine fields or more) and a port's length (one past five digits fails the range check).)
    ('fs/connect.c', "        for (isize i = 0; i < f[6].len; i++) ok = ok && (isalnum((unsigned char)f[6].p[i]) || f[6].p[i] == '.' || f[6].p[i] == '-');", ""),
    ('fs/connect.c', "        for (isize i = 0; i < f[8].len; i++) ok = ok && isdigit((unsigned char)f[8].p[i]);", ""),
    ('fs/connect.c', "        for (const char *c = v.name; *c && ok; c++) ok = (unsigned char)*c >= 0x20 && *c != 0x7F;", ""),
    ('fs/connect.c', "        if (port <= 0 || port > 65535) continue;", ""),
    ('fs/connect.c', "        int w = port == 445 ? snprintf(", "        int w = port == 0 ? snprintf("),
    ('fs/connect.c', "        for (int j = 0; j < n && !seen; j++) seen = strcmp(s[j].addr, v.addr) == 0;", ""),
    ('fs/connect.c', "    qsort(s, (size_t)n, sizeof *s, net_server_cmp);", ""),
    ('fs/places.c', "    return k == PL_SHARE || k == PL_NETBM || k == PL_SERVER;", "    return k == PL_SHARE || k == PL_NETBM;"),
    ('fs/places.c', "    return k == PL_NETBM || k == PL_SERVER;", "    return k == PL_NETBM;"),
    ('fs/places.c', "    case PL_SERVER: names[n++] = S(\"network-server\"), names[n++] = S(\"folder-remote\"); break;", ""),
    ('fs/places.c', "    for (int i = 0; i < n; i++) places_add(pl, PL_SERVER, servers[i].addr, str_from_c(servers[i].name));", ""),
    ('fs/places.c', '    return k == PL_UNMOUNTED || place_connects(k);', '    return k == PL_UNMOUNTED;'),   # (retargeted, step F)
    ('fs/places.c', 'pl->p[i].kind == PL_TRASH || place_folderless(pl->p[i].kind)) continue;', 'pl->p[i].kind == PL_TRASH) continue;'),
    # F16 step 7c: SFTP
    ('fs/connect.c', '    { "sftp", true, true, false, NET_NAMES_NONE },     // (step 7c)\n', ''),   # (retargeted, step F)
    ('fs/connect.c', '    Str rest = str_cut(text, sep + 3, text.len);', '    Str rest = str_cut(text, 6, text.len);'),   # (retargeted, step F)
    ('fs/connect.c', '        user = str_cut(auth, 0, at);\n', ''),
    ('fs/connect.c', '        if (user.len) snprintf(host, hcap, "%.*s@%.*s", STR_ARG(user), STR_ARG(name));\n        else snprintf', '        snprintf'),
    ('fs/connect.c', '        if ((size_t)(user.len + 1 + name.len) >= hcap) return snprintf(why, wcap, "The address is too long"), CONNECT_BAD;\n', ''),
    ('fs/connect.c', '    return sh.len == 0 ? CONNECT_SERVER : CONNECT_OK;', '    return CONNECT_OK;'),
    ('fs/connect.c', 'kind, s->names == NET_NAMES_FOLDER ? "folder" : s->names == NET_NAMES_NONE ? "" : "share"', 'kind, s->names == NET_NAMES_FOLDER ? "folder" : "share"'),   # (retargeted, step F)
    ('fs/connect.c', '        if (scap) share[0] = 0;\n        return CONNECT_OK;', '        return CONNECT_OK;'),
    ('fs/connect.c', '    if (str_ends_with(out, S("\\nChoice: "))) return GIO_ASK_CHOICE;\n', ''),
    ('fs/connect.c', 'S("passphrase")) >= 0 ? GIO_ASK_KEY : GIO_ASK_PASSWORD;', 'S("passphrase")) >= 0 ? GIO_ASK_PASSWORD : GIO_ASK_PASSWORD;'),   # (retargeted, step F)
    ('fs/connect.c', '; i += k + HEAD.len) ++*rounds, last = i + k;', '; i += k + HEAD.len) ++*rounds;'),   # (retargeted, step F)
    ('fs/connect.c', '    if (str_ends_with(out, S("\\nUser: "))) return GIO_ASK_USER;', ''),
    # (Dropped from the code, shown unneeded by a surviving mutant: "User: " after a space; gio put it
    # after a newline in every run measured.)
    ('fs/connect.c', '        { "sftp:", "sftp", "host", NULL, true },\n', ''),   # (retargeted, step F)
    # (dropped, step F: the table rewrote this; its rows are covered by the unit mutants on gvfs_share_uri's one path)
    ('fs/connect.c', '    int n = snprintf(out, cap, "%s://%s%s%s%s%s%s%s%s", scheme, hd ? domain : "", hd ? ";" : "", hu ? user : "", hu ? "@" : "",', '    int n = snprintf(out, cap, "%s://%s%s%s%s%s%s%s%s", scheme, hd ? domain : "", hd ? ";" : "", "", "",'),   # (retargeted, step F)
    ('fs/connect.c', '    if (k == COUNT(KIND) || !gvfs_key(name, KIND[k].host, host, sizeof host)) return false;', '    if (k == COUNT(KIND)) return false;\n    gvfs_key(name, KIND[k].host, host, sizeof host);'),   # (retargeted, step F)
    ('fs/places.c', '        if (c != CONNECT_OK && c != CONNECT_SERVER) continue;', '        if (c != CONNECT_OK) continue;'),
    ('ui/panels.c', '    if (u->login_kind == UI_LOGIN_KEY) return f == UI_LOGIN_PASS;\n', ''),   # (retargeted, step E)
    ('ui/panels.c', ' && !(f == UI_LOGIN_DOMAIN && u->login_kind == UI_LOGIN_SSH);', ';'),   # (retargeted, step E)
    ('ui/panels.c', "    case UK_MENU: if (e.ch == 'g' && ui_login_guest(u)) return", "    case UK_MENU: if (e.ch == 'g') return"),   # (retargeted, step E)
    ('ui/handle.c', '        if (in->modal == UI_MODAL_GUEST && ui_login_guest(u)) return', '        if (in->modal == UI_MODAL_GUEST) return'),   # (retargeted, step E)
    # F16 step 7d: Airplane Mode, and the share poll's back-off
    ('fs/state.c', '        if (str_eq(key, S("airplane"))) s.airplane = str_eq(val, S("on"));\n', ''),
    ('fs/state.c', 's.airplane = str_eq(val, S("on"));', 's.airplane = true;'),
    ('fs/places.c', '    if (airplane) {\n        places_add(pl, PL_AIRPLANE', '    if (false) {\n        places_add(pl, PL_AIRPLANE'),
    ('fs/places.c', '        places_add(pl, PL_AIRPLANE, "airplane:", S("Airplane mode"));\n        return;\n', '        places_add(pl, PL_AIRPLANE, "airplane:", S("Airplane mode"));\n'),
    ('fs/places.c', '    return place_brings(k) || k == PL_AIRPLANE;', '    return place_brings(k);'),   # (retargeted, step F)
    ('fs/places.c', '    case PL_AIRPLANE: names[n++] = S("airplane-mode"), names[n++] = S("airplane-mode-symbolic"); break;', '    case PL_AIRPLANE: break;'),
    ('fs/connect.c', '    if (reset || prev <= 0) return base;', '    if (prev <= 0) return base;'),
    ('fs/connect.c', '    if (reset || prev <= 0) return base;', '    if (reset) return base;'),
    ('fs/connect.c', '    return prev > most / 2 ? most : prev * 2;', '    return prev * 2;'),
    ('fs/connect.c', '    return prev > most / 2 ? most : prev * 2;', '    return prev > most ? most : prev * 2;'),
    ('ui/menus.c', "    if (k == UI_CTX_AIRPLANE) return it == UM_AIRPLANE_OFF_IT;   // (F16 step 7d: the sidebar's row)\n", ''),   # (retargeted, step F)
    ('ui/menus.c', '    case UM_AIRPLANE: return u->airplane;\n', ''),
    ('ui/menus.c', 'case UM_LOCATION: case UM_CONNECT: case UM_AIRPLANE: return true;', 'case UM_LOCATION: case UM_CONNECT: return true;'),
    ('ui/menus.c', '        case UM_AIRPLANE_OFF_IT: return u->airplane = false, (UiAction){ UA_AIRPLANE, 0, false };', '        case UM_AIRPLANE_OFF_IT: return (UiAction){ UA_AIRPLANE, 0, false };'),
    ('ui/menus.c', '    case UM_AIRPLANE: return u->airplane = !u->airplane, (UiAction){ UA_AIRPLANE, 0, false };', '    case UM_AIRPLANE: return u->airplane = true, (UiAction){ UA_AIRPLANE, 0, false };'),
    # (dropped, step F: ctx_airplane is ctx_kind now: its reset is #1189's)
    ('ui/ui.c', '    if (u->airplane_asking) return UI_ON_AIRPLANE;\n', ''),
    ('ui/prompts.c', ' || on == UI_ON_CLEAR || on == UI_ON_AIRPLANE;', ' || on == UI_ON_CLEAR;'),
    ('ui/prompts.c', 'return yes ? (u->airplane = false, (UiAction){ UA_AIRPLANE, 0, false }) : UI_NONE;', 'return (u->airplane = false, (UiAction){ UA_AIRPLANE, 0, false });'),
    ('ui/prompts.c', '    case UI_ON_AIRPLANE: u->airplane_asking = false; return', '    case UI_ON_AIRPLANE: return'),
    ('ui/handle.c', '        if (yes_no && ui_is_question(ui_on_top(u))) return', '        if (yes_no && false) return'),
    ('ui/keys.c', ' { UK_ITEM0 + UM_AIRPLANE, "airplane-mode" },', ''),
    ('fs/icons.c', 'is_svg = (svg || str_ends_with(s, S("-symbolic.svg"))) && str_ends_with', 'is_svg = svg && str_ends_with'),
    ('fs/places.c', 'S("airplane-mode"), names[n++] = S("airplane-mode-symbolic"); break;', 'S("airplane-mode"); break;'),
    # The milestone's code-health cleanup, step A
    ('fs/farlist.c', '        for (int k = 0; ok && k < 8; k++) ok = k == 2 || v[k] >= 0;\n', ''),
    ('fs/farlist.c', 'ok = k == 2 || v[k] >= 0;', 'ok = v[k] >= 0;'),
    ('fs/props.c', '        if (w->pts_len && du_mount_point(w, e->d_name)) {', '        if (false) {'),
    ('fs/props.c', 'strcmp(b + base + 1, name) == 0) return true;', 'true) return true;'),
    ('fs/props.c', '        if (!realpath(roots[i], w->path)) snprintf(w->path, sizeof w->path, "%s", roots[i]);', '        snprintf(w->path, sizeof w->path, "%s", roots[i]);'),
    ('fs/props.c', '            if (w->pts_len + k > w->pts_cap) {', '            if (false) {'),
    ('fs/props.c', '                s->cut = true;\n                break;', '                break;'),
    # The milestone's code-health cleanup, step C
    ('fs/clean.c', 'path_walk(ms, path, out, cap, WALK_MISSING_OK | WALK_STOP_FAR, &far) && !far;', 'path_walk(ms, path, out, cap, WALK_MISSING_OK | WALK_STOP_FAR, &far);'),   # (retargeted, step C review)
    ('fs/clean.c', 'path_walk(ms, path, out, cap, WALK_MISSING_OK | WALK_STOP_FAR, &far)', 'path_walk(ms, path, out, cap, WALK_STOP_FAR, &far)'),   # (retargeted, step C review)
    ('fs/clean.c', 'path_walk(ms, path, out, cap, WALK_MISSING_OK | WALK_STOP_FAR, &far)', 'path_walk(ms, path, out, cap, WALK_MISSING_OK, &far)'),
    ('fs/list.c', '            if (flags & WALK_STOP_FAR) return snprintf(out, cap, "%s", cand), *far = true, true;\n', ''),
    ('fs/paths.c', '        if (!mountinfo_line(line, mp, sizeof mp, &type, &src) || !type.len) continue;', '        if (!mountinfo_line(line, mp, sizeof mp, &type, &src)) continue;'),
    ('fs/paths.c', ', mount_far_type(type), str_eq(type, S("fuse.gvfsd-fuse")) };', ', mount_far_type(type), false };'),
    # The milestone's code-health cleanup, step D: the lister's refusal carries its errno
    ('fs/farlist.c', 'fprintf(out, "X %d: %s\\n", errno, strerror(errno))', 'fprintf(out, "X %d: %s\\n", 1, strerror(errno))'),   # (retargeted, step D review)
    ('fs/farlist.c', '        m->text = (Str){ line.p + c + 2, line.len - c - 2 };', '        m->text = (Str){ line.p + 2, line.len - 2 };'),   # (retargeted, step D review)
    ('fs/farlist.c', "        if (c + 2 >= line.len || line.p[c + 1] != ' ' ||", "        if (c + 2 > line.len || line.p[c + 1] != ' ' ||"),   # (retargeted, step D review)
    # The step C review: the mount list's text in an arena of its own
    ('fs/paths.c', '    arena_restore(tmp, m);\n    return ms;', '    return ms;'),
    ('fs/paths.c', '    Mounts ms = mounts_parse(a, read_file(tmp, path));', '    Mounts ms = mounts_parse(a, read_file(a, path));'),
    ('fs/farlist.c', "        if (c + 2 >= line.len || line.p[c + 1] != ' ' ||", "        if (c + 2 >= line.len ||"),
    # The milestone's code-health cleanup, step E: the pickers' table, ui_login_open
    ('ui/ui.c', 'p == UI_PANEL_SHARES ? &u->shp : NULL;', 'NULL;'),
    ('ui/panels.c', '    return u->login_kind == UI_LOGIN_SHARE;', '    return true;'),
    ('ui/panels.c', '    if (!again) {\n        ui_line_set(ui_login_line(u, UI_LOGIN_USER), def_user);', '    if (1) {\n        ui_line_set(ui_login_line(u, UI_LOGIN_USER), def_user);'),
    ('ui/panels.c', '    u->login_at = ask_user && kind != UI_LOGIN_KEY && u->login_user.len == 0 ? UI_LOGIN_USER : UI_LOGIN_PASS;', '    u->login_at = UI_LOGIN_USER;'),
    ('ui/panels.c', '    u->panel = UI_PANEL_LOGIN;\n    ui_edit_close(u), u->menu_on = false;\n}', '    u->panel = UI_PANEL_LOGIN;\n}'),
    # The milestone's code-health cleanup, step F: the place kinds, the schemes, str_find_str
    ('base/str.c', '    for (isize i = 0; i + w.len <= s.len; i++)\n        if (memcmp(s.p + i, w.p, (size_t)w.len) == 0) return i;', '    for (isize i = 0; i + w.len < s.len; i++)\n        if (memcmp(s.p + i, w.p, (size_t)w.len) == 0) return i;'),
    ('fs/places.c', '    return place_brings(k) || k == PL_AIRPLANE;', '    return k == PL_AIRPLANE;'),
    ('fs/connect.c', '    if (strcmp(scheme, "dav") == 0 && gvfs_key(name, "ssl", ssl, sizeof ssl) && strcmp(ssl, "true") == 0) scheme = "davs";\n', ''),
    ('fs/connect.c', '        if (strcmp(KIND[k].type, "dav:") != 0) return false;', '        {}'),
    ('fs/connect.c', '    isize close = auth.len > 0 && auth.p[0] == \'[\' ? str_find(auth, \']\') : -1;', '    isize close = -1;'),
    ('fs/connect.c', '(close < 2 || (close + 1 < auth.len && auth.p[close + 1] != \':\')))', '(close < 2))'),
    ('fs/connect.c', '    if (!percent_decode(sh, share, scap) || !share[0]) snprintf(share, scap, "%.*s", STR_ARG(sh));', '    snprintf(share, scap, "%.*s", STR_ARG(sh));'),
]
