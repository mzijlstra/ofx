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
    // The bar menus' entries with no key as ofx has them (F15 step 2): each runs as picked.
    { UK_ITEM0 + UM_NEW_FILE, "new-file" }, { UK_ITEM0 + UM_DUPLICATE, "duplicate" }, { UK_ITEM0 + UM_PREFERENCES, "preferences" },
    { UK_ITEM0 + UM_SIDE_PLACES, "side-places" }, { UK_ITEM0 + UM_SIDE_TREE, "side-tree" }, { UK_ITEM0 + UM_SIDE_NONE, "side-none" },
    { UK_ITEM0 + UM_COLORS, "colors" }, { UK_ITEM0 + UM_SHOW_MENU, "show-menu" }, { UK_ITEM0 + UM_SORT_NAME, "sort-name" },
    { UK_ITEM0 + UM_SORT_SIZE, "sort-size" }, { UK_ITEM0 + UM_SORT_MTIME, "sort-mtime" }, { UK_ITEM0 + UM_CLEAR_PREVIEWS, "clear-previews" },
    { UK_ITEM0 + UM_ABOUT, "about" },
    // and a right-click menu's that needs nothing clicked: the tab menu's, on the current tab
    // (Michael, 2026-11-08)
    { UK_ITEM0 + UM_TAB_OTHERS, "tab-others" },
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
    { K('w', C_ | S_), UK_ITEM0 + UM_TAB_OTHERS },   // (Close Other Tabs: Ctrl+W's others, Michael, 2026-11-08)
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

enum { UI_BINDS_MAX = COUNT(UI_BINDS_DEFAULT) + CONF_KEYS_MAX };   // (ofx.conf's lines at most, and the defaults)

// A binding in use: whether it is from ofx.conf, and then its line there; its name as the menus
// write it.
typedef struct {
    UiBind b;
    bool   user;
    int    line;
    char   name[UI_KEY_NAME_MAX];
} UiBinding;

// The bindings in use: ofx.conf's key lines first, in their order, then the defaults they left.
static struct {
    UiBinding e[UI_BINDS_MAX];
    int       n;
    bool      ready;
} ui_binds;

static void ui_binds_reset(void)
{
    ui_binds.n = 0, ui_binds.ready = true;
    for (int i = 0; i < COUNT(UI_BINDS_DEFAULT); i++, ui_binds.n++) {
        ui_binds.e[i] = (UiBinding){ .b = UI_BINDS_DEFAULT[i] };
        ui_chord_name(UI_BINDS_DEFAULT[i].c, ui_binds.e[i].name, UI_KEY_NAME_MAX);
    }
}

static void ui_binds_ready(void)
{
    if (!ui_binds.ready) ui_binds_reset();
}

static UiKey ui_bound(UiChord c)
{
    ui_binds_ready();
    for (int i = 0; i < ui_binds.n; i++)
        if (ui_chord_eq(ui_binds.e[i].b.c, c)) return ui_binds.e[i].b.cmd;
    return UK_NONE;
}

// Command cmd's k-th binding (0 the first), -1 past its last.
static int ui_cmd_bind(UiKey cmd, int k)
{
    ui_binds_ready();
    for (int i = 0; i < ui_binds.n; i++)
        if (ui_binds.e[i].b.cmd == cmd && k-- == 0) return i;
    return -1;
}

// The name of command cmd's k-th key (0 the first), "" past its last.
static const char *ui_cmd_key(UiKey cmd, int k)
{
    int i = ui_cmd_bind(cmd, k);
    return i < 0 ? "" : ui_binds.e[i].name;
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

// ---- ofx.conf's key lines (F15 step 2) -------------------------------------------------------------

// a and b the same, ASCII letters in either case.
static bool ui_same_nocase(Str a, const char *b)
{
    isize n = (isize)strlen(b);
    if (a.len != n) return false;
    for (isize i = 0; i < n; i++) {
        char x = a.p[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

// A key as the menus write it (ui_chord_name), into *c: modifiers (Ctrl+, Alt+, Shift+, Super+, in
// any order and case) and a key's name (Enter, PgUp, F2, KP1, Space, any case) or its character
// (Ctrl+T, Ctrl+=, Ctrl++); with no modifier, a character is typed (t, G, ~, é) and Space a typed
// space. False, with why, for what isn't one, or is one ofx keeps fixed.
static bool ui_chord_parse(Str s, UiChord *c, char *why, int cap)
{
    static const struct { const char *pre; u8 bit; } MODS[] = { { "ctrl+", UI_MOD_CTRL }, { "alt+", UI_MOD_ALT }, { "shift+", UI_MOD_SHIFT }, { "super+", UI_MOD_SUPER } };
    u8 mods = 0;
    for (bool more = true; more;) {
        more = false;
        for (int i = 0; i < COUNT(MODS); i++) {
            isize n = (isize)strlen(MODS[i].pre);
            if (s.len > n && ui_same_nocase(str_cut(s, 0, n), MODS[i].pre)) mods |= MODS[i].bit, s = str_cut(s, n, s.len), more = true;
        }
    }
    u32 code = 0;
    char b[UI_KEY_NAME_MAX];
    for (u32 k = UKC_ESC; k <= UKC_F25 && !code; k++) {
        ui_chord_name((UiChord){ k, 0 }, b, (int)sizeof b);
        if (b[0] != '?' && ui_same_nocase(s, b)) code = k;
    }
    if (!code && ui_same_nocase(s, "Space")) code = ' ';
    isize at = 0;
    u32 cp = s.len ? utf8_next(s.p, s.len, &at) : 0;
    if (!code && s.len && at == s.len && cp > ' ' && cp != 0x7F) {
        if (!mods) return *c = (UiChord){ cp, UI_MOD_TYPED }, true;
        if (cp >= 0x7F) return snprintf(why, (size_t)cap, "a key with Ctrl, Alt or Super is one of a US keyboard's"), false;
        if (mods == UI_MOD_SHIFT) return snprintf(why, (size_t)cap, "Shift and a character: write the character it types (G, ~)"), false;
        code = cp >= 'A' && cp <= 'Z' ? cp - 'A' + 'a' : cp;
    }
    if (!code) return snprintf(why, (size_t)cap, "no key %.*s", (int)(s.len < 40 ? s.len : 40), s.p), false;
    if (code == ' ' && !mods) return *c = (UiChord){ ' ', UI_MOD_TYPED }, true;
    if (mods == UI_MOD_ALT && ((code >= 'a' && code <= 'z') || (code >= '1' && code <= '9')))
        return snprintf(why, (size_t)cap, "Alt and a letter or digit stays: the menus, a tab by place"), false;
    return *c = (UiChord){ code, mods }, true;
}

// The command named name (UI_CMDS), UK_NONE for none.
static UiKey ui_cmd_named(Str name)
{
    for (int i = 0; i < COUNT(UI_CMDS); i++)
        if (str_eq(name, str_from_c(UI_CMDS[i].name))) return UI_CMDS[i].cmd;
    return UK_NONE;
}

static const char *ui_cmd_name(UiKey cmd)
{
    for (int i = 0; i < COUNT(UI_CMDS); i++)
        if (UI_CMDS[i].cmd == cmd) return UI_CMDS[i].name;
    return "?";
}

static void ui_binds_drop(int i)
{
    memmove(&ui_binds.e[i], &ui_binds.e[i + 1], sizeof ui_binds.e[0] * (size_t)(ui_binds.n - i - 1));
    ui_binds.n--;
}

// What ofx.conf's key lines did: the first line skipped and why; how many keys a line took from
// another command (a default's, or an earlier line's), how many of those weren't taken the read
// before (fresh: said once, Michael's answer 3), and the first fresh one; whether the bindings
// changed from those before.
typedef struct {
    int  bad;
    char why[200];
    int  clashes, fresh;
    char clash[200];
    bool changed;
} UiKeysRead;

enum { UI_CLASHES_MAX = 64 };

// The keys lines took from another command as the status line last could say them (UiKeysRead.fresh
// is the rest), and the last read's, until ui_clashes_told.
static struct {
    UiChord c[UI_CLASHES_MAX], next[UI_CLASHES_MAX];
    int     n, next_n;
} ui_clashed;

// The last read's clashes as said: called when the status line could say them (no bad line took it:
// the step 2 confirm, 2026-11-08), so a clash saved beside a typo is said once the typo is fixed.
static void ui_clashes_told(void)
{
    memcpy(ui_clashed.c, ui_clashed.next, sizeof ui_clashed.c[0] * (size_t)ui_clashed.next_n), ui_clashed.n = ui_clashed.next_n;
}

// ofx.conf's key lines (n of them, each `<key> <command>` or `none <command>`, from file line no[i])
// over the defaults, in order: a key bound leaves the command it had (which keeps its others), a
// later line on the same key wins over an earlier one, none takes all of a command's keys so far.
// A line ofx can't read is skipped and the rest apply.
static UiKeysRead ui_binds_set(const char (*line)[CONF_KEY_LEN], const int *no, int n)
{
    UiKeysRead r = { 0 };
    static UiBind was[UI_BINDS_MAX];
    ui_binds_ready();
    int was_n = ui_binds.n;
    for (int i = 0; i < was_n; i++) was[i] = ui_binds.e[i].b;
    ui_binds_reset();
    int users = 0;   // (ofx.conf's come first: a command's first key is the one its menu shows)
    static UiChord now[UI_CLASHES_MAX];   // the keys this read's lines took
    int now_n = 0;
    for (int k = 0; k < n; k++) {
        Str l = str_trim(str_from_c(line[k]));
        isize sp = str_find(l, ' ');
        Str keys = sp < 0 ? l : str_cut(l, 0, sp), name = sp < 0 ? STR_EMPTY : str_trim(str_cut(l, sp + 1, l.len));
        UiKey cmd = ui_cmd_named(name);
        char why[160] = "";
        UiChord c = { 0 };
        bool none = str_eq(keys, S("none"));
        if (sp < 0 || str_find(name, ' ') >= 0) snprintf(why, sizeof why, "key wants a key, then a command");
        else if (!cmd) snprintf(why, sizeof why, "no command %.*s", (int)(name.len < 40 ? name.len : 40), name.p);
        else if (!none && !ui_chord_parse(keys, &c, why, (int)sizeof why)) {}
        if (why[0]) {
            if (!r.bad) r.bad = no[k], snprintf(r.why, sizeof r.why, "%s", why);
            continue;
        }
        for (int i = ui_binds.n - 1; i >= 0; i--) {
            bool same_key = !none && ui_chord_eq(ui_binds.e[i].b.c, c);
            if (!(none ? ui_binds.e[i].b.cmd == cmd : same_key)) continue;
            if (same_key && ui_binds.e[i].b.cmd != cmd) {
                r.clashes++;
                bool seen = false;
                for (int j = 0; j < ui_clashed.n && !seen; j++) seen = ui_chord_eq(ui_clashed.c[j], c);
                if (now_n < UI_CLASHES_MAX) now[now_n++] = c;
                if (!seen && !r.fresh++) {
                    char kn[UI_KEY_NAME_MAX];
                    ui_chord_name(c, kn, (int)sizeof kn);
                    if (ui_binds.e[i].user) snprintf(r.clash, sizeof r.clash, "lines %d and %d both bind %s: line %d wins", ui_binds.e[i].line, no[k], kn, no[k]);
                    else snprintf(r.clash, sizeof r.clash, "%s is %.*s now, not %s", kn, (int)name.len, name.p, ui_cmd_name(ui_binds.e[i].b.cmd));
                }
            }
            if (ui_binds.e[i].user) users--;
            ui_binds_drop(i);
        }
        if (none) continue;
        memmove(&ui_binds.e[users + 1], &ui_binds.e[users], sizeof ui_binds.e[0] * (size_t)(ui_binds.n - users));
        ui_binds.e[users] = (UiBinding){ .b = { c, cmd }, .user = true, .line = no[k] };
        ui_chord_name(c, ui_binds.e[users].name, UI_KEY_NAME_MAX);
        users++, ui_binds.n++;
    }
    r.changed = was_n != ui_binds.n;
    for (int i = 0; i < was_n && !r.changed; i++) r.changed = !ui_chord_eq(was[i].c, ui_binds.e[i].b.c) || was[i].cmd != ui_binds.e[i].b.cmd;
    memcpy(ui_clashed.next, now, sizeof now[0] * (size_t)now_n), ui_clashed.next_n = now_n;
    return r;
}

// The key lines the written ofx.conf has: every key as ofx has it, commented out, and the
// commands with none. Its length; cut at cap.
static size_t ui_keys_conf_text(char *out, size_t cap)
{
    size_t at = 0;
    char b[UI_KEY_NAME_MAX];
    out[0] = 0;
    for (int i = 0; i < COUNT(UI_BINDS_DEFAULT) && at < cap; i++) {
        ui_chord_name(UI_BINDS_DEFAULT[i].c, b, (int)sizeof b);
        int n = snprintf(out + at, cap - at, "#key = %s %s\n", b, ui_cmd_name(UI_BINDS_DEFAULT[i].cmd));
        if (n > 0) at += (size_t)n;
    }
    bool first = true;
    size_t start = at;   // (the comment line's start: wrapped before 80 columns)
    for (int i = 0; i < COUNT(UI_CMDS) && at < cap; i++) {
        bool has = false;
        for (int j = 0; j < COUNT(UI_BINDS_DEFAULT); j++) has |= UI_BINDS_DEFAULT[j].cmd == UI_CMDS[i].cmd;
        if (has) continue;
        bool wrap = !first && at - start + 2 + strlen(UI_CMDS[i].name) > 79;
        int n = snprintf(out + at, cap - at, "%s%s", first ? "# With no key: " : wrap ? ",\n# " : ", ", UI_CMDS[i].name);
        if (wrap) start = at + 2;
        if (n > 0) at += (size_t)n;
        first = false;
    }
    if (!first && at < cap) {
        int n = snprintf(out + at, cap - at, "\n");
        if (n > 0) at += (size_t)n;
    }
    return at < cap ? at : cap - 1;
}

// ---- the menus and the panel ---------------------------------------------------------------------

// What menu entry it shows where a key goes: its command's first key, or its hint ("" neither).
static const char *ui_menu_key(UiMenuItem it)
{
    if (UI_MENU_ITEM[it].hint) return UI_MENU_ITEM[it].hint;
    return UI_MENU_ITEM[it].cmd ? ui_cmd_key(UI_MENU_ITEM[it].cmd, 0) : "";
}

// Keyboard Shortcuts' row r's key k (0 the first, 1 the second): what the row says, else its
// command's keys in order after the row's own ("Esc", then the job rows' key); -1: a row's text.
static int ui_keys_bind(int r, int k, const char **text)
{
    *text = k ? UI_KEYS[r].key2 : UI_KEYS[r].key;
    if (*text || !UI_KEYS[r].cmd) return -1;
    return ui_cmd_bind(UI_KEYS[r].cmd, k - (k == 1 && UI_KEYS[r].key));
}

// Row r's key k as shown ("" none).
static const char *ui_keys_key(int r, int k)
{
    const char *text;
    int i = ui_keys_bind(r, k, &text);
    return text ? text : i < 0 ? "" : ui_binds.e[i].name;
}

// Whether that key is one of yours, from ofx.conf (the panel marks it, F15 step 3).
static bool ui_keys_mine(int r, int k)
{
    const char *text;
    int i = ui_keys_bind(r, k, &text);
    return i >= 0 && ui_binds.e[i].user;
}

// Row r's command's name in ofx.conf, "" for a row of fixed keys.
static const char *ui_keys_name(int r)
{
    return UI_KEYS[r].cmd ? ui_cmd_name(UI_KEYS[r].cmd) : "";
}

// what, and its command's first key in brackets ("Back (Alt+Left)"); what alone when the command has
// none (ofx.conf took them): a tooltip or a hint never names a key that doesn't do it (the F15
// code-health review). out, for the caller to use at once.
static const char *ui_with_key(char *out, size_t cap, const char *what, UiKey cmd)
{
    const char *k = ui_cmd_key(cmd, 0);
    snprintf(out, cap, k[0] ? "%s (%s)" : "%s", what, k);
    return out;
}
