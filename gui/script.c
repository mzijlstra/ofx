// OFX_GUI_SCRIPT=FILE: the input comes from a script instead of the user, one frame per loop, so
// the window can be tested and screenshotted on a headless output without touching anyone's
// keyboard or pointer (AGENTS.md, "Testing the GUI"). Included by main.c after its state.
//
// One command per line; # starts a comment.
//   settle                  wait until the window's size has held for 20 frames (the compositor
//                           resizes a new window after its first frames)
//   wait <frames>
//   sleep <ms>              frames until that much time has passed
//   key <name>              the event a real key sends (ui/keys.c, F15): ctrl-, shift- and alt-
//                           before up down left right pageup pagedown home end enter backspace esc
//                           tab delete menu f1 .. f25 equal minus, or the key's character (ctrl-t,
//                           alt-1, alt-f); space a typed space; close-window the compositor's close
//                           (as SIGUSR1 from another window's Close All). A name that is no key
//                           fails the script; a key that means nothing sends nothing, as on_key.
//   char <c>                one typed character
//   keys <word>...          keys and characters in one frame's input, as fast typing during a slow
//                           frame gives them: each word a key name (as for `key`) or char:<c>
//   type <text>             the rest of the line, after one space, as typed characters
//   wheel <notches> [ctrl|side] over the list; positive away from the user (up); with side over
//                           the sidebar; with ctrl at the
//                           window's right edge, as the mouse's wheel goes (wheel_at): the zoom
//   press <row> [ctrl][shift] a left press on list row <row> (0 = the first on screen; in the icon
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
//   point-side <name>       the same for the sidebar row shown with that name (a tree folder's, a
//                           place's label: F13)
//   hover-here              the pointer to the last point-ui (the hover highlights, a drive's
//                           eject button, a button's tooltip); it rests there, hit again after
//                           each frame as the mouse's is (hover_refresh)
//   pointer-off             the pointer out of the window
//   idle                    the window waits for events as it does unscripted, until a tooltip
//                           waiting is due (or the search field's text, F9); a wake-up 3 s on stands in if nothing comes, and the
//                           state's idlewoke says which ended the wait (timer or fallback)
//   click-here              a left press at the last point-ui
//   drag-here <dx>          a left press there, the pointer <dx> px across and the button let go,
//                           in one frame, as the mouse's go (drag_at: the zoom slider's knob, the
//                           sidebar's edge)
//   press-here              a left press at the last point, the button held across frames (F13)
//   move-to <x> <y> [ctrl][shift]  the pointer there (it rests there, as hover-here's); a held button
//                           drags, with those keys held (a drop's copy or move, F13 step 3)
//   move-here [ctrl][shift] the same, to the last point-ui
//   move-by <dx> <dy> [ctrl][shift]  the same, that far from where the pointer is
//   release [ctrl][shift]   the button let go, those keys held
//   middle-here             a middle press there (a folder into a background tab, a tab closed)
//   middle <row>            a middle press on list row <row>
//   right-here              a right press there (F8: an entry's menu, or the folder shown's on empty space)
//   right-at <x> <y>        a right press at a point
//   back, forward           the mouse's back and forward buttons
//   raw-button <n>          GLFW's button n (1-8) pressed and released through input.c's callback
//   raw-key <key> [mods]    GLFW key code <key> pressed through input.c's key callback, with GLFW's
//                           mod bits (1 Shift, 2 Ctrl, 4 Alt): what gui/glfwkeys.c makes of it
//   user-keys               from here on the compositor's keyboard reaches the window too, keys
//                           and characters through GLFW's callbacks as a user's: `sh vkbd … &` in
//                           the test sway (tools/gui/sway.sh; weston has no virtual keyboard)
//   user-pointer            from here on the compositor's pointer reaches the window as a user's,
//                           raylib's button state and all (hover, presses, drags): `sh vptr … &` in
//                           the test sway; the scripted pointer commands are not for after it
//   clip-drop               the system clipboard's own connection cut, as a compositor that drops it
//                           (the clip group, in the test sway)
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
//                           expect and wait-for read the line as of the last frame an unscripted
//                           window would draw too (asked for by input, a worker's answer or a
//                           timer), so a change that asks for no frame fails them: the screen
//                           would not show it (2026-11-05). A frame the script gives no input to
//                           (wait, sleep, expect, sh, point-ui, ...) asks for none by itself, and a
//                           pointer move only when it changes what is lit or dragged.
//   wait-for <ms> <word>... frames until every word is in the state line (a reload comes in its
//                           own time); an expectation, failed when <ms> pass
//   quit
//   quits <frames>          the next command must make the window quit within that many frames
//                           (the last tab's Ctrl+W); what follows it is never run. Without it, a
//                           window that quits with commands left fails the run

#include <spawn.h>
#include <sys/socket.h>
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
    bool  held;          // the left button down (press-here), until release (F13)
    int   quits;         // frames left for the window to quit in, after the command after `quits`
    bool  quits_ran;     // that command ran
    Str   typing;        // what is left of a `type` longer than a frame's events
    char  drawn[16384];  // the state line as of the last frame an unscripted window would draw too
    bool  pending;       // a frame is asked for, not drawn yet: what it will show is the state now
    bool  gave;          // this frame's command was input (a key, the pointer), not a wait or a check
    bool  user_keys;     // the keyboard's keys and characters reach the window (`user-keys`)
    bool  user_pointer;  // the pointer's input reaches it, as an unscripted window's (`user-pointer`)
} script;

static u8 script_mem[KiB(256)];

// Whether the badge was drawn inside the window, whole (F13 step 4).
static bool badge_in(void)
{
    Clay_BoundingBox b = Clay_GetElementData(CLAY_ID("drag-badge")).boundingBox;
    return b.x >= 0 && b.y >= 0 && b.x + b.width <= (float)GetScreenWidth() && b.y + b.height <= (float)GetScreenHeight();
}

// The keys a pointer command says are held: ctrl, shift (UI_MOD_*), among its words (press, the
// drag's moves and release).
static int script_mods(const char *a)
{
    return (strstr(a, "ctrl") ? UI_MOD_CTRL : 0) | (strstr(a, "shift") ? UI_MOD_SHIFT : 0);
}

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

// s's spaces as '_', in place (the state line is split at spaces). Returns s.
static char *state_unspace(char *s)
{
    for (char *c = s; *c; c++)
        if (*c == ' ') *c = '_';
    return s;
}

// A word of the state line: s, "-" when it is empty, spaces as '_' (the line is split at spaces;
// F9's review, 2026-10-29: this was written out at each word). Into out; returns out.
static char *state_word(char *out, size_t cap, Str s)
{
    snprintf(out, cap, "%.*s", s.len ? (int)s.len : 1, s.len ? s.p : "-");
    return state_unspace(out);
}

static void script_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// The state line's words for the drags (F13: inside the window, F14: with other programs; split out
// of script_state, code-health review 2026-11-06): what the left button holds, the files dragged
// and where they would land (kind:place), what the drop does and its folder, the outline and the
// badge as drawn, the spring, and the compositor's side (gui/dnd.c). Its length, as snprintf's.
static int state_drag(char *out, size_t cap)
{
    static const char *const DRAG_WORDS[] = { "-", "zoom", "side", "press", "files", "out", "in" };
    _Static_assert(sizeof DRAG_WORDS / sizeof DRAG_WORDS[0] == DRAG_IN + 1, "a word for each DRAG_*");
    static const char *const DROP_WORDS[] = { "-", "row", "side", "tab", "crumb", "list" }, *const OP_WORDS[] = { "move", "copy", "trash" };
    char dragname[NAME_MAX + 1] = "-", dropw[32] = "-", dropdir[NAME_MAX + 1] = "-";
    char dropfirst[NAME_MAX + 1] = "-";   // (F14: the first file of the last drop from another program)
    if (dnd.got.n) state_word(dropfirst, sizeof dropfirst, path_base(str_from_c(dnd.got.path[0])));
    if (files_over()) state_word(dragname, sizeof dragname, str_from_c(app.drag_name));
    if (files_over() && app.drop.kind != DROP_NONE) snprintf(dropw, sizeof dropw, "%s:%d", DROP_WORDS[app.drop.kind], app.drop.at);
    if (files_over() && app.drop.ok) state_word(dropdir, sizeof dropdir, path_base(str_from_c(app.drop.dir)));
    const char *dropop = files_over() && app.drop.ok ? OP_WORDS[drop_op(&app.drop)] : "-";
    const char *badge = !badge_drawn ? "-" : app.in_nofiles && app.drag == DRAG_NONE ? "nofiles" : badge_op ? badge_op : "none";
    return snprintf(out, cap,
                    " drag=%s edgelit=%d resizeptr=%d dragn=%td dragname=%s dragicon=%s drop=%s dropok=%d dropop=%s dropdir=%s dropmark=%d badge=%s"
                    " spring=%d springbar=%d badgein=%d dndok=%d dndpress=%d dndover=%d dndat=%.0f,%.0f dndaction=%u dndreading=%d drops=%d"
                    " dropfail=%d dropn=%td dropfirst=%s owndrops=%d dndout=%s dndoutact=%u dndsent=%lld dndwrote=%d dndicon=%d",
                    DRAG_WORDS[app.drag], edge_lit && side_w(), cursor_now == MOUSE_CURSOR_RESIZE_EW, files_over() ? dragged.n : (isize)0,
                    dragname, badge_icon[0] ? badge_icon : "-", dropw, app.drop.ok, dropop, dropdir, drop_marked, badge, app.spring_tab,
                    spring_fill < 0 ? -1 : (int)(spring_fill * 100), badge_drawn && badge_in(), dnd.ok, dnd.press != 0, dnd.over >= 0,
                    dnd.x, dnd.y, dnd.action, dnd.reading, dnd.drops, dnd.drops_failed, dnd.got.n, dropfirst, dnd.own_drops,
                    DND_OUT_WORD[dnd.out], dnd.out_action, (long long)atomic_load(&dnd.sent), atomic_load(&dnd.wrote), dnd.icon != NULL);
}

// Words of the state line appended at out + *len (cap the whole line's room), each with its own
// printf so the compiler checks its format against its arguments (the F15 code-health review: one
// snprintf of about 200 words, its arguments far from their words). *len counts on past cap, as
// snprintf does, so the caller sees a line cut short.
static void sw(char *out, size_t cap, int *len, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void sw(char *out, size_t cap, int *len, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf((size_t)*len < cap ? out + *len : NULL, (size_t)*len < cap ? cap - (size_t)*len : 0, fmt, ap);
    va_end(ap);
    if (n > 0) *len += n;
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
    if (app.ui.focus == UI_FOCUS_CRUMBS) snprintf(keyed, sizeof keyed, app.ui.crumb_at >= app.crumbs.n ? "branch" : "%d", app.ui.crumb_at);   // (past the path: the branch button, F12 step 5c)
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
    // The cursor's name colour as an unselected row would have it: its git change's ("git") or else
    // its type's; and the rows whose git letter or dot was drawn (F12).
    const char *gitnamecol = "-";
    int gitcells = 0;
    if (app.have_list && app.list.n) {
        GitState gs = entry_git(app.ui.list.sel);
        Clay_Color c = row_name_colour(app.ui.list.sel, false, false, gs), g = git_colour(gs);
        gitnamecol = gs != GS_NONE && c.r == g.r && c.g == g.g && c.b == g.b ? "git" : "type";
    }
    for (int r = 0; r < app.ui.list.rows; r++) gitcells += Clay_GetElementData(CLAY_IDI("git-mark", r)).found;
    static const char *const GIT_WORDS[GS_N] = { "-", "ignored", "untracked", "staged", "renamed", "modified", "conflict" };   // (the cursor's, F12)
    static const char *const MODALS[] = {
        [UI_ON_NONE] = "-", [UI_ON_DELETE] = "delete", [UI_ON_CLASH] = "clash", [UI_ON_CANCEL] = "cancel", [UI_ON_QUIT] = "quit",
        [UI_ON_FULL] = "full", [UI_ON_EMPTY] = "empty", [UI_ON_CLEAR] = "clear-previews", [UI_ON_PROPS] = "props", [UI_ON_OW] = "open-with",
        [UI_ON_BM] = "bookmarks", [UI_ON_KEYS] = "shortcuts", [UI_ON_ABOUT] = "about", [UI_ON_BAR] = "menubar", [UI_ON_RESTORE] = "restore", [UI_ON_BRANCHES] = "branches", [UI_ON_LOGIN] = "login",
        [UI_ON_SHARES] = "shares",
    };
    _Static_assert(COUNT(MODALS) == UI_ON_KINDS, "a name for each panel");
    // The path buttons' labels, '|' between; the cursor's folder count (- unknown, ? unreadable).
    char crumbs[2048] = "", cnt[32] = "-";
    for (int i = 0; i < app.crumbs.n; i++) {
        size_t k = strlen(crumbs);
        snprintf(crumbs + k, sizeof crumbs - k, "%s%.*s", i ? "|" : "", STR_ARG(ui_crumb_label(&app.crumbs, i)));
    }
    state_unspace(crumbs);   // (a share's button: "pub on nas", F16 step 5)
    // The tabs' folder names in order, '|' between (the current one from app.cwd).
    char tabdirs[1024] = "";
    for (int i = 0; i < tabbar.n; i++) {
        const char *d = tab_dir(i);
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
        snprintf(sides + k, sizeof sides - k, "%s%s%s", i ? "|" : "", p->kind == PL_HEAD ? "^" : p->kind == PL_UNMOUNTED || p->kind == PL_NETBM ? "~" : "", p->label);
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
    if (app.ui.side_mode == SIDE_PLACES && hi >= 0 && hi < app.places.n) ejectbtn = place_unmountable(&app.places.p[hi]);
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
    // The footer's git part (" · N changed"), as drawn: the free space before it varies (F12 step 5a review).
    char infogit[64] = "-";
    // The branch button and its menu (F12 step 5b): the label, the list as a|b|c.
    char branch[GIT_HEAD_MAX + 32] = "-", brlist[UI_MENU_BRANCHES * GIT_HEAD_MAX] = "-", brnote[GIT_HEAD_MAX + 32] = "-";
    if (app.ui.br_note) state_word(brnote, sizeof brnote, branch_menu_label(UM_BR_NOTE));   // (the note as drawn)
    // The branches panel (F12 step 6): the cursor's branch, the filter's text.
    char brpsel[GIT_HEAD_MAX] = "-", brptext[UI_FIND_MAX + 1] = "-";
    if (app.ui.panel == UI_PANEL_BRANCHES && app.ui.brp.list.n > 0 && app.ui.brp_idx[app.ui.brp.list.sel] < git.br.n)
        state_word(brpsel, sizeof brpsel, str_from_c(git.br.name[app.ui.brp_idx[app.ui.brp.list.sel]]));
    if (app.ui.brp.filter.len) state_word(brptext, sizeof brptext, (Str){ app.ui.brp.filter.b, app.ui.brp.filter.len });
    // A server's shares (F16 step 7a): the cursor's, the filter's text, the list as read.
    char shpsel[SERVER_SHARE_NAME] = "-", shptext[UI_FIND_MAX + 1] = "-", shplist[2048] = "-";
    if (app.ui.panel == UI_PANEL_SHARES && app.ui.shp.list.n > 0 && app.ui.shp_idx[app.ui.shp.list.sel] < shp.n)
        state_word(shpsel, sizeof shpsel, str_from_c(shp.name[app.ui.shp_idx[app.ui.shp.list.sel]]));
    if (app.ui.shp.filter.len) state_word(shptext, sizeof shptext, (Str){ app.ui.shp.filter.b, app.ui.shp.filter.len });
    for (int i = 0, o = 0; i < shp.n && o < (int)sizeof shplist - 1; i++) {
        char w[SERVER_SHARE_NAME];
        state_word(w, sizeof w, str_from_c(shp.name[i]));
        o += snprintf(shplist + o, sizeof shplist - (size_t)o, "%s%s", i ? "|" : "", w);
        if (o >= (int)sizeof shplist) shplist[sizeof shplist - 1] = 0;
    }
    if (branch_label()) state_word(branch, sizeof branch, str_from_c(branch_label()));
    // The button inside the path's part of the bar (the path buttons gave it room); its menu's top
    // left at the button's bottom left.
    Clay_ElementData bb = Clay_GetElementData(Clay_GetElementId(cz(BAR_IDS[UB_BRANCH]))), cb = Clay_GetElementData(CLAY_ID("crumbs")), mbb = Clay_GetElementData(CLAY_ID("menu"));
    Clay_ElementData gb = app.crumb_arrows ? Clay_GetElementData(bar_id(UB_RIGHT)) : Clay_GetElementData(CLAY_IDI("crumb", app.crumb_hi - 1));   // (the path's last: its children, as Clay squeezes the group)
    Clay_String btext = branch_label() ? branch_text(branch_label(), branch_btn_w((float)GetScreenWidth(), branch_label())) : (Clay_String){ 0 };
    int brfits = bb.found && cb.found && gb.found && bb.boundingBox.x + bb.boundingBox.width <= cb.boundingBox.x + cb.boundingBox.width + 0.5f &&
                 gb.boundingBox.x + gb.boundingBox.width <= bb.boundingBox.x + 0.5f &&
                 Clay_GetElementData(CLAY_ID("crumb-group")).boundingBox.width <= bar_room((float)GetScreenWidth()) - bb.boundingBox.width - BAR_GAP + 0.5f &&
                 bb.boundingBox.width + 0.5f >= str_width((Str){ btext.chars, btext.length }) + 2 * CRUMB_PAD;   // (whole: Clay squeezes it too)
    for (int i = app.crumb_lo; brfits && i < app.crumb_hi; i++)   // (nor the path buttons: Clay shrinks them to fit)
        brfits = Clay_GetElementData(CLAY_IDI("crumb", i)).boundingBox.width + 0.5f >= crumb_width(i);
    int brunder = bb.found && mbb.found && fabsf(mbb.boundingBox.x - bb.boundingBox.x) < 0.5f && fabsf(mbb.boundingBox.y - (bb.boundingBox.y + bb.boundingBox.height)) < 0.5f;
    for (int i = 0, at = 0; i < app.ui.br_n && at < (int)sizeof brlist - 1; i++)   // (the menu's 8)
        at += snprintf(brlist + at, sizeof brlist - (size_t)at, "%s%s", i ? "|" : "", git.br.name[i]);
    const char *dotp = strstr(info, "\xC2\xB7 ");
    if (dotp && strstr(dotp, "changed")) state_word(infogit, sizeof infogit, str_from_c(dotp + 3));
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
    // The open menu's keys as it shows them (F15): word=key for each entry with one.
    char menukeys[1024] = "";
    for (int i = 0; app.ui.menu_on && i < UI_MENU[app.ui.menu].n; i++) {
        UiMenuItem it = UI_MENU[app.ui.menu].items[i];
        size_t k = strlen(menukeys);
        if (ui_menu_key(it)[0]) snprintf(menukeys + k, sizeof menukeys - k, "%s%s=%s", k ? "," : "", UI_MENU_ITEM[it].word, ui_menu_key(it));
    }
    if (!menukeys[0]) snprintf(menukeys, sizeof menukeys, "-");
    state_unspace(menukeys);
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
    // And whether menu_size, which places a right-click menu before it is drawn, measured it as
    // drawn (the F15 code-health review found them apart for a long branch name).
    Vector2 msz = app.ui.menu_on && mb.found ? menu_size(app.ui.menu) : (Vector2){ 0 };
    int menusized = app.ui.menu_on && mb.found && fabsf(msz.x - 2 - mb.boundingBox.width) < 1 && fabsf(msz.y - 2 - mb.boundingBox.height) < 1;   // (its border outside the box)
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
    state_unspace(bmlist);
    if (!bmlist[0]) snprintf(bmlist, sizeof bmlist, "-");
    // Keyboard Shortcuts' first row shown: its action (spaces '_'; a heading "#" and its name).
    char keysfirst[128] = "-";
    for (isize v = app.ui.keys_list.top; app.ui.panel == UI_PANEL_KEYS && v < app.ui.keys_list.n; v++) {
        int line = app.ui.keys_row[v];
        snprintf(keysfirst, sizeof keysfirst, "%s%s", line < 0 ? "#" : "", line < 0 ? UI_KEY_SECTION[-1 - line] : UI_KEYS[line].action);
        break;
    }
    state_unspace(keysfirst);
    char keyscap[UI_KEY_NAME_MAX + 16];   // and the first key it drew (spaces '_'; "-" none)
    snprintf(keyscap, sizeof keyscap, "%s", app.ui.panel == UI_PANEL_KEYS && keys_cap_drawn ? keys_cap_drawn : "-");
    state_unspace(keyscap);
    bool keyscapmine = app.ui.panel == UI_PANEL_KEYS && keys_cap_drawn && keys_cap_mine;
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
    state_word(owtext, sizeof owtext, (Str){ app.ui.ow.filter.b, app.ui.ow.filter.len });
    isize owsel = app.ui.panel == UI_PANEL_OW && app.ui.ow.list.n > 0 ? app.ui.ow_idx[app.ui.ow.list.sel] : -1;
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
    state_unspace(findtext);
    char propwhere[PATH_MAX], propdrive[256], proplink[PATH_MAX];
    snprintf(propwhere, sizeof propwhere, "%s", props.where[0] ? props.where : "-");
    snprintf(propdrive, sizeof propdrive, "%s", props.drive[0] ? props.drive : "-");
    snprintf(proplink, sizeof proplink, "%s", props.link[0] ? props.link : "-");
    state_unspace(propwhere);
    state_unspace(propdrive);
    state_unspace(proplink);
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
    int len = 0;
    sw(out, cap, &len, "dir=%s ", app.cwd);
    sw(out, cap, &len, "n=%td ", app.list.n);
    sw(out, cap, &len, "hidden=%td ", app.list.hidden);
    sw(out, cap, &len, "sel=%.*s ", STR_ARG(sel));
    sw(out, cap, &len, "idx=%td ", app.ui.list.sel);
    sw(out, cap, &len, "top=%td ", app.ui.list.top);
    sw(out, cap, &len, "rows=%d ", app.ui.list.rows);
    sw(out, cap, &len, "hover=%d ", app.hov.row);
    sw(out, cap, &len, "accent=#%02x%02x%02x ", t->accent.r, t->accent.g, t->accent.b);
    sw(out, cap, &len, "listacc=#%02x%02x%02x ", app.theme_list.accent.r, app.theme_list.accent.g, app.theme_list.accent.b);
    sw(out, cap, &len, "markedname=%s ", marked_name_drawn.a ? arena_sprintf(&frame, "#%02x%02x%02x", (int)marked_name_drawn.r, (int)marked_name_drawn.g, (int)marked_name_drawn.b) : "-");
    sw(out, cap, &len, "bg=#%02x%02x%02x ", t->bg.r, t->bg.g, t->bg.b);
    sw(out, cap, &len, "icons=%s ", t->icons[0] ? t->icons : "-");
    sw(out, cap, &len, "scale=%d ", app.icon_scale);
    sw(out, cap, &len, "w=%d ", GetScreenWidth());
    sw(out, cap, &len, "h=%d ", GetScreenHeight());
    sw(out, cap, &len, "themes=%d ", app.themes);
    sw(out, cap, &len, "frame_ms_max=%.1f ", (double)frame_us_max / 1000.0);
    sw(out, cap, &len, "icon=%s ", icon);
    sw(out, cap, &len, "shown=%d ", app.show_hidden);
    sw(out, cap, &len, "sort=%s ", app.ui.sort.key == SORT_NAME ? "name" : app.ui.sort.key == SORT_SIZE ? "size" : "mtime");
    sw(out, cap, &len, "rev=%d ", app.ui.sort.reverse);
    sw(out, cap, &len, "finding=%d ", app.ui.finding);
    sw(out, cap, &len, "findkeys=%d ", app.ui.focus == UI_FOCUS_FIND);
    sw(out, cap, &len, "miss=%d ", app.ui.find_miss);
    sw(out, cap, &len, "find=%.*s ", app.ui.find.len ? app.ui.find.len : 1, app.ui.find.len ? app.ui.find.b : "-");
    sw(out, cap, &len, "last=%.*s ", app.ui.last_len ? app.ui.last_len : 1, app.ui.last_len ? app.ui.last : "-");
    sw(out, cap, &len, "side=%td ", app.ui.side.sel);
    sw(out, cap, &len, "sidefocus=%d ", app.ui.focus == UI_FOCUS_SIDE);
    sw(out, cap, &len, "place=%s ", app.ui.side_mode == SIDE_PLACES && app.ui.side.sel < app.places.n ? app.places.p[app.ui.side.sel].label : "-");
    sw(out, cap, &len, "here=%s ", app.place_cur >= 0 ? app.places.p[app.place_cur].label : "-");
    sw(out, cap, &len, "back=%d ", hist.nback);
    sw(out, cap, &len, "fwd=%d ", hist.nfwd);
    sw(out, cap, &len, "marked=%td ", app.ui.marked);
    sw(out, cap, &len, "editing=%d ", app.ui.focus == UI_FOCUS_EDIT);
    sw(out, cap, &len, "edit=%s ", edit);
    sw(out, cap, &len, "confirming=%d ", app.ui.confirming);
    sw(out, cap, &len, "asking=%d ", app.ui.asking);
    sw(out, cap, &len, "job=%s ", !oldest ? "-" : JOB_PHASES[atomic_load(&oldest->j.phase)]);
    sw(out, cap, &len, "undo=%s ", UNDO_KINDS[undo.kind]);
    sw(out, cap, &len, "clip=%td ", own.n);
    sw(out, cap, &len, "cut=%d ", own.cut);
    sw(out, cap, &len, "sysclip=%d ", clip.ok);
    sw(out, cap, &len, "modal=%s ", MODALS[modal_kind()]);
    sw(out, cap, &len, "askall=%d ", app.ui.ask_all);
    sw(out, cap, &len, "tabs=%d ", tabbar.n);
    sw(out, cap, &len, "tab=%d ", tabbar.cur);
    sw(out, cap, &len, "tabdirs=%s ", tabdirs);
    sw(out, cap, &len, "sizecell=%s ", sizecell);
    sw(out, cap, &len, "crumbs=%s ", crumbs);
    sw(out, cap, &len, "crumbhome=%d ", app.crumbs.seg[0].home ? 0 : -1);
    sw(out, cap, &len, "crumbshare=%d ", app.crumbs.n > 0 && app.crumbs.seg[0].share);   // (the first button a share's, F16 step 5)
    sw(out, cap, &len, "crumb=%d ", app.crumbs.cur);
    sw(out, cap, &len, "crumbshown=%d-%d ", app.crumb_lo, app.crumb_hi);
    sw(out, cap, &len, "arrows=%d ", app.crumb_arrows);
    sw(out, cap, &len, "locating=%d ", app.ui.focus == UI_FOCUS_EDIT && app.ui.edit_path);
    sw(out, cap, &len, "cnt=%s ", cnt);
    sw(out, cap, &len, "esel=%d-%d ", slo, shi);
    sw(out, cap, &len, "caret=%d ", app.ui.edit.caret);
    sw(out, cap, &len, "copied=%s ", copied);
    sw(out, cap, &len, "crumbkey=%s ", keyed);
    sw(out, cap, &len, "jobs=%d ", jobs.nrun);
    sw(out, cap, &len, "waiting=%d ", jobs.nwait);
    sw(out, cap, &len, "jobrows=%s ", joblabels);
    sw(out, cap, &len, "jobkey=%s ", jobkey);
    sw(out, cap, &len, "jobspeed=%d ", oldest && oldest->line.now > 0);
    sw(out, cap, &len, "clashjob=%s ", clashjob);
    sw(out, cap, &len, "sides=%s ", sides[0] ? sides : "-");
    sw(out, cap, &len, "spaced=%d ", spaced);
    sw(out, cap, &len, "sidemode=%s ", SIDE_MODES_W[app.ui.side_mode]);
    sw(out, cap, &len, "treecur=%s ", treecur);
    sw(out, cap, &len, "treehere=%d ", treehere);
    sw(out, cap, &len, "trash=%d ", app.ui.view == UI_VIEW_TRASH);
    sw(out, cap, &len, "sidehover=%d ", app.hov.side);
    sw(out, cap, &len, "hovernum=%s ", hovernum);
    sw(out, cap, &len, "ejectbtn=%d ", ejectbtn);
    sw(out, cap, &len, "cwdavail=%d ", app.cwd_avail >= 0);
    sw(out, cap, &len, "info=%s ", info);
    sw(out, cap, &len, "view=%s ", app.ui.grid ? "icons" : "list");
    sw(out, cap, &len, "cols=%d ", (int)ui_cols(&app.ui.list));
    sw(out, cap, &len, "cells=%d ", app.grid_cells);
    sw(out, cap, &len, "cellfirst=%td ", app.grid_first);
    sw(out, cap, &len, "celllast=%td ", app.grid_last);
    sw(out, cap, &len, "celllines=%d ", app.grid_hover_lines);
    sw(out, cap, &len, "cellcut=%d ", app.grid_hover_cut);
    sw(out, cap, &len, "iconpx=%d ", entry_px());
    sw(out, cap, &len, "icontex=%d ", icontex);
    sw(out, cap, &len, "iconresets=%d ", app.icon_full_resets);
    sw(out, cap, &len, "crumbfocus=%d ", app.ui.focus == UI_FOCUS_CRUMBS);
    sw(out, cap, &len, "tooltip=%s ", tipword);
    sw(out, cap, &len, "idlewoke=%s ", idle_woke);
    sw(out, cap, &len, "zoom=%d ", ui_zoom_px(&app.ui));
    sw(out, cap, &len, "cellw=%d ", cell_w());
    sw(out, cap, &len, "cellh=%d ", cell_h());
    sw(out, cap, &len, "zoomshown=%d ", Clay_GetElementData(CLAY_ID("zoom-track")).found);
    sw(out, cap, &len, "tipin=%d ", tipin);
    sw(out, cap, &len, "slots=%d ", app.nslots);
    sw(out, cap, &len, "previews=%d ", thumbs.drawn_now);
    sw(out, cap, &len, "thumbsel=%s ", thumbsel);
    sw(out, cap, &len, "thumbbox=%s ", thumbbox);
    sw(out, cap, &len, "thumbbusy=%d ", thumbs_busy());
    sw(out, cap, &len, "thumbmade=%d ", thumbs.got[TG_MADE]);
    sw(out, cap, &len, "thumbread=%d ", thumbs.got[TG_CACHED]);
    sw(out, cap, &len, "thumbfailed=%d ", thumbs.got[TG_FAILED]);
    sw(out, cap, &len, "thumbknown=%d ", thumbs.got[TG_FAIL_KNOWN]);
    sw(out, cap, &len, "thumbnone=%d ", thumbs.got[TG_NONE]);
    sw(out, cap, &len, "thumbchanged=%d ", thumbs.got[TG_CHANGED]);
    sw(out, cap, &len, "thumbheld=%td ", thumbs.held);
    sw(out, cap, &len, "thumbdropped=%d ", thumbs.evicted);
    sw(out, cap, &len, "thumbresets=%d ", thumbs.resets);
    sw(out, cap, &len, "thumblocal=%d ", app.thumb_local);
    sw(out, cap, &len, "reloadasked=%d ", app.reloads_asked);
    sw(out, cap, &len, "menuseps=%d ", menuseps);
    sw(out, cap, &len, "bmlist=%s ", bmlist);
    sw(out, cap, &len, "bmn=%d ", app.ui.bm_n);
    sw(out, cap, &len, "bmhere=%d ", app.ui.bm_here);
    sw(out, cap, &len, "canbm=%d ", app.ui.can_bookmark);
    sw(out, cap, &len, "bmmore=%d ", app.ui.bm_more);
    sw(out, cap, &len, "bmp=%d ", app.ui.panel == UI_PANEL_BM);
    sw(out, cap, &len, "bmpn=%td ", app.ui.bmp_list.n);
    sw(out, cap, &len, "bmpsel=%td ", app.ui.bmp_list.sel);
    sw(out, cap, &len, "bmpnaming=%d ", app.ui.bmp_naming);
    sw(out, cap, &len, "bmptext=%s ", bmptext);
    sw(out, cap, &len, "keys=%d ", app.ui.panel == UI_PANEL_KEYS);
    sw(out, cap, &len, "keysn=%td ", app.ui.keys_list.n);
    sw(out, cap, &len, "keystop=%td ", app.ui.keys_list.top);
    sw(out, cap, &len, "keysrows=%d ", app.ui.keys_list.rows);
    sw(out, cap, &len, "keysfirst=%s ", keysfirst);
    sw(out, cap, &len, "keyscap=%s ", keyscap);
    sw(out, cap, &len, "keyscapmine=%d ", keyscapmine);
    sw(out, cap, &len, "keysbad=%d ", conf_keys_read.bad);
    sw(out, cap, &len, "keyclashes=%d ", conf_keys_read.clashes);
    sw(out, cap, &len, "menukeys=%s ", menukeys);
    sw(out, cap, &len, "menu=%s ", app.ui.menu_on ? UI_MENU[app.ui.menu].title : "-");
    sw(out, cap, &len, "menuat=%s ", menuat);
    sw(out, cap, &len, "menuoff=%s ", menuoff);
    sw(out, cap, &len, "menugrey=%s ", menugrey);
    sw(out, cap, &len, "menuhid=%s ", menuhid);
    sw(out, cap, &len, "menumarked=%s ", menumarked);
    sw(out, cap, &len, "menubox=%s ", menubox);
    sw(out, cap, &len, "menusized=%d menusize=%.0fx%.0f ", menusized, msz.x, msz.y);
    sw(out, cap, &len, "menuin=%d ", menuin);
    sw(out, cap, &len, "menuclear=%d ", menuclear);
    sw(out, cap, &len, "permscols=%d ", app.ui.perms_cols);
    sw(out, cap, &len, "permsshown=%d ", perms_shown((float)GetScreenWidth()));
    sw(out, cap, &len, "permsnote=%d ", (int)(strstr(info, "Permissions_hidden") != NULL));
    sw(out, cap, &len, "groupcol=%d ", app.ui.group_col);
    sw(out, cap, &len, "groupdrawn=%d ", (int)Clay_GetElementData(CLAY_ID("col-group")).found + (int)Clay_GetElementData(CLAY_IDI("group", 0)).found);
    sw(out, cap, &len, "colors=%d ", app.ui.colors);
    sw(out, cap, &len, "barhidden=%d ", app.ui.bar_hidden);
    sw(out, cap, &len, "barshown=%d ", bar_shown());
    sw(out, cap, &len, "menuhover=%d ", app.hov.menu_title);
    sw(out, cap, &len, "canwrite=%d ", app.ui.can_write);
    sw(out, cap, &len, "faron=%d ", app.far_on);
    sw(out, cap, &len, "farloading=%d ", app.far_loading);
    sw(out, cap, &len, "farwait=%d ", app.far_waiting);
    sw(out, cap, &len, "farslow=%d ", app.far_slow);
    char farnote[sizeof app.far_note];
    sw(out, cap, &len, "farnote=%s ", state_word(farnote, sizeof farnote, str_from_c(app.far_note)));
    pthread_mutex_lock(&far.mu);
    sw(out, cap, &len, "farkills=%d ", far.kills);
    sw(out, cap, &len, "farheld=%d ", far.nkilled);
    sw(out, cap, &len, "farasked=%d ", far.asked);
    sw(out, cap, &len, "fardos=%d ", far.dos);
    bool word_bad;
    const char *word = app.have_list && !app.list.n ? list_word(&word_bad) : "";
    char listword[sizeof app.far_note];
    sw(out, cap, &len, "listword=%s ", state_word(listword, sizeof listword, str_from_c(word)));
    pthread_mutex_unlock(&far.mu);
    // Connect to Server (F16 step 4): the thread's phase (0 idle, 1 gio runs, 2 a question, 3 a
    // result), the questions put, whether ofx is dumpable (not while one runs); the question up, its field with the keys, asked again, no user
    // field; its user, and the password's length only (never its text).
    pthread_mutex_lock(&cn.mu);
    sw(out, cap, &len, "sharesheld=%d ", gvfsw.held >= 0);   // (step 5 review: a share lister that didn't answer, killed, held now)
    sw(out, cap, &len, "conn=%d connasked=%d dumpable=%d ", (int)cn.phase, cn.asked, prctl(PR_GET_DUMPABLE, 0, 0, 0, 0));
    pthread_mutex_unlock(&cn.mu);
    char loginuser[256];
    sw(out, cap, &len, "login=%d loginat=%d loginagain=%d loginnouser=%d loginuser=%s loginpass=%d ", app.ui.panel == UI_PANEL_LOGIN, app.ui.login_at, app.ui.login_again,
       app.ui.login_no_user, state_word(loginuser, sizeof loginuser, (Str){ app.ui.login_user.b, app.ui.login_user.len }), app.ui.login_pass.len);
    sw(out, cap, &len, "windows=%d ", windows);
    sw(out, cap, &len, "siderows=%d ", app.ui.side.rows);
    sw(out, cap, &len, "sidetop=%td ", app.ui.side.top);
    sw(out, cap, &len, "footerin=%d ", footerin);
    sw(out, cap, &len, "props=%d ", app.ui.panel == UI_PANEL_PROPS);
    sw(out, cap, &len, "propat=%d ", app.ui.prop_at);
    sw(out, cap, &len, "propcan=%d ", app.ui.prop_can);
    sw(out, cap, &len, "propn=%td ", props.n);
    sw(out, cap, &len, "propmode=%04o ", props.have ? (unsigned)(props.st.st_mode & 07777) : 0u);
    sw(out, cap, &len, "propfiles=%lld ", (long long)props.sum.files);
    sw(out, cap, &len, "propdirs=%lld ", (long long)props.sum.dirs);
    sw(out, cap, &len, "propbytes=%lld ", (long long)props.sum.bytes);
    sw(out, cap, &len, "propdone=%d ", props.done);
    sw(out, cap, &len, "propcut=%d ", props.sum.cut || props.cut);
    sw(out, cap, &len, "proplinks=%lld,%lld,%lld,%d ", (long long)props.sum.linked_names, (long long)props.sum.linked_files, (long long)props.sum.once_bytes, props.sum.once_over);
    sw(out, cap, &len, "propnlink=%llu ", props.have ? (unsigned long long)props.st.st_nlink : 0ull);
    sw(out, cap, &len, "proptype=%s ", props.type[0] ? props.type : "-");
    sw(out, cap, &len, "proptypename=%s ", proptypename);
    sw(out, cap, &len, "propbyname=%d ", props.type_by_name);
    sw(out, cap, &len, "propname=%s ", propname);
    sw(out, cap, &len, "propwhere=%s ", propwhere);
    sw(out, cap, &len, "propdrive=%s ", propdrive);
    sw(out, cap, &len, "proplink=%s ", proplink);
    sw(out, cap, &len, "propcreated=%d ", props.btime >= 0);
    sw(out, cap, &len, "propidx=%td ", props.idx);
    sw(out, cap, &len, "ow=%d ", app.ui.panel == UI_PANEL_OW);
    sw(out, cap, &len, "owfresh=%d ", app.ui.ow.fresh);
    sw(out, cap, &len, "owlist=%d ", app.ui.ow.in_list);
    sw(out, cap, &len, "owtext=%s ", owtext);
    sw(out, cap, &len, "own=%td ", app.ui.ow.list.n);
    sw(out, cap, &len, "owsel=%td ", app.ui.ow.list.sel);
    sw(out, cap, &len, "owapp=%s ", owapp);
    sw(out, cap, &len, "owicon=%s ", owicon);
    sw(out, cap, &len, "owtex=%d ", owtex);
    sw(out, cap, &len, "owtype=%s ", ow.type[0] ? ow.type : "-");
    sw(out, cap, &len, "owfiles=%td ", ow.n);
    sw(out, cap, &len, "owopts=%s ", owopts);
    sw(out, cap, &len, "owbad=%d ", app.ui.ow.m.bad);
    sw(out, cap, &len, "owdef=%d ", app.ui.ow_def);
    sw(out, cap, &len, "owdefcan=%d ", app.ui.ow_def_can);
    sw(out, cap, &len, "findopts=%s ", findopts);
    sw(out, cap, &len, "findbad=%d ", app.ui.find_m.bad);
    sw(out, cap, &len, "findselect=%d ", app.ui.find_select);
    sw(out, cap, &len, "findn=%td ", findn);
    sw(out, cap, &len, "findat=%td ", findat);
    sw(out, cap, &len, "findtext=%s ", findtext);
    sw(out, cap, &len, "search=%d ", app.ui.view == UI_VIEW_SEARCH);
    sw(out, cap, &len, "searchkeys=%d ", app.ui.focus == UI_FOCUS_SEARCH);
    sw(out, cap, &len, "searchtext=%s ", searchtext);
    sw(out, cap, &len, "searchdone=%d ", app.search_done);
    sw(out, cap, &len, "searchfull=%d ", app.search_s.full);
    sw(out, cap, &len, "searchdue=%d ", app.search_due != 0);
    sw(out, cap, &len, "searchbad=%d ", app.search_m.bad);
    sw(out, cap, &len, "searchbar=%d ", (int)Clay_GetElementData(CLAY_ID("search-bar")).found);
    sw(out, cap, &len, "searchsel=%d-%d ", app.ui.search.anchor < app.ui.search.caret ? app.ui.search.anchor : app.ui.search.caret, app.ui.search.anchor < app.ui.search.caret ? app.ui.search.caret : app.ui.search.anchor);
    sw(out, cap, &len, "searchcaret=%d ", app.ui.search.caret);
    sw(out, cap, &len, "findsel=%d-%d ", app.ui.find.anchor < app.ui.find.caret ? app.ui.find.anchor : app.ui.find.caret, app.ui.find.anchor < app.ui.find.caret ? app.ui.find.caret : app.ui.find.anchor);
    sw(out, cap, &len, "findcaret=%d ", app.ui.find.caret);
    sw(out, cap, &len, "seldir=%s ", seldir);
    sw(out, cap, &len, "searchinfo=%s ", searchinfo);
    sw(out, cap, &len, "inside=%d ", app.ui.search_inside);
    sw(out, cap, &len, "insidekeys=%d ", app.ui.focus == UI_FOCUS_INSIDE);
    sw(out, cap, &len, "insidetext=%s ", insidetext);
    sw(out, cap, &len, "insideopts=%s ", insideopts);
    sw(out, cap, &len, "insidebad=%d ", app.search_im.bad);
    sw(out, cap, &len, "insiderow=%d ", (int)Clay_GetElementData(CLAY_ID("inside-field")).found);
    sw(out, cap, &len, "hit=%s ", hit);
    sw(out, cap, &len, "hitmatch=%s ", hitmatch);
    sw(out, cap, &len, "hitdrawn=%d ", hitdrawn);
    sw(out, cap, &len, "datecol=%s ", VIEWS[app.ui.view].date_col);
    sw(out, cap, &len, "datecolw=%.0f ", date_w());
    sw(out, cap, &len, "owreads=%d ", ow_idb_reads);
    sw(out, cap, &len, "conf=%s ", confw);
    sw(out, cap, &len, "confbad=%d ", app.conf.bad);
    sw(out, cap, &len, "seldate=%s ", seldate);
    char selperms[16] = "-";
    if (se && !entry_unknown(se)) ui_perms(selperms, sizeof selperms, se->mode);
    sw(out, cap, &len, "selowner=%s selperms=%s ", se && !entry_unknown(se) ? id_name(false, se->uid) : "-", selperms);
    sw(out, cap, &len, "trashfull=%d ", app.trash_full);
    sw(out, cap, &len, "trashicon=%s ", trashicon);
    sw(out, cap, &len, "spawned=%d ", spawn_started);
    sw(out, cap, &len, "closingall=%d ", closing_all);
    sw(out, cap, &len, "closedtabs=%d ", closed_tabs.n);
    sw(out, cap, &len, "emptyn=%td ", app.empty_n);
    sw(out, cap, &len, "shiftsel=%s ", app.ui.shift_add ? "add" : "range");
    sw(out, cap, &len, "clearn=%lld ", clear.counted ? (long long)clear.sum.files : -1LL);
    sw(out, cap, &len, "clearing=%d ", clear.running);
    sw(out, cap, &len, "gitshown=%d ", git_shown() != NULL);
    sw(out, cap, &len, "gitsel=%s ", GIT_WORDS[app.have_list && app.list.n ? entry_git(app.ui.list.sel) : GS_NONE]);
    sw(out, cap, &len, "gitn=%td ", git_shown() ? git_shown()->g.changed : (isize)0);
    sw(out, cap, &len, "gitnamecol=%s ", gitnamecol);
    sw(out, cap, &len, "gitcells=%d ", gitcells);
    sw(out, cap, &len, "infogit=%s ", infogit);
    sw(out, cap, &len, "gitdue=%d ", git.due != 0);
    sw(out, cap, &len, "branch=%s ", branch);
    sw(out, cap, &len, "brn=%d ", app.ui.br_n);
    sw(out, cap, &len, "brhere=%d ", app.ui.br_here);
    sw(out, cap, &len, "brnote=%s ", brnote);
    sw(out, cap, &len, "brlist=%s ", brlist);
    sw(out, cap, &len, "brfits=%d ", brfits);
    sw(out, cap, &len, "brunder=%d ", brunder);
    sw(out, cap, &len, "gitwatch=%d ", atomic_load(&watch.git_wd) >= 0);
    sw(out, cap, &len, "brtotal=%d ", app.ui.br_total);
    sw(out, cap, &len, "brp=%d ", app.ui.panel == UI_PANEL_BRANCHES);
    sw(out, cap, &len, "brpn=%td ", app.ui.brp.list.n);
    sw(out, cap, &len, "brpsel=%s ", brpsel);
    sw(out, cap, &len, "brptext=%s ", brptext);
    sw(out, cap, &len, "brpinlist=%d ", app.ui.brp.in_list);
    sw(out, cap, &len, "shp=%d shpn=%td shpsel=%s shptext=%s shps=%s shppending=%d shpinlist=%d ", app.ui.panel == UI_PANEL_SHARES, app.ui.shp.list.n, shpsel, shptext, shplist, shp.pending, app.ui.shp.in_list);
    sw(out, cap, &len, "sidew=%d ", side_w());
    sw(out, cap, &len, "sidebox=%.0f ", Clay_GetElementData(CLAY_ID("side")).boundingBox.width);
    sw(out, cap, &len, "sidepx=%d ", app.side_px);
    if (len >= 0 && (size_t)len < cap) len += state_drag(out + len, cap - (size_t)len);
    // status= last: its text has spaces, so it takes the rest of the line.
    if (len >= 0 && (size_t)len < cap) len += snprintf(out + len, cap - (size_t)len, " statusbad=%d status=%s", app.status[0] && app.status_bad, app.status[0] ? app.status : "-");
    // (cut short, its last words, status= among them, would fail an expect that is right: F13 code-health review)
    if (len < 0 || (size_t)len >= cap) script_fail("the state line is %d bytes, more than its %zu", len, cap);
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

// A `key` word as the event a real key sends (ui/keys.c, as bound): ctrl-, shift- and alt- before a
// key's name (up, pageup, enter, esc, f1 ... f25, equal, minus, or the key's character: ctrl-t,
// alt-1), space a typed space, close-window the compositor's close. False for a word that names no
// key; a key that means nothing (Ctrl+Y, or a key ofx.conf took away) is an empty event.
static bool script_event(Str name, UiEvent *e)
{
    if (str_eq(name, S("close-window"))) return *e = (UiEvent){ UK_QUIT, 0, UK_NONE }, true;
    if (str_eq(name, S("space"))) return *e = ui_event_of((UiChord){ ' ', UI_MOD_TYPED }), true;
    u8 mods = 0;
    for (bool more = true; more;) {
        more = false;
        static const struct { const char *pre; u8 bit; } MODS[] = { { "ctrl-", UI_MOD_CTRL }, { "shift-", UI_MOD_SHIFT }, { "alt-", UI_MOD_ALT } };
        for (int i = 0; i < COUNT(MODS); i++)
            if (name.len > (isize)strlen(MODS[i].pre) && str_starts_with(name, str_from_c(MODS[i].pre)))
                mods |= MODS[i].bit, name = str_cut(name, (isize)strlen(MODS[i].pre), name.len), more = true;
    }
    static const struct { const char *name; u32 code; } KEYS[] = {
        { "up", UKC_UP }, { "down", UKC_DOWN }, { "left", UKC_LEFT }, { "right", UKC_RIGHT }, { "pageup", UKC_PGUP }, { "pagedown", UKC_PGDN },
        { "home", UKC_HOME }, { "end", UKC_END }, { "enter", UKC_ENTER }, { "backspace", UKC_BACKSPACE }, { "esc", UKC_ESC }, { "tab", UKC_TAB },
        { "delete", UKC_DELETE }, { "menu", UKC_MENU }, { "equal", '=' }, { "minus", '-' },
    };
    u32 code = 0;
    for (int i = 0; i < COUNT(KEYS); i++)
        if (str_eq(name, str_from_c(KEYS[i].name))) code = KEYS[i].code;
    char b[8];
    snprintf(b, sizeof b, "%.*s", STR_ARG(name));
    if (!code && b[0] == 'f' && b[1] >= '1' && b[1] <= '9' && atoi(b + 1) <= 25) code = UKC_F1 + (u32)atoi(b + 1) - 1;
    if (!code && name.len == 1 && name.p[0] > ' ' && name.p[0] < 0x7F) code = (u32)(name.p[0] >= 'A' && name.p[0] <= 'Z' ? name.p[0] - 'A' + 'a' : name.p[0]);
    if (!code) return false;
    *e = ui_event_of((UiChord){ code, mods });
    return true;
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
    // A far folder (F16) by its path as ofx holds it, not realpath's: that would read it, on the
    // main thread, and a share whose server the script stopped would hold the window.
    bool held = far_dir(path) && snprintf(p, sizeof p, "%s", path) < (int)sizeof p;
    if (!root || !root[0] || (!held && !realpath(path, p)) || !realpath(root, r)) return false;
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
    // OFX_DIR and OFX_ROOT in the child's environment only: setenv here would change environ while
    // the worker threads getenv (code-health review, 2026-11-06).
    enum { ENV_MAX = 1024 };
    static char *envp[ENV_MAX + 3];
    int ne = 0;
    for (char **e = environ; *e; e++) {
        if (strncmp(*e, "OFX_DIR=", 8) == 0 || strncmp(*e, "OFX_ROOT=", 9) == 0) continue;
        if (ne == ENV_MAX) {
            script_fail("sh: over %d environment variables", ENV_MAX);
            return;
        }
        envp[ne++] = *e;
    }
    envp[ne++] = arena_sprintf(&scratch, "OFX_DIR=%s", shown);
    envp[ne++] = arena_sprintf(&scratch, "OFX_ROOT=%s", root);
    envp[ne] = NULL;
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
    if (posix_spawnp(&pid, "timeout", NULL, &attr, argv, envp) == 0) {
        int st;
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) script_fail("sh failed: %.*s", STR_ARG(cmd));
    }
    posix_spawnattr_destroy(&attr);
    sigaction(SIGCHLD, &old, NULL);
}

// After a frame is drawn, before the swap: a pending shot.
// The frame was one an unscripted window draws too (dirty): what expect and wait-for read, so a
// change no frame asked to draw fails them (2026-11-05: the branch list that came alone, and the
// menu kept showing "Reading branches…" until the pointer moved).
static void script_drawn(void)
{
    script_state(script.drawn, sizeof script.drawn);
}

// What expect and wait-for read: the last drawn frame's state, or the state now while a frame is
// asked for and not drawn yet (a worker's answer, a hover after a frame); with the words no frame shows read as they are now: the window registry's count, the previews'
// texture bytes (dropping one not drawn changes nothing on screen, so it asks for no frame).
static void script_seen(char *out, size_t cap)
{
    static const char *const live_words[] = { "windows=", "thumbheld=" };
    if (script.pending) {
        script_state(out, cap);
        return;
    }
    static char live[16384];
    script_state(live, sizeof live);
    size_t n = 0;
    for (const char *p = script.drawn; *p && n + 1 < cap;) {
        const char *end = strchr(p, ' '), *from = p;
        size_t len = end ? (size_t)(end - p) : strlen(p);
        for (size_t k = 0; k < sizeof live_words / sizeof *live_words; k++) {
            size_t kl = strlen(live_words[k]);
            if (len < kl || strncmp(p, live_words[k], kl) != 0) continue;
            const char *l = strstr(live, live_words[k]);
            while (l && l != live && l[-1] != ' ') l = strstr(l + 1, live_words[k]);
            if (l) from = l, len = strcspn(l, " ");
        }
        n += (size_t)snprintf(out + n, cap - n, "%s%.*s", n ? " " : "", (int)len, from);
        if (n >= cap) n = cap - 1;
        p += end ? (size_t)(end - p) + 1 : strlen(p);
    }
    out[n] = 0;
}

static void script_after_draw(void)
{
    if (!script.shot[0]) return;
    Image img = LoadImageFromScreen();
    if (!ExportImage(img, script.shot)) script_fail("cannot write %s", script.shot);
    UnloadImage(img);
    script.shot[0] = 0;
}

// The script's last line, once: at its end, or when the window quits under it (the last tab's
// Ctrl+W). A window that quits with a command left, or a wait-for waiting, fails the run: what it
// never reached would otherwise pass unseen (a Close All that quit where f7b expects it to ask,
// 2026-11-04).
static void script_end(void)
{
    static bool done;
    if (done) return;
    done = true;
    if (script.waiting_for[0]) script_fail("the window quit during a wait-for, before the script's end");
    Str line;
    for (int n = script.line; !script.quits_ran && !script.failed && next_line(script.text, &script.at, &line);) {
        line = str_trim(line), n++;
        if (line.len && line.p[0] != '#') script.expects++, script_fail("the window quit before line %d, the script's end not reached", n);
    }
    printf("script: %d of %d expectations failed\n", script.failed, script.expects);
    fflush(stdout);
}

// The next frame's input: false at the end of the script. With OFX_GUI_FAIL_FAST (the GUI
// mutants) the first failure ends it: every wait-for after it would only run out its time.
static bool script_input(UiInput *in)
{
    script.gave = false;
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
        while (i < script.typing.len && in->nev < UI_EVENTS_MAX) in->ev[in->nev++] = ui_event_of((UiChord){ utf8_next(script.typing.p, script.typing.len, &i), UI_MOD_TYPED });
        script.typing = str_cut(script.typing, i, script.typing.len);
        script.gave = true;
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
            script_seen(state, sizeof state);
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
    if (script.quits_ran && script.quits-- > 0) return true;
    if (script.quits_ran) {
        script_fail("the window didn't quit");
        script_end();
        return false;
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
        int moved = -1;   // a pointer command's: whether it changed what is lit or dragged
        if (str_eq(cmd, S("settle"))) {
            script.settling = true, script.settle_n = 0;
        } else if (str_eq(cmd, S("wait"))) {
            script.wait = atoi(a);
        } else if (str_eq(cmd, S("sleep"))) {
            script.until = now_ms() + atoi(a);
        } else if (str_eq(cmd, S("key"))) {
            UiEvent e;
            if (!script_event(arg, &e)) script_fail("no key %s", a);
            else if (e.key || e.cmd || e.ch) in->ev[in->nev++] = e;   // (as on_key: a key that means nothing sends nothing)
        } else if (str_eq(cmd, S("keys"))) {
            for (isize at = 0; at < arg.len && in->nev < UI_EVENTS_MAX;) {
                while (at < arg.len && arg.p[at] == ' ') at++;
                isize e = at;
                while (e < arg.len && arg.p[e] != ' ') e++;
                Str w = str_cut(arg, at, e);
                at = e;
                if (w.len == 0) break;
                UiEvent ev;
                if (str_starts_with(w, S("char:")) && w.len == 6) in->ev[in->nev++] = ui_event_of((UiChord){ (u32)(unsigned char)w.p[5], UI_MOD_TYPED });
                else if (script_event(w, &ev)) {
                    if (ev.key || ev.cmd || ev.ch) in->ev[in->nev++] = ev;
                }
                else script_fail("keys: no key %.*s", STR_ARG(w));
            }
        } else if (str_eq(cmd, S("char")) || str_eq(cmd, S("type"))) {
            // The raw line for type: its text may start or end with spaces.
            Str text = str_eq(cmd, S("type")) ? str_cut(line, 5, line.len) : arg;
            if (str_eq(cmd, S("char")) && text.len != 1) script_fail("char takes one character");
            isize i = 0;
            while (i < text.len && in->nev < UI_EVENTS_MAX) in->ev[in->nev++] = ui_event_of((UiChord){ utf8_next(text.p, text.len, &i), UI_MOD_TYPED });
            script.typing = str_cut(text, i, text.len);   // the rest on the next frames
        } else if (str_eq(cmd, S("wheel"))) {
            if (strstr(a, "ctrl")) wheel_at(in, (float)GetScreenWidth() - 2, atoi(a), true);
            else if (strstr(a, "side")) wheel_at(in, 10, atoi(a), false);   // (over the sidebar's left edge)
            else wheel_at(in, (float)side_w() + 40, atoi(a), false);   // over the files, as the mouse's (a panel's list takes it, F7.5)
        } else if (str_eq(cmd, S("press"))) {
            int r = atoi(a);
            int mods = script_mods(a);
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
        } else if (str_eq(cmd, S("point-side"))) {   // as point-ui, the sidebar row shown named <name> (F13: the tree's rows move with the scratch path)
            int k = -1;
            for (int r = 0; r < app.ui.side.rows && app.ui.side.top + r < app.ui.side.n && k < 0; r++) {
                isize i = app.ui.side.top + r;
                Str name = app.ui.side_mode == SIDE_TREE ? (i < tree.n ? tree.row[i].name : STR_EMPTY) : (i < app.places.n ? str_from_c(app.places.p[i].label) : STR_EMPTY);
                if (str_eq(name, str_from_c(a))) k = r;
            }
            Clay_ElementData d = { 0 };
            if (k >= 0) d = Clay_GetElementData(CLAY_IDI("side-row", k));
            if (!d.found) script_fail("point-side: no row %s shown", a);
            script.px = d.boundingBox.x + d.boundingBox.width / 2, script.py = d.boundingBox.y + d.boundingBox.height / 2;
            printf("point: %.0f %.0f\n", script.px, script.py);
        } else if (str_eq(cmd, S("hover-here"))) {
            script.pointing = true;
            moved = hover_at(script.px, script.py);
        } else if (str_eq(cmd, S("pointer-off"))) {
            script.pointing = false;
            moved = hover_refresh(false, script.px, script.py);   // GLFW keeps the last position
        } else if (str_eq(cmd, S("user-keys"))) {
            script.user_keys = true;
        } else if (str_eq(cmd, S("user-pointer"))) {
            script.user_pointer = user_pointer = true;   // (input_real's hover and drags as a user's)
        } else if (str_eq(cmd, S("clip-drop"))) {
            if (!clip.dpy || shutdown(wl_display_get_fd(clip.dpy), SHUT_RDWR) != 0) script_fail("clip-drop: no clipboard connection to cut");
        } else if (str_eq(cmd, S("idle"))) {
            script_idle();
        } else if (str_eq(cmd, S("click-here"))) {   // (pressed and let go: a drag it starts ends at once)
            press_at(in, script.px, script.py, 0);
            drag_at(in, script.px, script.py, false);
        } else if (str_eq(cmd, S("drag-here"))) {
            press_at(in, script.px, script.py, 0);
            drag_at(in, script.px + (float)atoi(a), script.py, true);
            drag_at(in, script.px + (float)atoi(a), script.py, false);
        } else if (str_eq(cmd, S("press-here"))) {   // (F13) held across frames, until release
            press_at(in, script.px, script.py, 0);
            script.held = true;
        } else if (str_eq(cmd, S("move-to")) || str_eq(cmd, S("move-here")) || str_eq(cmd, S("move-by"))) {
            float x = script.px, y = script.py, dx = 0, dy = 0;
            if (str_eq(cmd, S("move-to")) && sscanf(a, "%f %f", &x, &y) != 2) script_fail("move-to <x> <y>");
            if (str_eq(cmd, S("move-by")) && sscanf(a, "%f %f", &dx, &dy) != 2) script_fail("move-by <dx> <dy>");
            x += dx, y += dy;
            app.drag_mods = script_mods(a);
            script.px = x, script.py = y, script.pointing = true;
            moved = script.held && drag_at(in, x, y, true);
            moved |= hover_at(x, y);
        } else if (str_eq(cmd, S("release"))) {
            app.drag_mods = script_mods(a);
            drag_at(in, script.px, script.py, false);
            script.held = false;
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
            Err err;
            if (a[0] != '/' && dir && dir[0]) snprintf(script.shot, sizeof script.shot, "%s/%s", dir, a);
            else snprintf(script.shot, sizeof script.shot, "%s", a);
            // (through the test guard as ofx's own writes: an absolute path outside OFX_GUI_ROOT is
            // refused, the F15 code-health review)
            if (!ops_allowed(script.shot, &err)) script_fail("shot: %s", err.msg), script.shot[0] = 0;
            script.wait = 1;   // drawn on the next frame
        } else if (str_eq(cmd, S("sh"))) {
            script_sh(arg);
        } else if (str_eq(cmd, S("state"))) {
            script_state(state, sizeof state);
            printf("state: %s\n", state);
        } else if (str_eq(cmd, S("expect"))) {
            script.expects++;
            script_seen(state, sizeof state);
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
        } else if (str_eq(cmd, S("quits"))) {
            script.expects++;
            script.quits = atoi(a);
            if (script.quits < 1) script_fail("quits: a frame count of 1 or more, not '%s'", a);
        } else if (str_eq(cmd, S("quit"))) {
            script.at = script.text.len;   // what follows it is left on purpose
            break;
        } else {
            script_fail("unknown command %.*s", STR_ARG(cmd));
        }
        if (script.quits && !str_eq(cmd, S("quits"))) script.quits_ran = true;
        // What gives the window input, as for a user: not a wait, a check, a point computed, another
        // program's copy; a pointer move only when it changes what is lit or dragged, as
        // input_real's (review, 2026-11-05).
        static const Str passive[] = { S("settle"), S("wait"), S("sleep"), S("state"), S("expect"), S("wait-for"), S("quits"), S("shot"), S("sh"), S("idle"),
                                       S("point-ui"), S("point-menu"), S("point-side"), S("user-keys"), S("user-pointer"), S("clip-text"), S("clip-drop") };
        script.gave = true;
        for (size_t k = 0; k < sizeof passive / sizeof *passive; k++) script.gave &= !str_eq(cmd, passive[k]);
        if (moved >= 0) script.gave = moved;
        fflush(stdout);
        return true;
    }
    script_end();
    return false;
}
