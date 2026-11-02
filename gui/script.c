// OFX_GUI_SCRIPT=FILE: the input comes from a script instead of the user, one frame per loop, so
// the window can be tested and screenshotted on a headless output without touching anyone's
// keyboard or pointer (CLAUDE.md, "Testing the GUI"). Included by main.c after its state.
//
// One command per line; # starts a comment.
//   settle                  wait until the window's size has held for 20 frames (the compositor
//                           resizes a new window after its first frames)
//   wait <frames>
//   sleep <ms>              frames until that much time has passed
//   key <name>              up down pageup pagedown home end enter right left backspace esc tab
//                           f2 f9 delete shift-delete space, and the chords alt-left alt-right ctrl-f
//                           ctrl-h ctrl-a ctrl-c ctrl-x ctrl-v ctrl-z ctrl-shift-n ctrl-l alt-up
//                           ctrl-t ctrl-w ctrl-tab ctrl-shift-tab ctrl-shift-pageup ctrl-shift-pagedown,
//                           shift-left shift-right shift-home shift-end shift-enter ctrl-k ctrl-j ctrl-q,
//                           ctrl-1 ctrl-2 ctrl-left ctrl-right (a word in a field), ctrl-shift-left ctrl-shift-right,
//                           ctrl-backspace ctrl-delete, ctrl-equal ctrl-minus ctrl-0, and alt-1 .. alt-9;
//                           f10, alt-<letter> (a menu), ctrl-n, and close-window (the compositor's
//                           close, as SIGUSR1 from another window's Close All); ctrl-q is Close All;
//                           ctrl-i (Properties, F7.2), alt-enter (a terminal), ctrl-m (Make Link),
//                           ctrl-p ctrl-g (Show Permissions, Show Group), ctrl-shift-f (Search, F9), ctrl-enter (Open Containing Folder)
//   char <c>                one typed character
//   keys <word>...          keys and characters in one frame's input, as fast typing during a slow
//                           frame gives them: each word a key name (as for `key`) or char:<c>
//   type <text>             the rest of the line, after one space, as typed characters
//   wheel <notches> [ctrl]  over the list; positive away from the user (up); with ctrl at the
//                           window's right edge, as the mouse's wheel goes (wheel_at): the zoom
//   press <row> [ctrl|shift] a left press on list row <row> (0 = the first on screen; in the icon
//                           view the cell, across then down), through the
//                           same double-click timing as the mouse: two presses on a row within
//                           400 ms open it; with ctrl or shift held it marks rows instead
//   press-side <row>        a left press on sidebar row <row>
//   header name|size|mtime  a click on that column's header
//   click-at <x> <y>        a left press at a point, hit-tested against the last frame as the
//                           mouse's is (rows, sidebar rows, headers)
//   point-ui <id> [k]       the point at the middle of a Clay element (k: its CLAY_IDI index),
//                           printed as "point: x y" and kept for click-here
//   point-menu <word>       the same for the open menu's entry named <word> in the state's menu
//                           lists (follow, search-in): a new entry doesn't renumber the scripts
//   hover-here              the pointer to the last point-ui (the hover highlights, a drive's
//                           eject button, a button's tooltip); it rests there, hit again after
//                           each frame as the mouse's is (hover_refresh)
//   pointer-off             the pointer out of the window
//   idle                    the window waits for events as it does unscripted, until a tooltip
//                           waiting is due (or the search field's text, F9); a wake-up 3 s on stands in if nothing comes, and the
//                           state's idlewoke says which ended the wait (timer or fallback)
//   click-here              a left press at the last point-ui
//   drag-here <dx>          a left press there, the pointer <dx> px across and the button let go,
//                           in one frame, as the mouse's go (zoom_drag_at: the zoom slider's knob)
//   middle-here             a middle press there (a folder into a background tab, a tab closed)
//   middle <row>            a middle press on list row <row>
//   right-here              a right press there (F8: an entry's menu, or the folder shown's on empty space)
//   right-at <x> <y>        a right press at a point
//   back, forward           the mouse's back and forward buttons
//   raw-button <n>          GLFW's button n (1-8) pressed and released through input.c's callback
//   raw-key <key> [mods]    GLFW key code <key> pressed through input.c's key callback, with GLFW's
//                           mod bits (1 Shift, 2 Ctrl, 4 Alt): what ui_key_of makes of it
//   user-keys               from here on the compositor's keyboard reaches the window too, keys
//                           and characters through GLFW's callbacks as a user's: `sh wtype …` in
//                           the test sway (tools/gui/sway.sh; weston has no virtual keyboard)
//   clip-text <text>        the rest of the line onto GLFW's clipboard as another program's copy
//                           would put it, \n a line break (weston has no data-control)
//   shot <file.png>         the next drawn frame; a relative path goes under OFX_GUI_SHOTS
//   sh <command...>         runs the rest of the line with /bin/sh and waits for it (5 s at most),
//                           $OFX_DIR set to the folder shown (the home trash in the trash
//                           view), $OFX_ROOT to the run's scratch tree.
//                           Refused unless the folder is under OFX_GUI_ROOT and nothing failed yet.
//   state                   prints one line of key=value words about the window
//   expect <word>...        each word must be in that line (whole words: `sel=a.txt`); the
//                           script ends with "script: N of M expectations failed" when any did
//   wait-for <ms> <word>... frames until every word is in the state line (a reload comes in its
//                           own time); an expectation, failed when <ms> pass
//   quit

#include <spawn.h>
#include <sys/wait.h>

static struct {
    Str   text;
    isize at;
    int   line;
    int   wait;          // frames left of a `wait`
    i64   until;         // a `sleep`'s or `wait-for`'s end, in ms
    char  waiting_for[512];
    int   settle_w, settle_h, settle_n;
    bool  settling;
    char  shot[2100];
    int   expects, failed;
    float px, py;        // the last point-ui
    bool  pointing;      // the pointer rests there (hover-here), until pointer-off
    Str   typing;        // what is left of a `type` longer than a frame's events
    bool  user_keys;     // the keyboard's keys and characters reach the window (`user-keys`)
} script;

static u8 script_mem[KiB(256)];

// `idle`'s stand-in wake-up: 3 s on, should the timer never come. Each idle has its own number,
// so a stand-in left over from an earlier one (two idles within 3 s) doesn't end this one, and it
// wakes the window through wake_main, which is quiet once the window is closing (2026-10-28).
static atomic_bool idle_fallback;
static atomic_int  idle_gen;
static const char *idle_woke = "-";

static void *idle_fallback_main(void *arg)
{
    int gen = (int)(intptr_t)arg;
    nanosleep(&(struct timespec){ 3, 0 }, NULL);
    if (atomic_load(&idle_gen) != gen) return NULL;
    atomic_store(&idle_fallback, true);
    wake_main();
    return NULL;
}

// The window asleep until the tooltip waiting is due, as unscripted: only the timer thread's
// wake-up (or the fallback) ends it. Frame-done events from the last swap wake it once or twice
// on the way; it sleeps again after each.
static void script_idle(void)
{
    i64 due = wake_due();
    atomic_store(&idle_fallback, false);
    int gen = atomic_fetch_add(&idle_gen, 1) + 1;
    pthread_t th;
    pthread_create(&th, NULL, idle_fallback_main, (void *)(intptr_t)gen);
    pthread_detach(th);
    EnableEventWaiting();
    while (due && now_ms() < due && !atomic_load(&idle_fallback)) PollInputEvents();
    DisableEventWaiting();
    idle_woke = !due ? "nothing" : atomic_load(&idle_fallback) ? "fallback" : "timer";
    atomic_fetch_add(&idle_gen, 1);   // (this idle's stand-in, still asleep, now does nothing)
}

static bool script_open(const char *path)
{
    static Arena a;
    arena_init(&a, "script", script_mem, sizeof script_mem);
    script.text = read_file(&a, path);
    if (!script.text.p || script.text.len == 0) {
        fprintf(stderr, "ofx: script %s: cannot read it\n", path);
        return false;
    }
    return true;
}

// A word of the state line: s, "-" when it is empty, spaces as '_' (the line is split at spaces;
// F9's review, 2026-10-29: this was written out at each word). Into out; returns out.
static char *state_word(char *out, size_t cap, Str s)
{
    snprintf(out, cap, "%.*s", s.len ? (int)s.len : 1, s.len ? s.p : "-");
    for (char *c = out; *c; c++)
        if (*c == ' ') *c = '_';
    return out;
}

static void script_state(char *out, size_t cap)
{
    const char *icon = "-";
    Str sel = S("-");
    int icontex = 0;   // the width of the cursor's icon texture as loaded (F6.2: the view's size)
    if (app.have_list && app.ui.list.n > 0) {
        sel = app.list.e[app.ui.list.sel].name;
        IconSlot *s = entry_icon(app.ui.list.sel);
        if (s && s->file[0]) icon = s->file;
        if (s) icontex = s->tex.width;
    }
    Theme *t = &app.theme;
    // The edit line's text, its spaces as '_' (the state line is words).
    static const char *const JOB_PHASES[] = { "scan", "run", "ask", "done" };
    static const char *const UNDO_KINDS[] = { "-", "rename", "mkdir", "trash", "copy", "move", "mkfile", "chmod", "link" };
    _Static_assert(sizeof UNDO_KINDS / sizeof UNDO_KINDS[0] == UNDO_LINK + 1, "a name for each undo");
    char edit[NAME_MAX + 1];
    state_word(edit, sizeof edit, (Str){ app.ui.edit.b, app.ui.edit.len });
    // The edit line's selection and caret (bytes), the text it last copied, the path buttons'
    // keyboard mark (- when they don't have it).
    int slo, shi;
    ui_edit_sel(&app.ui, &slo, &shi);
    char copied[NAME_MAX + 1], keyed[16] = "-";
    state_word(copied, sizeof copied, str_from_c(app.text_copied));
    if (app.ui.focus == UI_FOCUS_CRUMBS) snprintf(keyed, sizeof keyed, "%d", app.ui.crumb_at);
    // The jobs (F5.3): the oldest running one for job= and done=; the rows' labels, '|' between,
    // spaces as '_'; the marked row; the first row's line (points reached) and whether it has a
    // speed.
    JobRec *oldest = job_oldest();
    char joblabels[1024] = "", jobkey[16] = "-";
    for (int i = 0; i < jobs.nrun; i++) {
        size_t k = strlen(joblabels);
        snprintf(joblabels + k, sizeof joblabels - k, "%s%s", i ? "|" : "", job_recs[jobs.run[i]].label);
    }
    for (char *c = joblabels; *c; c++)
        if (*c == ' ') *c = '_';
    if (!joblabels[0]) snprintf(joblabels, sizeof joblabels, "-");
    if (app.ui.focus == UI_FOCUS_JOBS) snprintf(jobkey, sizeof jobkey, "%d", app.ui.job_at);
    char clashjob[256] = "-";   // the job the clash panel names
    if (jobs.asking >= 0) state_word(clashjob, sizeof clashjob, str_from_c(job_recs[jobs.asking].label));
    static const char *const MODALS[] = { "-", "delete", "clash", "cancel", "quit", "full", "empty", "props", "open-with", "bookmarks", "shortcuts", "about", "menubar", "restore" };
    _Static_assert(sizeof MODALS / sizeof MODALS[0] == MODAL_RESTORE + 1, "a name for each panel");
    // The path buttons' labels, '|' between; the cursor's folder count (- unknown, ? unreadable).
    char crumbs[2048] = "", cnt[32] = "-";
    for (int i = 0; i < app.crumbs.n; i++) {
        size_t k = strlen(crumbs);
        snprintf(crumbs + k, sizeof crumbs - k, "%s%.*s", i ? "|" : "", STR_ARG(ui_crumb_label(&app.crumbs, i)));
    }
    // The tabs' folder names in order, '|' between (the current one from app.cwd).
    char tabdirs[1024] = "";
    for (int i = 0; i < tabbar.n; i++) {
        const char *d = i == tabbar.cur ? app.cwd : tabs[tabbar.slot[i]].dir;
        size_t k = strlen(tabdirs);
        snprintf(tabdirs + k, sizeof tabdirs - k, "%s%.*s", i ? "|" : "", STR_ARG(strcmp(d, "/") == 0 ? S("/") : path_base(str_from_c(d))));
    }
    // The sidebar's rows (F6): labels, '|' between, a heading with a '^' first, spaces as '_'; how
    // many drives have their space read.
    char sides[2048] = "";
    int spaced = 0;
    for (int i = 0; i < app.places.n; i++) {
        const Place *p = &app.places.p[i];
        size_t k = strlen(sides);
        // (^ a heading; ~ a drive not mounted yet, 2026-10-30)
        snprintf(sides + k, sizeof sides - k, "%s%s%s", i ? "|" : "", p->kind == PL_HEAD ? "^" : p->kind == PL_UNMOUNTED ? "~" : "", p->label);
        spaced += place_is_drive(p->kind) && p->size > 0;
    }
    for (char *c = sides; *c; c++)
        if (*c == ' ') *c = '_';
    // The tree (F6): its rows, the cursor's row name:open and its row on screen, whether it is the
    // folder shown's row. The status line as it reads without a message, spaces as '_'.
    static const char *const SIDE_MODES_W[] = { "places", "tree", "hidden" };
    char treecur[NAME_MAX + 32] = "-";
    int treehere = -1;
    if (app.ui.side_mode == SIDE_TREE && app.ui.side.sel < tree.n) {
        const TreeRow *r = &tree.row[app.ui.side.sel];
        snprintf(treecur, sizeof treecur, "%.*s:%d", STR_ARG(r->name), r->open);
        treehere = tree_find(&tree, str_from_c(app.cwd)) == (int)app.ui.side.sel;
    }
    // The sidebar row under the pointer (F6.1): its number as drawn ("used:" or "free:"), and
    // whether its eject button shows.
    // The number drawn there, said as which it is: worked out here apart from the layout's.
    char hovernum[64] = "-";
    int ejectbtn = 0;
    isize hi = app.hov.side >= 0 ? app.ui.side.top + app.hov.side : -1;
    if (app.ui.side_mode == SIDE_PLACES && hi >= 0 && hi < app.places.n && app.hover_num[0]) {
        const Place *hp = &app.places.p[hi];
        char used[32], avail[32];
        ui_size(used, sizeof used, hp->size - hp->avail), ui_size(avail, sizeof avail, hp->avail);
        snprintf(hovernum, sizeof hovernum, "%s", strcmp(app.hover_num, used) == 0 ? "used" : strcmp(app.hover_num, avail) == 0 ? "free" : "?");
    }
    if (app.ui.side_mode == SIDE_PLACES && hi >= 0 && hi < app.places.n) ejectbtn = place_ejectable(&app.places.p[hi]);
    // The tooltip shown (Michael, 2026-10-24), spaces as '_'.
    char tipword[NAME_MAX + 64] = "-";
    // The tooltip shown lies inside the window (F6.3: the zoom plus's, at the right edge); -1 none.
    Clay_ElementData tipbox = Clay_GetElementData(CLAY_ID("tooltip"));
    int tipin = !app.tip.shown || !tipbox.found ? -1
              : tipbox.boundingBox.x >= 0 && tipbox.boundingBox.y >= 0 && tipbox.boundingBox.x + tipbox.boundingBox.width <= (float)GetScreenWidth() &&
                    tipbox.boundingBox.y + tipbox.boundingBox.height <= (float)GetScreenHeight();
    Clay_ElementId tipid;
    if (app.tip.shown && tip_of(app.tip.target, tipword, sizeof tipword, &tipid))
        for (char *c = tipword; *c; c++)
            if (*c == ' ') *c = '_';
    char info[NAME_MAX + 64];
    status_info(info, sizeof info);
    for (char *c = info; *c; c++)
        if (*c == ' ') *c = '_';
    // The search view (F9): its field's text and its status line, spaces as '_'; the cursor's folder
    // below the one searched ("-": that one, or not a search).
    char searchtext[UI_FIND_MAX + 1], searchinfo[512], seldir[PATH_MAX];
    state_word(searchtext, sizeof searchtext, (Str){ app.ui.search.b, app.ui.search.len });
    char info_s[512] = "";
    if (app.ui.view == UI_VIEW_SEARCH) search_status(info_s, sizeof info_s);
    state_word(searchinfo, sizeof searchinfo, str_from_c(info_s));
    const Entry *se = app.have_list && app.ui.list.n > 0 ? &app.list.e[app.ui.list.sel] : NULL;
    snprintf(seldir, sizeof seldir, "%.*s", se && se->dir.len ? (int)se->dir.len : 1, se && se->dir.len ? se->dir.p : "-");   // (its spaces kept, as before)
    // Inside files (F9 step 3): the "Containing" field's text, and where the cursor's result
    // matched: line:column+more lines, the match, and whether its row drew it.
    char insidetext[UI_FIND_MAX + 1], hit[64] = "-", hitmatch[SEARCH_SNIP + 1] = "-";
    state_word(insidetext, sizeof insidetext, (Str){ app.ui.inside.b, app.ui.inside.len });
    if (se && se->hit) {
        snprintf(hit, sizeof hit, "%lld:%lld+%lld", (long long)se->hit->line, (long long)se->hit->col, (long long)se->hit->more);
        state_word(hitmatch, sizeof hitmatch, (Str){ se->hit->text.p + se->hit->at, se->hit->len });
    }
    int hitdrawn = se ? Clay_GetElementData(CLAY_IDI("hit", (int)(app.ui.list.sel - app.ui.list.top))).found : 0;
    // The cursor's preview, as its texture's size (F6.4).
    char thumbsel[32] = "-";
    char tpath[PATH_MAX];
    if (app.have_list && app.ui.list.n > 0 && entry_path(app.ui.list.sel, tpath, sizeof tpath)) {
        Texture2D *tt = thumb_peek(tpath, grid_px() * app.icon_scale);
        if (tt) snprintf(thumbsel, sizeof thumbsel, "%dx%d", tt->width, tt->height);
    }
    // And its box as the last frame drew it, in logical pixels.
    char thumbbox[32] = "-";
    Clay_ElementData tb = Clay_GetElementData(CLAY_IDI("thumb", (int)(app.ui.list.sel - app.ui.list.top)));
    if (tb.found) snprintf(thumbbox, sizeof thumbbox, "%.0fx%.0f", tb.boundingBox.width, tb.boundingBox.height);
    // The menu bar (F7.1): the open menu's marked entry, the File menu's entries that can't be
    // picked now (whether or not it is open), and the windows in the registry.
    char menuat[32] = "-", menuoff[256] = "";
    if (app.ui.menu_on && app.ui.menu_at >= 0) snprintf(menuat, sizeof menuat, "%s", UI_MENU_ITEM[UI_MENU[app.ui.menu].items[app.ui.menu_at]].word);
    for (int i = 0; i < UI_MENU[0].n; i++) {
        UiMenuItem it = UI_MENU[0].items[i];
        size_t k = strlen(menuoff);
        if (it != UM_SEP && !ui_menu_enabled(&app.ui, it)) snprintf(menuoff + k, sizeof menuoff - k, "%s%s", k ? "," : "", UI_MENU_ITEM[it].word);
    }
    if (!menuoff[0]) snprintf(menuoff, sizeof menuoff, "-");
    // The open menu's entries (F7.4): those that can't be picked, those the last frame didn't draw,
    // those marked.
    char menugrey[512] = "", menuhid[512] = "", menumarked[256] = "";
    for (int i = 0; app.ui.menu_on && i < UI_MENU[app.ui.menu].n; i++) {
        UiMenuItem it = UI_MENU[app.ui.menu].items[i];
        if (it == UM_SEP) continue;
        char *dst[3] = { menugrey, menuhid, menumarked };
        size_t room[3] = { sizeof menugrey, sizeof menuhid, sizeof menumarked };
        bool in[3] = { !ui_menu_enabled(&app.ui, it), !Clay_GetElementData(CLAY_IDI("menu-item", i)).found, ui_menu_mark(&app.ui, it) == 1 };
        for (int j = 0; j < 3; j++) {
            size_t k = strlen(dst[j]);
            if (in[j]) snprintf(dst[j] + k, room[j] - k, "%s%s", k ? "," : "", UI_MENU_ITEM[it].word);
        }
    }
    if (!menugrey[0]) snprintf(menugrey, sizeof menugrey, "-");
    if (!menuhid[0]) snprintf(menuhid, sizeof menuhid, "-");
    if (!menumarked[0]) snprintf(menumarked, sizeof menumarked, "-");
    // ofx.conf as last read (F10): previews:max:date:skips (+ between), the first line it skipped,
    // and the cursor's entry's date as the list shows it.
    static const char *const DATES[] = { "short", "long", "iso", "pattern" };
    char confw[CONF_SKIP_MAX * CONF_NAME_MAX + 96], seldate[96], datew[96];
    int cw = snprintf(confw, sizeof confw, "%s:%lld:%s:", app.conf.previews ? "on" : "off", (long long)app.conf.preview_max, DATES[app.conf.date]);
    for (int k = 0; k < app.conf.skip.n && cw > 0 && (size_t)cw < sizeof confw; k++)
        cw += snprintf(confw + cw, sizeof confw - (size_t)cw, "%s%s", k ? "+" : "", app.conf.skip.name[k]);
    datew[0] = 0;
    if (app.have_list && app.ui.list.sel < app.list.n) entry_when(&app.list.e[app.ui.list.sel], (i64)time(NULL), datew, sizeof datew);
    state_word(seldate, sizeof seldate, str_from_c(datew));
    // The Trash place's icon by its name (2026-10-30: full or empty).
    const char *trashicon = "-";
    for (int i = 0; i < app.places.n; i++) {
        IconSlot *ic = app.places.p[i].kind == PL_TRASH ? place_icon(i) : NULL;
        if (ic) trashicon = ic->key.p;
    }
    // The open menu's box as the last frame drew it (F8: a right-click menu inside the window).
    char menubox[64] = "-";
    Clay_ElementData mb = Clay_GetElementData(CLAY_ID("menu"));
    if (app.ui.menu_on && mb.found)
        snprintf(menubox, sizeof menubox, "%.0f,%.0f,%.0f,%.0f", mb.boundingBox.x, mb.boundingBox.y, mb.boundingBox.width, mb.boundingBox.height);
    int menuin = app.ui.menu_on && mb.found && mb.boundingBox.x >= 0 && mb.boundingBox.y >= 0 &&
                 mb.boundingBox.x + mb.boundingBox.width <= (float)GetScreenWidth() && mb.boundingBox.y + mb.boundingBox.height <= (float)GetScreenHeight();
    // And whether it leaves the cursor's row in sight (a right-click menu by the keys opens under it).
    Clay_ElementData cr = Clay_GetElementData(CLAY_IDI("row", (int)(app.ui.list.sel - app.ui.list.top)));
    int menuclear = app.ui.menu_on && mb.found && cr.found &&
                    (mb.boundingBox.y >= cr.boundingBox.y + cr.boundingBox.height || mb.boundingBox.y + mb.boundingBox.height <= cr.boundingBox.y);
    int menuseps = 0;   // the open menu's lines drawn (one before no bookmarks isn't, F7.5)
    for (int i = 0; app.ui.menu_on && i < UI_MENU[app.ui.menu].n; i++)
        menuseps += UI_MENU[app.ui.menu].items[i] == UM_SEP && Clay_GetElementData(CLAY_IDI("menu-sep", i)).found;
    // The Bookmarks menu's (F7.5): their names as it lists them, '|' between, spaces as '_'.
    char bmlist[1024] = "";
    for (int k = 0; k < UI_MENU_BOOKMARKS; k++) {
        const Bookmark *b = bm_menu(k);
        if (!b) break;
        size_t at = strlen(bmlist);
        snprintf(bmlist + at, sizeof bmlist - at, "%s%.*s", at ? "|" : "", STR_ARG(bm_label(b)));
    }
    for (char *c = bmlist; *c; c++) *c = *c == ' ' ? '_' : *c;
    if (!bmlist[0]) snprintf(bmlist, sizeof bmlist, "-");
    // Keyboard Shortcuts' first row shown: its action (spaces '_'; a heading "#" and its name).
    char keysfirst[128] = "-";
    for (isize v = app.ui.keys_list.top; app.ui.keys && v < app.ui.keys_list.n; v++) {
        int line = app.ui.keys_line[v];
        snprintf(keysfirst, sizeof keysfirst, "%s%s", line < 0 ? "#" : "", line < 0 ? UI_KEY_SECTION[-1 - line] : UI_KEYS[line].action);
        break;
    }
    for (char *c = keysfirst; *c; c++) *c = *c == ' ' ? '_' : *c;
    char bmptext[300];   // Edit Bookmarks' rename field (spaces '_'; "-" empty)
    state_word(bmptext, sizeof bmptext, (Str){ app.ui.bmp_name.b, app.ui.bmp_name.len });
    // The bottom bar as the last frame drew it lies inside the window (F7.1: under the menu bar's
    // 26 px the rest is as tall as before).
    Clay_ElementData fd = Clay_GetElementData(CLAY_ID("footer"));
    int footerin = fd.found && fd.boundingBox.y + fd.boundingBox.height <= (float)GetScreenHeight() + 0.5f;
    char propname[NAME_MAX + 1];   // the Properties panel's name, spaces as '_'
    state_word(propname, sizeof propname, str_from_c(props.name));
    char proptypename[128];   // its type in words, spaces as '_'
    state_word(proptypename, sizeof proptypename, str_from_c(props.type_name));
    // Open With (F7.3): its field's text (spaces '_'), the app under the cursor (its desktop id),
    // and the toggles as letters (c, w, r; "-" none), the panel's and find's.
    char owtext[UI_FIND_MAX + 1], owapp[NAME_MAX + 1], owopts[4], findopts[4], insideopts[4];
    state_word(owtext, sizeof owtext, (Str){ app.ui.ow_filter.b, app.ui.ow_filter.len });
    isize owsel = app.ui.ow && app.ui.ow_list.n > 0 ? app.ui.ow_idx[app.ui.ow_list.sel] : -1;
    snprintf(owapp, sizeof owapp, "%.*s", owsel >= 0 ? (int)ow.apps.a[owsel].id.len : 1, owsel >= 0 ? ow.apps.a[owsel].id.p : "-");
    // The icon drawn for that app: its file's name ("-" none).
    const char *owicon = "-";
    if (owsel >= 0) {
        IconSlot *ic = ow_icon(owsel);
        if (ic && ic->file[0]) owicon = strrchr(ic->file, '/') ? strrchr(ic->file, '/') + 1 : ic->file;
    }
    int owtex = 0;   // the panel's icon textures held (none once it closed)
    for (isize i = 0; i < UI_APPS_MAX; i++) owtex += ow.icon[i].tex.id != 0;
    UiMatchOpts mo[3] = { app.ui.ow_opts, app.ui.find_opts, app.ui.inside_opts };
    char *mout[3] = { owopts, findopts, insideopts };
    for (int k = 0; k < 3; k++) {
        int n = 0;
        if (mo[k].cased) mout[k][n++] = 'c';
        if (mo[k].word) mout[k][n++] = 'w';
        if (mo[k].regex) mout[k][n++] = 'r';
        if (!n) mout[k][n++] = '-';
        mout[k][n] = 0;
    }
    // The find bar's count, as it shows (spaces '_'; "-" none).
    isize findat, findn = find_count(&findat);
    char findtext[48] = "-";
    if (findn > 0) find_label(findn, findat, findtext, sizeof findtext);
    for (char *c = findtext; *c; c++) *c = *c == ' ' ? '_' : *c;
    char propwhere[PATH_MAX], propdrive[256], proplink[PATH_MAX];
    snprintf(propwhere, sizeof propwhere, "%s", props.where[0] ? props.where : "-");
    snprintf(propdrive, sizeof propdrive, "%s", props.drive[0] ? props.drive : "-");
    snprintf(proplink, sizeof proplink, "%s", props.link[0] ? props.link : "-");
    for (char *c = propwhere; *c; c++) *c = *c == ' ' ? '_' : *c;
    for (char *c = propdrive; *c; c++) *c = *c == ' ' ? '_' : *c;
    for (char *c = proplink; *c; c++) *c = *c == ' ' ? '_' : *c;
    int windows = 0;
    DIR *wd = app.windows[0] ? opendir(app.windows) : NULL;
    for (struct dirent *we; wd && (we = readdir(wd));) windows += we->d_name[0] != '.';
    if (wd) closedir(wd);
    // The cursor's Size cell as drawn, its spaces as '_'.
    char sizecell[64] = "-";
    if (app.have_list && app.ui.list.n > 0) {
        size_cell(app.ui.list.sel, sizecell, sizeof sizecell, false);
        if (!sizecell[0]) snprintf(sizecell, sizeof sizecell, "-");
        for (char *c = sizecell; *c; c++)
            if (*c == ' ') *c = '_';
    }
    if (app.have_list && app.ui.list.n > 0 && kind_is_dir(app.list.e[app.ui.list.sel].kind)) {
        isize c = app.count[app.ui.list.sel];
        if (c >= 0) snprintf(cnt, sizeof cnt, "%td", c);
        else if (c == -1) snprintf(cnt, sizeof cnt, "?");
    }
    snprintf(out, cap,
             "dir=%s n=%td hidden=%td sel=%.*s idx=%td top=%td rows=%d hover=%d accent=#%02x%02x%02x bg=#%02x%02x%02x "
             "icons=%s scale=%d w=%d h=%d reloads=%d themes=%d batches=%d frame_ms_max=%.1f icon=%s "
             "shown=%d sort=%s rev=%d finding=%d findkeys=%d miss=%d find=%.*s last=%.*s side=%td sidefocus=%d place=%s here=%s "
             "back=%d fwd=%d marked=%td editing=%d edit=%s confirming=%d asking=%d job=%s done=%lld/%lld undo=%s clip=%td cut=%d "
             "sysclip=%d modal=%s askall=%d tabs=%d tab=%d tabdirs=%s sizecell=%s crumbs=%s crumbhome=%d crumb=%d crumbshown=%d-%d arrows=%d locating=%d cnt=%s "
             "esel=%d-%d caret=%d copied=%s crumbkey=%s jobs=%d waiting=%d jobrows=%s jobkey=%s jobline=%d jobspeed=%d clashjob=%s "
             "sides=%s spaced=%d sidemode=%s treen=%d treecur=%s treerow=%td treehere=%d trash=%d sidehover=%d hovernum=%s ejectbtn=%d cwdavail=%d info=%s "
             "view=%s cols=%d cells=%d cellfirst=%td celllast=%td fulllines=%d celllines=%d cellcut=%d iconpx=%d icontex=%d iconresets=%d crumbfocus=%d tooltip=%s idlewoke=%s "
             "zoom=%d cellw=%d cellh=%d zoomshown=%d tipin=%d slots=%d "
             "previews=%d thumbsel=%s thumbbox=%s thumbbusy=%d thumbmade=%d thumbread=%d thumbfailed=%d thumbknown=%d thumbnone=%d thumbchanged=%d "
             "thumbheld=%td thumbdropped=%d thumbresets=%d thumblocal=%d reloadasked=%d menuseps=%d bmlist=%s bmn=%d bmhere=%d canbm=%d bmmore=%d bmp=%d bmpn=%td bmpsel=%td bmpnaming=%d bmptext=%s keys=%d keysn=%td keystop=%td keysrows=%d keysfirst=%s menu=%s menuat=%s menuoff=%s menugrey=%s menuhid=%s menumarked=%s menubox=%s menuin=%d menuclear=%d permscols=%d permsshown=%d permsnote=%d groupcol=%d groupdrawn=%d colors=%d barhidden=%d barshown=%d menuhover=%d canwrite=%d windows=%d siderows=%d footerin=%d "
             "props=%d propat=%d propcan=%d propn=%td propmode=%04o propfiles=%lld propdirs=%lld propbytes=%lld propdone=%d propcut=%d proplinks=%lld,%lld,%lld,%d propnlink=%llu proptype=%s proptypename=%s propbyname=%d propname=%s "
             "propwhere=%s propdrive=%s proplink=%s propcreated=%d propidx=%td "
             "ow=%d owfresh=%d owlist=%d owtext=%s own=%td owsel=%td owapp=%s owicon=%s owtex=%d owshown=%td owtype=%s owfiles=%td owrows=%d owtop=%td owopts=%s owbad=%d owdef=%d owdefcan=%d findopts=%s findbad=%d findselect=%d findn=%td findat=%td findtext=%s "
             "search=%d searchkeys=%d searchtext=%s searchdone=%d searchfull=%d searchdue=%d searchbad=%d searchbar=%d searchsel=%d-%d searchcaret=%d findsel=%d-%d findcaret=%d seldir=%s searchinfo=%s inside=%d insidekeys=%d insidetext=%s insideopts=%s insidebad=%d insiderow=%d hit=%s hitmatch=%s hitdrawn=%d datecol=%s datecolw=%.0f owreads=%d conf=%s confbad=%d seldate=%s trashfull=%d trashicon=%s spawned=%d closingall=%d closedtabs=%d statusbad=%d status=%s",
             app.cwd, app.list.n, app.list.hidden, STR_ARG(sel), app.ui.list.sel, app.ui.list.top, app.ui.list.rows, app.hov.row,
             t->accent.r, t->accent.g, t->accent.b, t->bg.r, t->bg.g, t->bg.b, t->icons[0] ? t->icons : "-",
             app.icon_scale, GetScreenWidth(), GetScreenHeight(), app.reloads, app.themes, atomic_load(&watch.batches),
             (double)frame_us_max / 1000.0, icon, app.show_hidden,
             app.ui.sort.key == SORT_NAME ? "name" : app.ui.sort.key == SORT_SIZE ? "size" : "mtime", app.ui.sort.reverse,
             app.ui.finding, app.ui.focus == UI_FOCUS_FIND, app.ui.find_miss, app.ui.find.len ? app.ui.find.len : 1, app.ui.find.len ? app.ui.find.b : "-",
             app.ui.last_len ? app.ui.last_len : 1, app.ui.last_len ? app.ui.last : "-", app.ui.side.sel, app.ui.focus == UI_FOCUS_SIDE,
             app.ui.side_mode == SIDE_PLACES && app.ui.side.sel < app.places.n ? app.places.p[app.ui.side.sel].label : "-", app.place_cur >= 0 ? app.places.p[app.place_cur].label : "-",
             hist.nback, hist.nfwd, app.ui.marked, app.ui.focus == UI_FOCUS_EDIT, edit, app.ui.confirming, app.ui.asking,
             !oldest ? "-" : JOB_PHASES[atomic_load(&oldest->j.phase)], oldest ? atomic_load(&oldest->j.files_done) : 0LL,
             oldest ? atomic_load(&oldest->j.files_total) : 0LL,
             UNDO_KINDS[undo.kind], own.n, own.cut, clip.ok, MODALS[modal_kind()], app.ui.ask_all, tabbar.n, tabbar.cur, tabdirs, sizecell, crumbs, app.crumbs.seg[0].home ? 0 : -1, app.crumbs.cur, app.crumb_lo, app.crumb_hi, app.crumb_arrows,
             app.ui.focus == UI_FOCUS_EDIT && app.ui.edit_path, cnt, slo, shi, app.ui.edit.caret, copied, keyed,
             jobs.nrun, jobs.nwait, joblabels, jobkey, oldest ? oldest->line.reached : 0, oldest && oldest->line.now > 0, clashjob,
             sides[0] ? sides : "-", spaced, SIDE_MODES_W[app.ui.side_mode], app.ui.side_mode == SIDE_TREE ? tree.n : 0, treecur,
             app.ui.side.sel - app.ui.side.top, treehere, app.ui.view == UI_VIEW_TRASH, app.hov.side, hovernum, ejectbtn, app.cwd_avail >= 0, info,
             app.ui.grid ? "icons" : "list", (int)ui_cols(&app.ui.list), app.grid_cells, app.grid_first, app.grid_last, app.grid_full_lines, app.grid_hover_lines, app.grid_hover_cut, entry_px(), icontex, app.icon_full_resets, app.ui.focus == UI_FOCUS_CRUMBS, tipword, idle_woke,
             ui_zoom_px(&app.ui), cell_w(), cell_h(), Clay_GetElementData(CLAY_ID("zoom-track")).found, tipin, app.nslots,
             thumbs.drawn_last, thumbsel, thumbbox, thumbs_busy(), thumbs.got[TG_MADE], thumbs.got[TG_CACHED], thumbs.got[TG_FAILED],
             thumbs.got[TG_FAIL_KNOWN], thumbs.got[TG_NONE], thumbs.got[TG_CHANGED], thumbs.held, thumbs.evicted, thumbs.resets, app.thumb_local,
             app.reloads_asked, menuseps, bmlist, app.ui.bm_n, app.ui.bm_here, app.ui.can_bookmark, app.ui.bm_more, app.ui.bmp, app.ui.bmp_list.n, app.ui.bmp_list.sel, app.ui.bmp_naming, bmptext, app.ui.keys, app.ui.keys_list.n, app.ui.keys_list.top, app.ui.keys_list.rows, keysfirst, app.ui.menu_on ? UI_MENU[app.ui.menu].title : "-", menuat, menuoff, menugrey, menuhid, menumarked, menubox, menuin, menuclear, app.ui.perms_cols, perms_shown((float)GetScreenWidth()), (int)(strstr(info, "Permissions_hidden") != NULL), app.ui.group_col,
             (int)Clay_GetElementData(CLAY_ID("col-group")).found + (int)Clay_GetElementData(CLAY_IDI("group", 0)).found, app.ui.colors, app.ui.bar_hidden, bar_shown(), app.hov.menu_title, app.ui.can_write, windows,
             app.ui.side.rows, footerin,
             app.ui.props, app.ui.prop_at, app.ui.prop_can, props.n, props.have ? (unsigned)(props.st.st_mode & 07777) : 0u,
             (long long)props.sum.files, (long long)props.sum.dirs, (long long)props.sum.bytes, props.done, props.sum.cut || props.cut,
             (long long)props.sum.linked_names, (long long)props.sum.linked_files, (long long)props.sum.once_bytes, props.sum.once_over,
             props.have ? (unsigned long long)props.st.st_nlink : 0ull,
             props.type[0] ? props.type : "-", proptypename, props.type_by_name, propname,
             propwhere, propdrive, proplink, props.btime >= 0, props.idx,
             app.ui.ow, app.ui.ow_fresh, app.ui.ow_in_list, owtext, app.ui.ow_list.n, app.ui.ow_list.sel, owapp, owicon, owtex, ow.shown, ow.type[0] ? ow.type : "-", ow.n,
             app.ui.ow_list.rows, app.ui.ow_list.top, owopts, app.ui.ow_m.bad, app.ui.ow_def, app.ui.ow_def_can, findopts, app.ui.find_m.bad, app.ui.find_select, findn, findat, findtext,
             app.ui.view == UI_VIEW_SEARCH, app.ui.focus == UI_FOCUS_SEARCH, searchtext, app.search_done, app.search_s.full, app.search_due != 0, app.search_m.bad,
             (int)Clay_GetElementData(CLAY_ID("search-bar")).found,
             app.ui.search.anchor < app.ui.search.caret ? app.ui.search.anchor : app.ui.search.caret,
             app.ui.search.anchor < app.ui.search.caret ? app.ui.search.caret : app.ui.search.anchor, app.ui.search.caret,
             app.ui.find.anchor < app.ui.find.caret ? app.ui.find.anchor : app.ui.find.caret,
             app.ui.find.anchor < app.ui.find.caret ? app.ui.find.caret : app.ui.find.anchor, app.ui.find.caret, seldir, searchinfo,
             app.ui.search_inside, app.ui.focus == UI_FOCUS_INSIDE, insidetext, insideopts, app.search_im.bad,
             (int)Clay_GetElementData(CLAY_ID("inside-field")).found, hit, hitmatch, hitdrawn, VIEWS[app.ui.view].date_col, date_w(), ow_idb_reads, confw, app.conf.bad, seldate, app.trash_full, trashicon, spawn_started, closing_all, closed_tabs.n, app.status[0] && app.status_bad, app.status[0] ? app.status : "-");
    frame_us_max = 0;
}

// Whether every word of `words` is a whole word of line.
static bool script_has_all(const char *line, Str words, const char **missing, char *buf, size_t cap)
{
    Str l = str_from_c(line);
    for (isize at = 0; at < words.len;) {
        while (at < words.len && words.p[at] == ' ') at++;
        isize e = at;
        while (e < words.len && words.p[e] != ' ') e++;
        if (e == at) break;
        Str w = str_cut(words, at, e);
        bool found = false;
        for (isize i = 0; !found && i + w.len <= l.len; i++)
            found = (i == 0 || l.p[i - 1] == ' ') && memcmp(l.p + i, w.p, (size_t)w.len) == 0 &&
                    (i + w.len == l.len || l.p[i + w.len] == ' ');
        if (!found) {
            snprintf(buf, cap, "%.*s", STR_ARG(w));
            *missing = buf;
            return false;
        }
        at = e;
    }
    return true;
}

static UiKey script_key(Str name)
{
    static const struct { const char *name; UiKey k; } KEYS[] = {
        { "up", UK_UP }, { "down", UK_DOWN }, { "pageup", UK_PAGEUP }, { "pagedown", UK_PAGEDOWN },
        { "home", UK_HOME }, { "end", UK_END }, { "enter", UK_ENTER }, { "right", UK_RIGHT },
        { "left", UK_LEFT }, { "backspace", UK_BACKSPACE }, { "esc", UK_ESC }, { "tab", UK_TAB },
        { "alt-left", UK_BACK }, { "alt-right", UK_FORWARD }, { "ctrl-f", UK_FIND }, { "ctrl-h", UK_HIDDEN },
        { "f2", UK_F2 }, { "f9", UK_F9 }, { "delete", UK_DELETE }, { "shift-delete", UK_SHIFT_DELETE }, { "ctrl-a", UK_ALL },
        { "ctrl-c", UK_COPY }, { "ctrl-m", UK_MAKE_LINK }, { "ctrl-p", UK_PERMS_COLS }, { "ctrl-g", UK_GROUP_COL }, { "ctrl-x", UK_CUT }, { "ctrl-v", UK_PASTE }, { "ctrl-z", UK_UNDO },
        { "ctrl-shift-n", UK_NEWFOLDER }, { "ctrl-l", UK_LOCATION }, { "alt-up", UK_PARENT },
        { "ctrl-t", UK_TAB_NEW }, { "ctrl-shift-t", UK_TAB_REOPEN }, { "ctrl-w", UK_TAB_CLOSE }, { "ctrl-tab", UK_TAB_NEXT }, { "ctrl-shift-tab", UK_TAB_PREV },
        { "ctrl-shift-pageup", UK_TAB_LEFT }, { "ctrl-shift-pagedown", UK_TAB_RIGHT },
        { "shift-left", UK_SEL_LEFT }, { "shift-right", UK_SEL_RIGHT }, { "shift-home", UK_SEL_HOME }, { "shift-end", UK_SEL_END },
        { "shift-up", UK_SEL_UP }, { "shift-down", UK_SEL_DOWN },
        { "shift-enter", UK_SHIFT_ENTER }, { "ctrl-k", UK_CRUMBS }, { "ctrl-j", UK_JOBS }, { "ctrl-q", UK_QUIT_ALL }, { "close-window", UK_QUIT },
        { "f10", UK_F10 }, { "ctrl-n", UK_NEW_WINDOW }, { "ctrl-i", UK_PROPERTIES }, { "alt-enter", UK_TERMINAL },
        { "ctrl-1", UK_VIEW_LIST }, { "ctrl-2", UK_VIEW_ICONS }, { "ctrl-left", UK_WORD_LEFT }, { "ctrl-right", UK_WORD_RIGHT },
        { "ctrl-shift-left", UK_SEL_WORD_LEFT }, { "ctrl-shift-right", UK_SEL_WORD_RIGHT }, { "ctrl-backspace", UK_WORD_BACKSPACE }, { "ctrl-delete", UK_WORD_DELETE },
        { "ctrl-equal", UK_ZOOM_IN }, { "ctrl-minus", UK_ZOOM_OUT }, { "ctrl-0", UK_ZOOM_RESET },
        { "ctrl-shift-f", UK_SEARCH }, { "ctrl-enter", UK_CONTAINING }, { "ctrl-shift-enter", UK_OPEN_LINE }, { "ctrl-shift-i", UK_INVERT }, { "ctrl-shift-a", UK_SELECT_MATCH }, { "ctrl-r", UK_RELOAD }, { "alt-home", UK_HOMEDIR }, { "alt-end", UK_TRASH_VIEW }, { "ctrl-d", UK_ADD_BOOKMARK }, { "ctrl-b", UK_EDIT_BOOKMARKS }, { "f1", UK_SHORTCUTS }, { "alt-down", UK_ALT_DOWN },
    };
    for (int i = 0; i < COUNT(KEYS); i++)
        if (str_eq(name, str_from_c(KEYS[i].name))) return KEYS[i].k;
    return UK_NONE;
}

static void script_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void script_fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("script:%d: ", script.line);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    script.failed++;
}

// Whether path is root or under it, both resolved.
static bool script_under(const char *path, const char *root)
{
    char p[PATH_MAX], r[PATH_MAX];
    if (!root || !root[0] || !realpath(path, p) || !realpath(root, r)) return false;
    size_t n = strlen(r);
    return strncmp(p, r, n) == 0 && (p[n] == 0 || p[n] == '/');
}

// An `sh` line runs only while the script is on track: ofx's folder inside the run's scratch tree
// (OFX_GUI_ROOT, set by run-script.sh), and no expectation failed yet. The lines delete and edit
// files relative to $OFX_DIR, so a navigation bug (or a GUI mutant that recreates one) would
// otherwise point them at wherever ofx landed: / once, Michael's home another time.
static void script_sh(Str cmd)
{
    const char *root = getenv("OFX_GUI_ROOT");
    if (script.failed) {
        script_fail("sh skipped after a failure: %.*s", STR_ARG(cmd));
        return;
    }
    // The trash view (F6) has no folder of its own: the home trash's stands in for it; the search
    // view's (F9) is the folder searched.
    char shown[PATH_MAX];
    if (app.ui.view != UI_VIEW_TRASH || !trash_home(shown, sizeof shown)) snprintf(shown, sizeof shown, "%s", here_dir());
    if (!script_under(shown, root)) {
        script_fail("sh refused: %s is not under OFX_GUI_ROOT (%s)", shown, root ? root : "unset");
        return;
    }
    setenv("OFX_DIR", shown, 1);
    setenv("OFX_ROOT", root, 1);
    char *c = arena_cstr(&scratch, cmd);
    // Under `timeout`: the window answers no events while it waits, and a window that stops
    // answering for long pops Hyprland's "terminate?" dialog on Michael's screen.
    char *argv[] = { "timeout", "5", "/bin/sh", "-c", c, NULL };
    // The window's SIGCHLD has SA_NOCLDWAIT (spawn.c): restore it, or waitpid finds no child.
    struct sigaction dfl = { .sa_handler = SIG_DFL }, old;
    sigaction(SIGCHLD, &dfl, &old);
    pid_t pid;
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);
    if (posix_spawnp(&pid, "timeout", NULL, &attr, argv, environ) == 0) {
        int st;
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) script_fail("sh failed: %.*s", STR_ARG(cmd));
    }
    posix_spawnattr_destroy(&attr);
    sigaction(SIGCHLD, &old, NULL);
}

// After a frame is drawn, before the swap: a pending shot.
static void script_after_draw(void)
{
    if (!script.shot[0]) return;
    Image img = LoadImageFromScreen();
    if (!ExportImage(img, script.shot)) script_fail("cannot write %s", script.shot);
    UnloadImage(img);
    script.shot[0] = 0;
}

// The script's last line, once: at its end, or when the window quits under it (the last tab's
// Ctrl+W).
static void script_end(void)
{
    static bool done;
    if (done) return;
    done = true;
    printf("script: %d of %d expectations failed\n", script.failed, script.expects);
    fflush(stdout);
}

// The next frame's input: false at the end of the script. With OFX_GUI_FAIL_FAST (the GUI
// mutants) the first failure ends it: every wait-for after it would only run out its time.
static bool script_input(UiInput *in)
{
    char state[16384], missing_buf[256];
    const char *missing;
    static int fast = -1;
    if (fast < 0) fast = getenv("OFX_GUI_FAIL_FAST") && getenv("OFX_GUI_FAIL_FAST")[0];
    if (fast && script.failed) {
        script_end();
        return false;
    }
    if (script.wait > 0) return script.wait--, true;
    if (script.typing.len) {
        // A long `type` goes on over several frames, UI_EVENTS_MAX characters a frame.
        isize i = 0;
        while (i < script.typing.len && in->nev < UI_EVENTS_MAX) in->ev[in->nev++] = (UiEvent){ UK_NONE, utf8_next(script.typing.p, script.typing.len, &i) };
        script.typing = str_cut(script.typing, i, script.typing.len);
        return true;
    }
    if (script.settling) {
        int w = GetScreenWidth(), h = GetScreenHeight();
        if (w == script.settle_w && h == script.settle_h) script.settle_n++;
        else script.settle_w = w, script.settle_h = h, script.settle_n = 0;
        if (script.settle_n < 20) return true;
        script.settling = false;
    }
    if (script.until) {
        if (script.waiting_for[0]) {
            script_state(state, sizeof state);
            if (script_has_all(state, str_from_c(script.waiting_for), &missing, missing_buf, sizeof missing_buf)) {
                script.until = 0, script.waiting_for[0] = 0;
                return true;
            }
            if (now_ms() < script.until) return true;
            script_fail("wait-for: no %s in: %s", missing, state);
            script.until = 0, script.waiting_for[0] = 0;
            return true;
        }
        if (now_ms() < script.until) return true;
        script.until = 0;
    }
    Str line;
    while (next_line(script.text, &script.at, &line)) {
        script.line++;
        line = str_trim(line);
        if (line.len == 0 || line.p[0] == '#') continue;
        isize sp = str_find(line, ' ');
        Str cmd = sp < 0 ? line : str_cut(line, 0, sp), arg = sp < 0 ? STR_EMPTY : str_trim(str_cut(line, sp + 1, line.len));
        char a[1024];
        snprintf(a, sizeof a, "%.*s", STR_ARG(arg));
        if (str_eq(cmd, S("settle"))) {
            script.settling = true, script.settle_n = 0;
        } else if (str_eq(cmd, S("wait"))) {
            script.wait = atoi(a);
        } else if (str_eq(cmd, S("sleep"))) {
            script.until = now_ms() + atoi(a);
        } else if (str_eq(cmd, S("key"))) {
            UiKey k = script_key(arg);
            if (str_eq(arg, S("space"))) in->ev[in->nev++] = (UiEvent){ UK_NONE, ' ' };   // a typed space
            else if (arg.len == 5 && str_starts_with(arg, S("alt-")) && arg.p[4] >= '1' && arg.p[4] <= '9')
                in->ev[in->nev++] = (UiEvent){ UK_TAB_JUMP, (u32)(arg.p[4] - '0') };
            else if (arg.len == 5 && str_starts_with(arg, S("alt-")) && arg.p[4] >= 'a' && arg.p[4] <= 'z')
                in->ev[in->nev++] = (UiEvent){ UK_MENU, (u32)arg.p[4] };
            else if (!k) script_fail("no key %s", a);
            else in->ev[in->nev++] = (UiEvent){ k, 0 };
        } else if (str_eq(cmd, S("keys"))) {
            for (isize at = 0; at < arg.len && in->nev < UI_EVENTS_MAX;) {
                while (at < arg.len && arg.p[at] == ' ') at++;
                isize e = at;
                while (e < arg.len && arg.p[e] != ' ') e++;
                Str w = str_cut(arg, at, e);
                at = e;
                if (w.len == 0) break;
                if (str_starts_with(w, S("char:")) && w.len == 6) in->ev[in->nev++] = (UiEvent){ UK_NONE, (u32)(unsigned char)w.p[5] };
                else if (script_key(w)) in->ev[in->nev++] = (UiEvent){ script_key(w), 0 };
                else script_fail("keys: no key %.*s", STR_ARG(w));
            }
        } else if (str_eq(cmd, S("char")) || str_eq(cmd, S("type"))) {
            // The raw line for type: its text may start or end with spaces.
            Str text = str_eq(cmd, S("type")) ? str_cut(line, 5, line.len) : arg;
            if (str_eq(cmd, S("char")) && text.len != 1) script_fail("char takes one character");
            isize i = 0;
            while (i < text.len && in->nev < UI_EVENTS_MAX) in->ev[in->nev++] = (UiEvent){ UK_NONE, utf8_next(text.p, text.len, &i) };
            script.typing = str_cut(text, i, text.len);   // the rest on the next frames
        } else if (str_eq(cmd, S("wheel"))) {
            if (strstr(a, "ctrl")) wheel_at(in, (float)GetScreenWidth() - 2, atoi(a), true);
            else wheel_at(in, (float)side_w() + 40, atoi(a), false);   // over the files, as the mouse's (a panel's list takes it, F7.5)
        } else if (str_eq(cmd, S("press"))) {
            int r = atoi(a);
            int mods = strstr(a, "ctrl") ? UI_MOD_CTRL : strstr(a, "shift") ? UI_MOD_SHIFT : 0;
            if (!mods && ui_press(&app.press, r, now_ms()) == 2) in->dclick = r;
            else in->click = r, in->click_mods = mods;
        } else if (str_eq(cmd, S("press-side"))) {
            in->side_click = atoi(a);
        } else if (str_eq(cmd, S("header"))) {
            in->header = str_eq(arg, S("name")) ? SORT_NAME : str_eq(arg, S("size")) ? SORT_SIZE : str_eq(arg, S("mtime")) ? SORT_MTIME : -1;
            if (in->header < 0) script_fail("no header %s", a);
        } else if (str_eq(cmd, S("click-at"))) {
            float x = 0, y = 0;
            if (sscanf(a, "%f %f", &x, &y) != 2) script_fail("click-at <x> <y>");
            else press_at(in, x, y, 0);
        } else if (str_eq(cmd, S("point-ui"))) {
            char id[200];
            int k = -1;
            Clay_ElementData d = { 0 };
            if (sscanf(a, "%199s %d", id, &k) >= 1)
                d = Clay_GetElementData(k >= 0 ? Clay_GetElementIdWithIndex(cz(id), (uint32_t)k) : Clay_GetElementId(cz(id)));
            if (!d.found) script_fail("point-ui: no element %s", a);
            script.px = d.boundingBox.x + d.boundingBox.width / 2, script.py = d.boundingBox.y + d.boundingBox.height / 2;
            printf("point: %.0f %.0f\n", script.px, script.py);
        } else if (str_eq(cmd, S("point-menu"))) {   // as point-ui, the open menu's entry by its word
            int k = -1;
            for (int i = 0; app.ui.menu_on && i < UI_MENU[app.ui.menu].n; i++)
                if (strcmp(UI_MENU_ITEM[UI_MENU[app.ui.menu].items[i]].word, a) == 0) k = i;
            Clay_ElementData d = { 0 };
            if (k >= 0) d = Clay_GetElementData(Clay_GetElementIdWithIndex(cz("menu-item"), (uint32_t)k));
            if (!d.found) script_fail("point-menu: no %s in the menu open", a);
            script.px = d.boundingBox.x + d.boundingBox.width / 2, script.py = d.boundingBox.y + d.boundingBox.height / 2;
            printf("point: %.0f %.0f\n", script.px, script.py);
        } else if (str_eq(cmd, S("hover-here"))) {
            script.pointing = true;
            hover_at(script.px, script.py);
        } else if (str_eq(cmd, S("pointer-off"))) {
            script.pointing = false;
            hover_refresh(false, script.px, script.py);   // GLFW keeps the last position
        } else if (str_eq(cmd, S("user-keys"))) {
            script.user_keys = true;
        } else if (str_eq(cmd, S("idle"))) {
            script_idle();
        } else if (str_eq(cmd, S("click-here"))) {
            press_at(in, script.px, script.py, 0);
        } else if (str_eq(cmd, S("drag-here"))) {
            press_at(in, script.px, script.py, 0);
            zoom_drag_at(in, script.px + (float)atoi(a), true);
            zoom_drag_at(in, script.px + (float)atoi(a), false);
        } else if (str_eq(cmd, S("middle-here"))) {
            middle_at(in, script.px, script.py);
        } else if (str_eq(cmd, S("right-here"))) {
            right_at(in, script.px, script.py);
        } else if (str_eq(cmd, S("right-at"))) {
            float x = 0, y = 0;
            if (sscanf(a, "%f %f", &x, &y) != 2) script_fail("right-at <x> <y>");
            else right_at(in, x, y);
        } else if (str_eq(cmd, S("middle"))) {
            in->mid_row = atoi(a);
        } else if (str_eq(cmd, S("back"))) {
            in->backs = 1;
        } else if (str_eq(cmd, S("forward"))) {
            in->forwards = 1;
        } else if (str_eq(cmd, S("raw-button"))) {
            int b = atoi(a);
            if (b < 1 || b > 8) script_fail("raw-button 1-8");
            else on_button(glfwGetCurrentContext(), b - 1, GLFW_PRESS, 0), on_button(glfwGetCurrentContext(), b - 1, GLFW_RELEASE, 0);
            // The callback counted it; this frame's input reads it as the mouse's would.
            in->backs = mouse_backs, in->forwards = mouse_forwards, mouse_backs = mouse_forwards = 0;
        } else if (str_eq(cmd, S("raw-key"))) {
            int k = 0, mods = 0;
            if (sscanf(a, "%d %d", &k, &mods) < 1) script_fail("raw-key <key> [mods]");
            else on_key(glfwGetCurrentContext(), k, 0, GLFW_PRESS, mods), on_key(glfwGetCurrentContext(), k, 0, GLFW_RELEASE, mods);
            // The callback queued it; this frame's input takes the queue as the user's would.
            for (int i = 0; i < evq.n && in->nev < UI_EVENTS_MAX; i++) in->ev[in->nev++] = evq.ev[i];
            evq.n = 0;
        } else if (str_eq(cmd, S("clip-text"))) {
            char t[PATH_MAX];
            int n = 0;
            for (isize i = 10; i < line.len && n < (int)sizeof t - 1; i++) {
                if (line.p[i] == '\\' && i + 1 < line.len && line.p[i + 1] == 'n') t[n++] = '\n', i++;
                else t[n++] = line.p[i];
            }
            t[n] = 0;
            SetClipboardText(t);
        } else if (str_eq(cmd, S("shot"))) {
            const char *dir = getenv("OFX_GUI_SHOTS");
            if (a[0] != '/' && dir && dir[0]) snprintf(script.shot, sizeof script.shot, "%s/%s", dir, a);
            else snprintf(script.shot, sizeof script.shot, "%s", a);
            script.wait = 1;   // drawn on the next frame
        } else if (str_eq(cmd, S("sh"))) {
            script_sh(arg);
        } else if (str_eq(cmd, S("state"))) {
            script_state(state, sizeof state);
            printf("state: %s\n", state);
        } else if (str_eq(cmd, S("expect"))) {
            script.expects++;
            script_state(state, sizeof state);
            if (!script_has_all(state, arg, &missing, missing_buf, sizeof missing_buf)) script_fail("expect: no %s in: %s", missing, state);
        } else if (str_eq(cmd, S("wait-for"))) {
            isize s2 = str_find(arg, ' ');
            if (s2 < 0) {
                script_fail("wait-for <ms> <word>...");
                continue;
            }
            script.expects++;
            script.until = now_ms() + atoi(a);
            snprintf(script.waiting_for, sizeof script.waiting_for, "%.*s", STR_ARG(str_cut(arg, s2 + 1, arg.len)));
        } else if (str_eq(cmd, S("quit"))) {
            break;
        } else {
            script_fail("unknown command %.*s", STR_ARG(cmd));
        }
        fflush(stdout);
        return true;
    }
    script_end();
    return false;
}
