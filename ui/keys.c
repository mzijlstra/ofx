// The keys (F15, docs/plan.md "F15 design"): one table behind the keys, the menus' key labels and
// the Keyboard Shortcuts panel. A key is a chord (a key and its modifiers, as gui/ reads GLFW's key
// callback) or a typed character (GLFW's character callback: the layout and Shift are the
// compositor's, `G`, `~`). Each event carries two meanings: the key itself, which the text fields,
// panels, menus and questions read (their keys stay fixed), and the command bound to it, which the
// files and the sidebar read (and a field passes on). Included by ui.c.

// ---- chords -----------------------------------------------------------------------------------

// The keys without a character of their own; a key with one is that character, letters lower case
// ('t' for the T key, '=' for the = key).
enum {
    UKC_ESC = 0x100, UKC_ENTER, UKC_TAB, UKC_BACKSPACE, UKC_INSERT, UKC_DELETE, UKC_RIGHT, UKC_LEFT, UKC_DOWN, UKC_UP,
    UKC_PGUP, UKC_PGDN, UKC_HOME, UKC_END, UKC_MENU,
    UKC_KP0, UKC_KP9 = UKC_KP0 + 9, UKC_KP_DECIMAL, UKC_KP_DIVIDE, UKC_KP_MULTIPLY, UKC_KP_SUBTRACT, UKC_KP_ADD, UKC_KP_EQUAL,
    UKC_F1, UKC_F25 = UKC_F1 + 24,
};

// A key and its modifiers (UI_MOD_*), or with UI_MOD_TYPED a typed character (code its code point).
typedef struct {
    u32 code;
    u8  mods;
} UiChord;

static const char *const UI_KEYCODE_NAME[] = {
    [UKC_ESC - UKC_ESC] = "Esc", [UKC_ENTER - UKC_ESC] = "Enter", [UKC_TAB - UKC_ESC] = "Tab", [UKC_BACKSPACE - UKC_ESC] = "Backspace",
    [UKC_INSERT - UKC_ESC] = "Insert", [UKC_DELETE - UKC_ESC] = "Delete", [UKC_RIGHT - UKC_ESC] = "Right", [UKC_LEFT - UKC_ESC] = "Left",
    [UKC_DOWN - UKC_ESC] = "Down", [UKC_UP - UKC_ESC] = "Up", [UKC_PGUP - UKC_ESC] = "PgUp", [UKC_PGDN - UKC_ESC] = "PgDn",
    [UKC_HOME - UKC_ESC] = "Home", [UKC_END - UKC_ESC] = "End", [UKC_MENU - UKC_ESC] = "Menu",
    [UKC_KP_DECIMAL - UKC_ESC] = "KP.", [UKC_KP_DIVIDE - UKC_ESC] = "KP/", [UKC_KP_MULTIPLY - UKC_ESC] = "KP*",
    [UKC_KP_SUBTRACT - UKC_ESC] = "KP-", [UKC_KP_ADD - UKC_ESC] = "KP+", [UKC_KP_EQUAL - UKC_ESC] = "KP=",
};

enum { UI_KEY_NAME_MAX = 40 };

// A chord's name as the menus write it: Ctrl+Shift+N, Alt+Left, F2, KP1; a typed character as
// itself (t, G, ~), a space as Space.
static void ui_chord_name(UiChord c, char *out, int cap)
{
    int n = 0;
    out[0] = 0;
    if (c.mods & UI_MOD_TYPED) {
        if (c.code == ' ') snprintf(out, (size_t)cap, "Space");
        else {
            char b[4];
            int len = ui_utf8(c.code, b);
            snprintf(out, (size_t)cap, "%.*s", len, b);
        }
        return;
    }
    static const struct { u8 bit; const char *name; } MODS[] = { { UI_MOD_CTRL, "Ctrl+" }, { UI_MOD_ALT, "Alt+" }, { UI_MOD_SHIFT, "Shift+" }, { UI_MOD_SUPER, "Super+" } };
    for (int i = 0; i < COUNT(MODS); i++)
        if (c.mods & MODS[i].bit) n += snprintf(out + n, (size_t)(cap - n), "%s", MODS[i].name);
    if (c.code >= UKC_F1 && c.code <= UKC_F25) snprintf(out + n, (size_t)(cap - n), "F%u", c.code - UKC_F1 + 1);
    else if (c.code >= UKC_KP0 && c.code <= UKC_KP9) snprintf(out + n, (size_t)(cap - n), "KP%u", c.code - UKC_KP0);
    else if (c.code >= UKC_ESC && c.code - UKC_ESC < (u32)COUNT(UI_KEYCODE_NAME) && UI_KEYCODE_NAME[c.code - UKC_ESC]) snprintf(out + n, (size_t)(cap - n), "%s", UI_KEYCODE_NAME[c.code - UKC_ESC]);
    else if (c.code == ' ') snprintf(out + n, (size_t)(cap - n), "Space");
    else if (c.code > ' ' && c.code < 0x7F) snprintf(out + n, (size_t)(cap - n), "%c", c.code >= 'a' && c.code <= 'z' ? (char)(c.code - 'a' + 'A') : (char)c.code);
    else snprintf(out + n, (size_t)(cap - n), "?");
}

static bool ui_chord_eq(UiChord a, UiChord b)
{
    return a.code == b.code && a.mods == b.mods;
}

// ---- the keys' own meanings ---------------------------------------------------------------------

// What a key is in a text field, a panel, a menu or a question: fixed, whatever it is bound to.
// Alt and a letter is UK_MENU and Alt and a digit UK_TAB_JUMP, the letter or digit in ch.
#define K(c, m) { (c), (m) }
static const struct { UiChord c; UiKey key; } UI_FIXED[] = {
    { K(UKC_UP, 0), UK_UP }, { K(UKC_DOWN, 0), UK_DOWN }, { K(UKC_LEFT, 0), UK_LEFT }, { K(UKC_RIGHT, 0), UK_RIGHT },
    { K(UKC_HOME, 0), UK_HOME }, { K(UKC_END, 0), UK_END },
    { K(UKC_PGUP, 0), UK_PAGEUP }, { K(UKC_PGDN, 0), UK_PAGEDOWN }, { K(UKC_PGUP, UI_MOD_SHIFT), UK_PAGEUP }, { K(UKC_PGDN, UI_MOD_SHIFT), UK_PAGEDOWN },
    { K(UKC_ENTER, 0), UK_ENTER }, { K(UKC_ENTER, UI_MOD_SHIFT), UK_SHIFT_ENTER },
    { K(UKC_ESC, 0), UK_ESC }, { K(UKC_ESC, UI_MOD_SHIFT), UK_ESC },
    { K(UKC_TAB, 0), UK_TAB }, { K(UKC_TAB, UI_MOD_SHIFT), UK_TAB },   // (Shift+Tab: back, where there are two places, F7.3)
    { K(UKC_BACKSPACE, 0), UK_BACKSPACE }, { K(UKC_BACKSPACE, UI_MOD_SHIFT), UK_BACKSPACE }, { K(UKC_DELETE, 0), UK_DELETE },
    { K(UKC_LEFT, UI_MOD_SHIFT), UK_SEL_LEFT }, { K(UKC_RIGHT, UI_MOD_SHIFT), UK_SEL_RIGHT }, { K(UKC_HOME, UI_MOD_SHIFT), UK_SEL_HOME },
    { K(UKC_END, UI_MOD_SHIFT), UK_SEL_END }, { K(UKC_UP, UI_MOD_SHIFT), UK_SEL_UP }, { K(UKC_DOWN, UI_MOD_SHIFT), UK_SEL_DOWN },
    // A word at a time in a text field (Michael, 2026-10-28)
    { K(UKC_LEFT, UI_MOD_CTRL), UK_WORD_LEFT }, { K(UKC_RIGHT, UI_MOD_CTRL), UK_WORD_RIGHT },
    { K(UKC_LEFT, UI_MOD_CTRL | UI_MOD_SHIFT), UK_SEL_WORD_LEFT }, { K(UKC_RIGHT, UI_MOD_CTRL | UI_MOD_SHIFT), UK_SEL_WORD_RIGHT },
    { K(UKC_BACKSPACE, UI_MOD_CTRL), UK_WORD_BACKSPACE }, { K(UKC_DELETE, UI_MOD_CTRL), UK_WORD_DELETE },
    { K('a', UI_MOD_CTRL), UK_ALL }, { K('c', UI_MOD_CTRL), UK_COPY }, { K('x', UI_MOD_CTRL), UK_CUT }, { K('v', UI_MOD_CTRL), UK_PASTE },
    // Edit Bookmarks' own: F2 renames, Alt+Up and Alt+Down move one
    { K(UKC_F1 + 1, 0), UK_F2 }, { K(UKC_UP, UI_MOD_ALT), UK_PARENT }, { K(UKC_DOWN, UI_MOD_ALT), UK_ALT_DOWN },
};

static UiKey ui_fixed_of(UiChord c, u32 *ch)
{
    *ch = 0;
    if (c.mods == UI_MOD_ALT && c.code >= 'a' && c.code <= 'z') return *ch = c.code, UK_MENU;
    if (c.mods == UI_MOD_ALT && c.code >= '1' && c.code <= '9') return *ch = c.code - '0', UK_TAB_JUMP;
    for (int i = 0; i < COUNT(UI_FIXED); i++)
        if (ui_chord_eq(UI_FIXED[i].c, c)) return UI_FIXED[i].key;
    return UK_NONE;
}

// ---- the commands -------------------------------------------------------------------------------

// Every command a key can be bound to, by its name in ofx.conf (the menus' entries' names where it
// is one). The command is the event ui/ reads in the files and the sidebar.
static const struct { UiKey cmd; const char *name; } UI_CMDS[] = {
    { UK_UP, "up" }, { UK_DOWN, "down" }, { UK_LEFT, "left" }, { UK_RIGHT, "right" },
    { UK_PAGEUP, "page-up" }, { UK_PAGEDOWN, "page-down" }, { UK_HOME, "first" }, { UK_END, "last" },
    { UK_SEL_UP, "select-up" }, { UK_SEL_DOWN, "select-down" }, { UK_SEL_LEFT, "select-left" }, { UK_SEL_RIGHT, "select-right" },
    { UK_SEL_HOME, "select-first" }, { UK_SEL_END, "select-last" }, { UK_MARK, "mark" },
    { UK_BACK, "back" }, { UK_FORWARD, "forward" }, { UK_PARENT, "parent" }, { UK_HOMEDIR, "home" }, { UK_TRASH_VIEW, "trash-view" },
    { UK_LOCATION, "location" },
    { UK_ENTER, "open" }, { UK_SHIFT_ENTER, "open-with" }, { UK_TERMINAL, "terminal" }, { UK_PROPERTIES, "properties" },
    { UK_ALL, "select-all" }, { UK_SELECT_MATCH, "select-match" }, { UK_INVERT, "invert" }, { UK_ESC, "unselect" },
    { UK_CUT, "cut" }, { UK_COPY, "copy" }, { UK_COPY_PATH, "copy-path" }, { UK_PASTE, "paste" }, { UK_UNDO, "undo" },
    { UK_MAKE_LINK, "make-link" }, { UK_F2, "rename" }, { UK_NEWFOLDER, "new-folder" }, { UK_DELETE, "trash" },
    { UK_SHIFT_DELETE, "delete" }, { UK_RESTORE, "restore" }, { UK_ADD_BOOKMARK, "add-bookmark" }, { UK_EDIT_BOOKMARKS, "edit-bookmarks" },
    { UK_TAB_NEW, "new-tab" }, { UK_TAB_REOPEN, "reopen-tab" }, { UK_NEW_WINDOW, "new-window" }, { UK_TAB_CLOSE, "close" },
    { UK_TAB_NEXT, "next-tab" }, { UK_TAB_PREV, "previous-tab" }, { UK_TAB_LEFT, "tab-left" }, { UK_TAB_RIGHT, "tab-right" },
    { UK_QUIT_ALL, "close-all" },
    { UK_F10, "menus" }, { UK_CONTEXT, "context-menu" }, { UK_SHORTCUTS, "shortcuts" }, { UK_RELOAD, "reload" }, { UK_HIDDEN, "hidden" },
    { UK_PERMS_COLS, "perms-cols" }, { UK_GROUP_COL, "group-col" }, { UK_SORT_NEXT, "sort-next" }, { UK_SORT_REVERSE, "sort-reverse" },
    { UK_VIEW_LIST, "view-list" }, { UK_VIEW_ICONS, "view-icons" }, { UK_ZOOM_IN, "zoom-in" }, { UK_ZOOM_OUT, "zoom-out" },
    { UK_ZOOM_RESET, "zoom-reset" },
    { UK_TAB, "sidebar" }, { UK_SIDE_GO, "to-sidebar" }, { UK_F9, "sidebar-mode" }, { UK_CRUMBS, "path-buttons" }, { UK_JOBS, "job-rows" },
    { UK_FIND, "find" }, { UK_FIND_NEXT, "find-next" }, { UK_FIND_PREV, "find-previous" },
    { UK_SEARCH, "search" }, { UK_CONTAINING, "containing" }, { UK_OPEN_LINE, "open-line" },
};

// ---- the bindings -------------------------------------------------------------------------------

// A key bound to a command.
typedef struct {
    UiChord c;
    UiKey   cmd;
} UiBind;

#define T(ch) { (ch), UI_MOD_TYPED }
#define C_ UI_MOD_CTRL
#define S_ UI_MOD_SHIFT
#define A_ UI_MOD_ALT
// The keys as ofx has them (Nemo's where it has one, docs/plan.md from F1 on); a command's first
// key is the one its menu entry shows.
static const UiBind UI_BINDS_DEFAULT[] = {
    { K(UKC_UP, 0), UK_UP }, { T('k'), UK_UP }, { K(UKC_DOWN, 0), UK_DOWN }, { T('j'), UK_DOWN },
    { K(UKC_LEFT, 0), UK_LEFT }, { T('h'), UK_LEFT }, { K(UKC_RIGHT, 0), UK_RIGHT }, { T('l'), UK_RIGHT },
    { K(UKC_PGUP, 0), UK_PAGEUP }, { K(UKC_PGDN, 0), UK_PAGEDOWN },
    { K(UKC_HOME, 0), UK_HOME }, { T('g'), UK_HOME }, { K(UKC_END, 0), UK_END }, { T('G'), UK_END },
    { K(UKC_UP, S_), UK_SEL_UP }, { T('K'), UK_SEL_UP }, { K(UKC_DOWN, S_), UK_SEL_DOWN }, { T('J'), UK_SEL_DOWN },
    { K(UKC_LEFT, S_), UK_SEL_LEFT }, { T('H'), UK_SEL_LEFT }, { K(UKC_RIGHT, S_), UK_SEL_RIGHT }, { T('L'), UK_SEL_RIGHT },
    { K(UKC_HOME, S_), UK_SEL_HOME }, { K(UKC_END, S_), UK_SEL_END }, { T(' '), UK_MARK },
    { K(UKC_LEFT, A_), UK_BACK }, { K(UKC_RIGHT, A_), UK_FORWARD },
    { K(UKC_UP, A_), UK_PARENT }, { K(UKC_BACKSPACE, 0), UK_PARENT },
    { K(UKC_HOME, A_), UK_HOMEDIR }, { T('~'), UK_HOMEDIR },   // (F7.4, as Nemo)
    { K(UKC_END, A_), UK_TRASH_VIEW },   // (Michael, 2026-10-27)
    { K('l', C_), UK_LOCATION }, { K(UKC_RIGHT, C_), UK_LOCATION },   // (Ctrl and an arrow: that part of the window, Michael, 2026-10-24)
    { K(UKC_ENTER, 0), UK_ENTER }, { K(UKC_ENTER, S_), UK_SHIFT_ENTER },
    { K(UKC_ENTER, A_), UK_TERMINAL }, { T('t'), UK_TERMINAL },   // (Michael, 2026-10-25)
    { K('i', C_), UK_PROPERTIES },
    { K('a', C_), UK_ALL }, { K('a', C_ | S_), UK_SELECT_MATCH }, { K('i', C_ | S_), UK_INVERT }, { K(UKC_ESC, 0), UK_ESC },
    { K('x', C_), UK_CUT }, { K('c', C_), UK_COPY }, { K('c', C_ | S_), UK_COPY_PATH }, { K('v', C_), UK_PASTE }, { K('z', C_), UK_UNDO },
    { K('m', C_), UK_MAKE_LINK },   // (F8, as Nemo)
    { K(UKC_F1 + 1, 0), UK_F2 }, { K('n', C_ | S_), UK_NEWFOLDER }, { K(UKC_DELETE, 0), UK_DELETE }, { K(UKC_DELETE, S_), UK_SHIFT_DELETE },
    { T('r'), UK_RESTORE }, { K('d', C_), UK_ADD_BOOKMARK }, { K('b', C_), UK_EDIT_BOOKMARKS },   // (F7.5, as Nemo)
    { K('t', C_), UK_TAB_NEW }, { K('t', C_ | S_), UK_TAB_REOPEN }, { K('n', C_), UK_NEW_WINDOW }, { K('w', C_), UK_TAB_CLOSE },
    { K(UKC_TAB, C_), UK_TAB_NEXT }, { K(UKC_PGDN, C_), UK_TAB_NEXT }, { K(UKC_TAB, C_ | S_), UK_TAB_PREV }, { K(UKC_PGUP, C_), UK_TAB_PREV },
    { K(UKC_PGUP, C_ | S_), UK_TAB_LEFT }, { K(UKC_PGDN, C_ | S_), UK_TAB_RIGHT }, { K('q', C_), UK_QUIT_ALL },
    { K(UKC_F1 + 9, 0), UK_F10 }, { K(UKC_F1 + 9, S_), UK_CONTEXT }, { K(UKC_MENU, 0), UK_CONTEXT },   // (F8, as GTK)
    { K(UKC_F1, 0), UK_SHORTCUTS }, { K(UKC_F1, C_), UK_SHORTCUTS },   // (F7.5: F1 and Ctrl+F1, Nemo's two)
    { K('r', C_), UK_RELOAD }, { K('h', C_), UK_HIDDEN }, { T('.'), UK_HIDDEN },
    { K('p', C_), UK_PERMS_COLS }, { K('g', C_), UK_GROUP_COL },   // (Michael, 2026-10-27: no printing in ofx)
    { T('s'), UK_SORT_NEXT }, { T('S'), UK_SORT_REVERSE },
    // The views in the top bar's order, list first (Michael, 2026-10-24); the icon view's sizes as
    // Nemo's (F6.3): Ctrl+= or Ctrl++ (Shift+= on most layouts), Ctrl+-, Ctrl+0, and the keypad's.
    { K('1', C_), UK_VIEW_LIST }, { K(UKC_KP0 + 1, C_), UK_VIEW_LIST }, { K('2', C_), UK_VIEW_ICONS }, { K(UKC_KP0 + 2, C_), UK_VIEW_ICONS },
    { K('=', C_), UK_ZOOM_IN }, { K('=', C_ | S_), UK_ZOOM_IN }, { K(UKC_KP_ADD, C_), UK_ZOOM_IN }, { K(UKC_KP_ADD, C_ | S_), UK_ZOOM_IN },
    { K('-', C_), UK_ZOOM_OUT }, { K(UKC_KP_SUBTRACT, C_), UK_ZOOM_OUT }, { K('0', C_), UK_ZOOM_RESET }, { K(UKC_KP0, C_), UK_ZOOM_RESET },
    { K(UKC_TAB, 0), UK_TAB }, { K(UKC_LEFT, C_), UK_SIDE_GO }, { K(UKC_F1 + 8, 0), UK_F9 }, { K('s', C_), UK_F9 },
    { K('k', C_), UK_CRUMBS }, { K(UKC_UP, C_), UK_CRUMBS }, { K('j', C_), UK_JOBS }, { K(UKC_DOWN, C_), UK_JOBS },
    { K('f', C_), UK_FIND }, { T('/'), UK_FIND }, { T('n'), UK_FIND_NEXT }, { T('N'), UK_FIND_PREV },
    { K('f', C_ | S_), UK_SEARCH }, { K(UKC_ENTER, C_), UK_CONTAINING }, { K(UKC_ENTER, C_ | S_), UK_OPEN_LINE },   // (F9)
};
#undef A_
#undef S_
#undef C_
#undef T
#undef K

enum { UI_BINDS_MAX = 512 };

// The bindings in use, and each one's name as the menus write it.
static struct {
    UiBind b[UI_BINDS_MAX];
    char   name[UI_BINDS_MAX][UI_KEY_NAME_MAX];
    int    n;
} ui_binds;

static void ui_binds_reset(void)
{
    ui_binds.n = 0;
    for (int i = 0; i < COUNT(UI_BINDS_DEFAULT); i++, ui_binds.n++) {
        ui_binds.b[i] = UI_BINDS_DEFAULT[i];
        ui_chord_name(UI_BINDS_DEFAULT[i].c, ui_binds.name[i], UI_KEY_NAME_MAX);
    }
}

static void ui_binds_ready(void)
{
    if (!ui_binds.n) ui_binds_reset();
}

static UiKey ui_bound(UiChord c)
{
    ui_binds_ready();
    for (int i = 0; i < ui_binds.n; i++)
        if (ui_chord_eq(ui_binds.b[i].c, c)) return ui_binds.b[i].cmd;
    return UK_NONE;
}

// The name of command cmd's k-th key (0 the first), "" past its last.
static const char *ui_cmd_key(UiKey cmd, int k)
{
    ui_binds_ready();
    for (int i = 0; i < ui_binds.n; i++)
        if (ui_binds.b[i].cmd == cmd && k-- == 0) return ui_binds.name[i];
    return "";
}

// ---- events -------------------------------------------------------------------------------------

// What a chord or a typed character sends: the key itself (UK_CMD when it has no meaning of its own
// but a command) and its command. Shift with Esc, Tab, Backspace, PgUp or PgDn is the key without
// it, command and all, unless Shift and the key are bound themselves.
static UiEvent ui_event_of(UiChord c)
{
    if (c.mods & UI_MOD_TYPED) return (UiEvent){ UK_NONE, c.code, ui_bound(c) };
    u32 ch;
    UiKey key = ui_fixed_of(c, &ch);
    UiEvent e = { key, ch, ui_bound(c) };
    if (e.key == UK_TAB_JUMP) e.cmd = UK_TAB_JUMP;
    UiChord plain = { c.code, (u8)(c.mods & ~UI_MOD_SHIFT) };
    if (!e.cmd && c.mods != plain.mods && e.key && e.key == ui_fixed_of(plain, &ch)) e.cmd = ui_bound(plain);
    if (!e.key && e.cmd) e.key = UK_CMD;
    return e;
}

// What command cmd's menu entry runs when picked: what its first key as ofx has it sends (a text
// field takes Copy as Ctrl+C), or the command alone.
static UiEvent ui_cmd_event(UiKey cmd)
{
    for (int i = 0; i < COUNT(UI_BINDS_DEFAULT); i++)
        if (UI_BINDS_DEFAULT[i].cmd == cmd) return ui_event_of(UI_BINDS_DEFAULT[i].c);
    return (UiEvent){ UK_CMD, 0, cmd };
}

// ---- the menus and the panel ---------------------------------------------------------------------

// What menu entry it shows where a key goes: its command's first key, or its hint ("" neither).
static const char *ui_menu_key(UiMenuItem it)
{
    if (UI_MENU_ITEM[it].hint) return UI_MENU_ITEM[it].hint;
    return UI_MENU_ITEM[it].cmd ? ui_cmd_key(UI_MENU_ITEM[it].cmd, 0) : "";
}

// What picking menu entry it runs: its command's event, or none (its own action, ui_menu_pick).
static UiEvent ui_menu_runs(UiMenuItem it)
{
    return UI_MENU_ITEM[it].runs ? ui_cmd_event(UI_MENU_ITEM[it].cmd) : (UiEvent){ UK_NONE, 0, UK_NONE };
}

// Keyboard Shortcuts' row r: its key k (0 the first, 1 the second; "" none).
static const char *ui_keys_key(int r, int k)
{
    const char *row = k ? UI_KEYS[r].key2 : UI_KEYS[r].key;
    return row || !UI_KEYS[r].cmd ? row : ui_cmd_key(UI_KEYS[r].cmd, k);
}
