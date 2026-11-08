// F15's unit tests (docs/plan.md "F15 design"): the one key table (ui/keys.c) and GLFW's keys as
// its chords (gui/glfwkeys.c). Step 1 changes nothing a key does: every key, menu label and
// shortcuts row is pinned to what ofx had before. Included by test.c.

#include "GLFW/glfw3.h"
#include "../gui/glfwkeys.c"

// What on_key sent before F15 (gui/input.c's ui_key_of, and Ctrl+Q, Alt+digit and Alt+letter):
// for every GLFW key, modifier set (GLFW's bits: 1 Shift, 2 Ctrl, 4 Alt, 8 Super) and action (1
// press, 2 repeat) that sent anything, the event (its key, its character). Recorded from the code at
// 3ef3827; the rest sent nothing.
static const struct { int key, mods, action; UiKey uk; u32 ch; } PIN_KEYS[] = {
    { 45, 2, 1, UK_ZOOM_OUT, 0 }, { 45, 2, 2, UK_ZOOM_OUT, 0 }, { 48, 2, 1, UK_ZOOM_RESET, 0 }, { 48, 2, 2, UK_ZOOM_RESET, 0 },
    { 49, 2, 1, UK_VIEW_LIST, 0 }, { 49, 2, 2, UK_VIEW_LIST, 0 }, { 49, 4, 1, UK_TAB_JUMP, 1 }, { 50, 2, 1, UK_VIEW_ICONS, 0 },
    { 50, 2, 2, UK_VIEW_ICONS, 0 }, { 50, 4, 1, UK_TAB_JUMP, 2 }, { 51, 4, 1, UK_TAB_JUMP, 3 }, { 52, 4, 1, UK_TAB_JUMP, 4 },
    { 53, 4, 1, UK_TAB_JUMP, 5 }, { 54, 4, 1, UK_TAB_JUMP, 6 }, { 55, 4, 1, UK_TAB_JUMP, 7 }, { 56, 4, 1, UK_TAB_JUMP, 8 },
    { 57, 4, 1, UK_TAB_JUMP, 9 }, { 61, 2, 1, UK_ZOOM_IN, 0 }, { 61, 2, 2, UK_ZOOM_IN, 0 }, { 61, 3, 1, UK_ZOOM_IN, 0 },
    { 61, 3, 2, UK_ZOOM_IN, 0 }, { 65, 2, 1, UK_ALL, 0 }, { 65, 2, 2, UK_ALL, 0 }, { 65, 3, 1, UK_SELECT_MATCH, 0 },
    { 65, 3, 2, UK_SELECT_MATCH, 0 }, { 65, 4, 1, UK_MENU, 97 }, { 66, 2, 1, UK_EDIT_BOOKMARKS, 0 }, { 66, 2, 2, UK_EDIT_BOOKMARKS, 0 },
    { 66, 4, 1, UK_MENU, 98 }, { 67, 2, 1, UK_COPY, 0 }, { 67, 2, 2, UK_COPY, 0 }, { 67, 3, 1, UK_COPY_PATH, 0 },
    { 67, 3, 2, UK_COPY_PATH, 0 }, { 67, 4, 1, UK_MENU, 99 }, { 68, 2, 1, UK_ADD_BOOKMARK, 0 }, { 68, 2, 2, UK_ADD_BOOKMARK, 0 },
    { 68, 4, 1, UK_MENU, 100 }, { 69, 4, 1, UK_MENU, 101 }, { 70, 2, 1, UK_FIND, 0 }, { 70, 2, 2, UK_FIND, 0 },
    { 70, 3, 1, UK_SEARCH, 0 }, { 70, 3, 2, UK_SEARCH, 0 }, { 70, 4, 1, UK_MENU, 102 }, { 71, 2, 1, UK_GROUP_COL, 0 },
    { 71, 2, 2, UK_GROUP_COL, 0 }, { 71, 4, 1, UK_MENU, 103 }, { 72, 2, 1, UK_HIDDEN, 0 }, { 72, 2, 2, UK_HIDDEN, 0 },
    { 72, 4, 1, UK_MENU, 104 }, { 73, 2, 1, UK_PROPERTIES, 0 }, { 73, 2, 2, UK_PROPERTIES, 0 }, { 73, 3, 1, UK_INVERT, 0 },
    { 73, 3, 2, UK_INVERT, 0 }, { 73, 4, 1, UK_MENU, 105 }, { 74, 2, 1, UK_JOBS, 0 }, { 74, 2, 2, UK_JOBS, 0 },
    { 74, 4, 1, UK_MENU, 106 }, { 75, 2, 1, UK_CRUMBS, 0 }, { 75, 2, 2, UK_CRUMBS, 0 }, { 75, 4, 1, UK_MENU, 107 },
    { 76, 2, 1, UK_LOCATION, 0 }, { 76, 2, 2, UK_LOCATION, 0 }, { 76, 4, 1, UK_MENU, 108 }, { 77, 2, 1, UK_MAKE_LINK, 0 },
    { 77, 2, 2, UK_MAKE_LINK, 0 }, { 77, 4, 1, UK_MENU, 109 }, { 78, 2, 1, UK_NEW_WINDOW, 0 }, { 78, 2, 2, UK_NEW_WINDOW, 0 },
    { 78, 3, 1, UK_NEWFOLDER, 0 }, { 78, 3, 2, UK_NEWFOLDER, 0 }, { 78, 4, 1, UK_MENU, 110 }, { 79, 4, 1, UK_MENU, 111 },
    { 80, 2, 1, UK_PERMS_COLS, 0 }, { 80, 2, 2, UK_PERMS_COLS, 0 }, { 80, 4, 1, UK_MENU, 112 }, { 81, 2, 1, UK_QUIT_ALL, 0 },
    { 81, 4, 1, UK_MENU, 113 }, { 82, 2, 1, UK_RELOAD, 0 }, { 82, 2, 2, UK_RELOAD, 0 }, { 82, 4, 1, UK_MENU, 114 },
    { 83, 2, 1, UK_F9, 0 }, { 83, 2, 2, UK_F9, 0 }, { 83, 4, 1, UK_MENU, 115 }, { 84, 2, 1, UK_TAB_NEW, 0 },
    { 84, 2, 2, UK_TAB_NEW, 0 }, { 84, 3, 1, UK_TAB_REOPEN, 0 }, { 84, 3, 2, UK_TAB_REOPEN, 0 }, { 84, 4, 1, UK_MENU, 116 },
    { 85, 4, 1, UK_MENU, 117 }, { 86, 2, 1, UK_PASTE, 0 }, { 86, 2, 2, UK_PASTE, 0 }, { 86, 4, 1, UK_MENU, 118 },
    { 87, 2, 1, UK_TAB_CLOSE, 0 }, { 87, 2, 2, UK_TAB_CLOSE, 0 }, { 87, 4, 1, UK_MENU, 119 }, { 88, 2, 1, UK_CUT, 0 },
    { 88, 2, 2, UK_CUT, 0 }, { 88, 4, 1, UK_MENU, 120 }, { 89, 4, 1, UK_MENU, 121 }, { 90, 2, 1, UK_UNDO, 0 },
    { 90, 2, 2, UK_UNDO, 0 }, { 90, 4, 1, UK_MENU, 122 }, { 256, 0, 1, UK_ESC, 0 }, { 256, 0, 2, UK_ESC, 0 },
    { 256, 1, 1, UK_ESC, 0 }, { 256, 1, 2, UK_ESC, 0 }, { 257, 0, 1, UK_ENTER, 0 }, { 257, 0, 2, UK_ENTER, 0 },
    { 257, 1, 1, UK_SHIFT_ENTER, 0 }, { 257, 1, 2, UK_SHIFT_ENTER, 0 }, { 257, 2, 1, UK_CONTAINING, 0 }, { 257, 2, 2, UK_CONTAINING, 0 },
    { 257, 3, 1, UK_OPEN_LINE, 0 }, { 257, 3, 2, UK_OPEN_LINE, 0 }, { 257, 4, 1, UK_TERMINAL, 0 }, { 257, 4, 2, UK_TERMINAL, 0 },
    { 258, 0, 1, UK_TAB, 0 }, { 258, 0, 2, UK_TAB, 0 }, { 258, 1, 1, UK_TAB, 0 }, { 258, 1, 2, UK_TAB, 0 },
    { 258, 2, 1, UK_TAB_NEXT, 0 }, { 258, 2, 2, UK_TAB_NEXT, 0 }, { 258, 3, 1, UK_TAB_PREV, 0 }, { 258, 3, 2, UK_TAB_PREV, 0 },
    { 259, 0, 1, UK_BACKSPACE, 0 }, { 259, 0, 2, UK_BACKSPACE, 0 }, { 259, 1, 1, UK_BACKSPACE, 0 }, { 259, 1, 2, UK_BACKSPACE, 0 },
    { 259, 2, 1, UK_WORD_BACKSPACE, 0 }, { 259, 2, 2, UK_WORD_BACKSPACE, 0 }, { 261, 0, 1, UK_DELETE, 0 }, { 261, 0, 2, UK_DELETE, 0 },
    { 261, 1, 1, UK_SHIFT_DELETE, 0 }, { 261, 1, 2, UK_SHIFT_DELETE, 0 }, { 261, 2, 1, UK_WORD_DELETE, 0 }, { 261, 2, 2, UK_WORD_DELETE, 0 },
    { 262, 0, 1, UK_RIGHT, 0 }, { 262, 0, 2, UK_RIGHT, 0 }, { 262, 1, 1, UK_SEL_RIGHT, 0 }, { 262, 1, 2, UK_SEL_RIGHT, 0 },
    { 262, 2, 1, UK_WORD_RIGHT, 0 }, { 262, 2, 2, UK_WORD_RIGHT, 0 }, { 262, 3, 1, UK_SEL_WORD_RIGHT, 0 }, { 262, 3, 2, UK_SEL_WORD_RIGHT, 0 },
    { 262, 4, 1, UK_FORWARD, 0 }, { 262, 4, 2, UK_FORWARD, 0 }, { 263, 0, 1, UK_LEFT, 0 }, { 263, 0, 2, UK_LEFT, 0 },
    { 263, 1, 1, UK_SEL_LEFT, 0 }, { 263, 1, 2, UK_SEL_LEFT, 0 }, { 263, 2, 1, UK_WORD_LEFT, 0 }, { 263, 2, 2, UK_WORD_LEFT, 0 },
    { 263, 3, 1, UK_SEL_WORD_LEFT, 0 }, { 263, 3, 2, UK_SEL_WORD_LEFT, 0 }, { 263, 4, 1, UK_BACK, 0 }, { 263, 4, 2, UK_BACK, 0 },
    { 264, 0, 1, UK_DOWN, 0 }, { 264, 0, 2, UK_DOWN, 0 }, { 264, 1, 1, UK_SEL_DOWN, 0 }, { 264, 1, 2, UK_SEL_DOWN, 0 },
    { 264, 2, 1, UK_JOBS, 0 }, { 264, 2, 2, UK_JOBS, 0 }, { 264, 4, 1, UK_ALT_DOWN, 0 }, { 264, 4, 2, UK_ALT_DOWN, 0 },
    { 265, 0, 1, UK_UP, 0 }, { 265, 0, 2, UK_UP, 0 }, { 265, 1, 1, UK_SEL_UP, 0 }, { 265, 1, 2, UK_SEL_UP, 0 },
    { 265, 2, 1, UK_CRUMBS, 0 }, { 265, 2, 2, UK_CRUMBS, 0 }, { 265, 4, 1, UK_PARENT, 0 }, { 265, 4, 2, UK_PARENT, 0 },
    { 266, 0, 1, UK_PAGEUP, 0 }, { 266, 0, 2, UK_PAGEUP, 0 }, { 266, 1, 1, UK_PAGEUP, 0 }, { 266, 1, 2, UK_PAGEUP, 0 },
    { 266, 2, 1, UK_TAB_PREV, 0 }, { 266, 2, 2, UK_TAB_PREV, 0 }, { 266, 3, 1, UK_TAB_LEFT, 0 }, { 266, 3, 2, UK_TAB_LEFT, 0 },
    { 267, 0, 1, UK_PAGEDOWN, 0 }, { 267, 0, 2, UK_PAGEDOWN, 0 }, { 267, 1, 1, UK_PAGEDOWN, 0 }, { 267, 1, 2, UK_PAGEDOWN, 0 },
    { 267, 2, 1, UK_TAB_NEXT, 0 }, { 267, 2, 2, UK_TAB_NEXT, 0 }, { 267, 3, 1, UK_TAB_RIGHT, 0 }, { 267, 3, 2, UK_TAB_RIGHT, 0 },
    { 268, 0, 1, UK_HOME, 0 }, { 268, 0, 2, UK_HOME, 0 }, { 268, 1, 1, UK_SEL_HOME, 0 }, { 268, 1, 2, UK_SEL_HOME, 0 },
    { 268, 4, 1, UK_HOMEDIR, 0 }, { 268, 4, 2, UK_HOMEDIR, 0 }, { 269, 0, 1, UK_END, 0 }, { 269, 0, 2, UK_END, 0 },
    { 269, 1, 1, UK_SEL_END, 0 }, { 269, 1, 2, UK_SEL_END, 0 }, { 269, 4, 1, UK_TRASH_VIEW, 0 }, { 269, 4, 2, UK_TRASH_VIEW, 0 },
    { 290, 0, 1, UK_SHORTCUTS, 0 }, { 290, 0, 2, UK_SHORTCUTS, 0 }, { 290, 2, 1, UK_SHORTCUTS, 0 }, { 290, 2, 2, UK_SHORTCUTS, 0 },
    { 291, 0, 1, UK_F2, 0 }, { 291, 0, 2, UK_F2, 0 }, { 298, 0, 1, UK_F9, 0 }, { 298, 0, 2, UK_F9, 0 },
    { 299, 0, 1, UK_F10, 0 }, { 299, 0, 2, UK_F10, 0 }, { 299, 1, 1, UK_CONTEXT, 0 }, { 299, 1, 2, UK_CONTEXT, 0 },
    { 320, 2, 1, UK_ZOOM_RESET, 0 }, { 320, 2, 2, UK_ZOOM_RESET, 0 }, { 321, 2, 1, UK_VIEW_LIST, 0 }, { 321, 2, 2, UK_VIEW_LIST, 0 },
    { 322, 2, 1, UK_VIEW_ICONS, 0 }, { 322, 2, 2, UK_VIEW_ICONS, 0 }, { 333, 2, 1, UK_ZOOM_OUT, 0 }, { 333, 2, 2, UK_ZOOM_OUT, 0 },
    { 334, 2, 1, UK_ZOOM_IN, 0 }, { 334, 2, 2, UK_ZOOM_IN, 0 }, { 334, 3, 1, UK_ZOOM_IN, 0 }, { 334, 3, 2, UK_ZOOM_IN, 0 },
    { 335, 0, 1, UK_ENTER, 0 }, { 335, 0, 2, UK_ENTER, 0 }, { 335, 1, 1, UK_SHIFT_ENTER, 0 }, { 335, 1, 2, UK_SHIFT_ENTER, 0 },
    { 335, 2, 1, UK_CONTAINING, 0 }, { 335, 2, 2, UK_CONTAINING, 0 }, { 335, 3, 1, UK_OPEN_LINE, 0 }, { 335, 3, 2, UK_OPEN_LINE, 0 },
    { 335, 4, 1, UK_TERMINAL, 0 }, { 335, 4, 2, UK_TERMINAL, 0 }, { 348, 0, 1, UK_CONTEXT, 0 }, { 348, 0, 2, UK_CONTEXT, 0 },
};
// Ctrl+Q with Shift, Alt or Super held closed every window too (on_key tested Ctrl alone); F15
// binds Ctrl+Q only.
static const struct { int key, mods, action; UiKey uk; u32 ch; } PIN_KEYS_DROPPED[] = {
    { 81, 3, 1, UK_QUIT_ALL, 0 }, { 81, 6, 1, UK_QUIT_ALL, 0 }, { 81, 7, 1, UK_QUIT_ALL, 0 }, { 81, 10, 1, UK_QUIT_ALL, 0 },
    { 81, 11, 1, UK_QUIT_ALL, 0 }, { 81, 14, 1, UK_QUIT_ALL, 0 }, { 81, 15, 1, UK_QUIT_ALL, 0 },
};
// Each menu entry's key as it showed, and the event picking it ran (UK_NONE, 0: its own action).
static const struct { const char *key; UiKey uk; u32 ch; } PIN_MENU[UM_ITEMS] = {
    { "", UK_NONE, 0 },
    { "Ctrl+T", UK_NONE, 0 },
    { "Ctrl+Shift+T", UK_TAB_REOPEN, 0 },
    { "Ctrl+N", UK_NONE, 0 },
    { "Ctrl+Shift+N", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "Enter", UK_NONE, 0 },
    { "Shift+Enter", UK_NONE, 0 },
    { "Ctrl+I", UK_NONE, 0 },
    { "Ctrl+Q", UK_NONE, 0 },
    { "Ctrl+W", UK_NONE, 0 },
    { "Ctrl+Z", UK_UNDO, 0 },
    { "Ctrl+X", UK_CUT, 0 },
    { "Ctrl+C", UK_COPY, 0 },
    { "Ctrl+Shift+C", UK_COPY_PATH, 0 },
    { "Ctrl+V", UK_PASTE, 0 },
    { "Ctrl+A", UK_ALL, 0 },
    { "Ctrl+Shift+A", UK_SELECT_MATCH, 0 },
    { "Ctrl+Shift+I", UK_INVERT, 0 },
    { "", UK_NONE, 0 },
    { "Ctrl+M", UK_MAKE_LINK, 0 },
    { "F2", UK_F2, 0 },
    { "Delete", UK_DELETE, 0 },
    { "Shift+Delete", UK_SHIFT_DELETE, 0 },
    { "r", UK_NONE, 114 },
    { "Ctrl+R", UK_RELOAD, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "Ctrl+H", UK_HIDDEN, 0 },
    { "Ctrl+P", UK_PERMS_COLS, 0 },
    { "Ctrl+G", UK_GROUP_COL, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "S", UK_NONE, 83 },
    { "Ctrl+=", UK_ZOOM_IN, 0 },
    { "Ctrl+-", UK_ZOOM_OUT, 0 },
    { "Ctrl+0", UK_ZOOM_RESET, 0 },
    { "Ctrl+1", UK_VIEW_LIST, 0 },
    { "Ctrl+2", UK_VIEW_ICONS, 0 },
    { "Alt+Up", UK_PARENT, 0 },
    { "Alt+Left", UK_BACK, 0 },
    { "Alt+Right", UK_FORWARD, 0 },
    { "Alt+Home", UK_HOMEDIR, 0 },
    { "Alt+End", UK_TRASH_VIEW, 0 },
    { "Ctrl+F", UK_FIND, 0 },
    { "Ctrl+Shift+F", UK_SEARCH, 0 },
    { "Ctrl+L", UK_LOCATION, 0 },
    { "Alt+Enter", UK_TERMINAL, 0 },
    { "Ctrl+D", UK_ADD_BOOKMARK, 0 },
    { "Ctrl+B", UK_EDIT_BOOKMARKS, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "F1", UK_SHORTCUTS, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "Middle click", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "Ctrl+Enter", UK_CONTAINING, 0 },
    { "Ctrl+Shift+Enter", UK_OPEN_LINE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "Ctrl+B", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "Ctrl+Shift+PgUp", UK_TAB_LEFT, 0 },
    { "Ctrl+Shift+PgDn", UK_TAB_RIGHT, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
    { "", UK_NONE, 0 },
};

// An event as one UiKey, as ofx read a key before F15: the key's own meaning, or its command.
static UiKey legacy_key(UiEvent e)
{
    return e.key == UK_CMD ? e.cmd : e.key;
}

// What the files and the sidebar did with a key whose one UiKey was uk: the command F15 gives it.
static UiKey legacy_cmd(UiKey uk)
{
    switch (uk) {
    case UK_BACKSPACE: return UK_PARENT;      // (UI_KEY_ACTS had both)
    case UK_WORD_LEFT: return UK_SIDE_GO;     // (outside a text field, the sidebar)
    case UK_WORD_RIGHT: return UK_LOCATION;   // (and the location line)
    case UK_SEL_WORD_LEFT: case UK_SEL_WORD_RIGHT: case UK_WORD_BACKSPACE: case UK_WORD_DELETE:
    case UK_ALT_DOWN: case UK_MENU: return UK_NONE;   // (the fields' and panels' alone)
    default: return uk;
    }
}

static void test_keys_pinned(void)
{
    // Every GLFW key, modifier set and action: the same event as before, and the command the files
    // did it as; nothing where nothing was sent.
    int sent = 0, bad = 0;
    for (int key = GLFW_KEY_UNKNOWN; key <= GLFW_KEY_LAST; key++)
        for (int mods = 0; mods < 16; mods++)
            for (int action = GLFW_PRESS; action <= GLFW_REPEAT; action++) {
                UiEvent e = glfw_event(key, mods, action);
                UiKey uk = UK_NONE;
                u32 ch = 0;
                for (int i = 0; i < COUNT(PIN_KEYS); i++)
                    if (PIN_KEYS[i].key == key && PIN_KEYS[i].mods == mods && PIN_KEYS[i].action == action) uk = PIN_KEYS[i].uk, ch = PIN_KEYS[i].ch;
                // (and nothing at all where nothing was: a key that means nothing sends no event)
                // (the one key F15 adds: Ctrl+Shift+W, Close Other Tabs, Michael 2026-11-08)
                if (key == GLFW_KEY_W && mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT)) {
                    CHECK(e.key == UK_CMD && e.cmd == UK_ITEM0 + UM_TAB_OTHERS);
                    continue;
                }
                bool ok = legacy_key(e) == uk && e.ch == ch && e.cmd == legacy_cmd(uk) && (uk || (!e.key && !e.cmd)) && (e.key != UK_CMD || e.cmd);
                sent += uk != UK_NONE;
                if (!ok && bad++ < 10) fprintf(stderr, "key %d mods %d action %d: %d,%u,%d (was %d,%u)\n", key, mods, action, e.key, e.ch, e.cmd, uk, ch);
            }
    CHECK(bad == 0 && sent == COUNT(PIN_KEYS));
    for (int i = 0; i < COUNT(PIN_KEYS_DROPPED); i++) {
        UiEvent e = glfw_event(PIN_KEYS_DROPPED[i].key, PIN_KEYS_DROPPED[i].mods, PIN_KEYS_DROPPED[i].action);
        CHECK(e.cmd != UK_QUIT_ALL);
    }
    CHECK(glfw_event(GLFW_KEY_Q, GLFW_MOD_CONTROL, GLFW_PRESS).cmd == UK_QUIT_ALL);
    CHECK(glfw_event(GLFW_KEY_Q, GLFW_MOD_CONTROL, GLFW_REPEAT).cmd == UK_NONE);   // (once a press)
    CHECK(glfw_event(GLFW_KEY_Q, GLFW_MOD_CONTROL, GLFW_RELEASE).cmd == UK_NONE);
    // Caps Lock and Num Lock don't change a key.
    UiEvent l = glfw_event(GLFW_KEY_T, GLFW_MOD_CONTROL | GLFW_MOD_CAPS_LOCK | GLFW_MOD_NUM_LOCK, GLFW_PRESS);
    CHECK(l.key == UK_CMD && l.cmd == UK_TAB_NEW);

    // The typed characters: the letters the files had as commands, their commands; the rest only
    // themselves.
    static const struct { char c; UiKey cmd; } TYPED[] = {
        { 'k', UK_UP }, { 'j', UK_DOWN }, { 'h', UK_LEFT }, { 'l', UK_RIGHT }, { 'K', UK_SEL_UP }, { 'J', UK_SEL_DOWN },
        { 'H', UK_SEL_LEFT }, { 'L', UK_SEL_RIGHT }, { 'g', UK_HOME }, { 'G', UK_END }, { '/', UK_FIND }, { '.', UK_HIDDEN },
        { 't', UK_TERMINAL }, { 'r', UK_RESTORE }, { '~', UK_HOMEDIR }, { 's', UK_SORT_NEXT }, { 'S', UK_SORT_REVERSE },
        { 'n', UK_FIND_NEXT }, { 'N', UK_FIND_PREV }, { ' ', UK_MARK },
    };
    for (u32 c = 0; c < 0x80; c++) {
        UiKey want = UK_NONE;
        for (int i = 0; i < COUNT(TYPED); i++)
            if ((u32)(unsigned char)TYPED[i].c == c) want = TYPED[i].cmd;
        UiEvent e = ui_event_of((UiChord){ c, UI_MOD_TYPED });
        CHECK(e.key == UK_NONE && e.ch == c && e.cmd == want);
    }
    CHECK(ui_event_of((UiChord){ 0xE9, UI_MOD_TYPED }).cmd == UK_NONE);

    // The menus: each entry's key as before, and what picking it runs, the same command (step 2:
    // the command alone, whatever keys it has).
    for (int it = 0; it < UM_ITEMS; it++) {
        UiEvent e = ui_menu_runs((UiMenuItem)it);
        UiKey was = PIN_MENU[it].uk ? legacy_cmd(PIN_MENU[it].uk) : PIN_MENU[it].ch ? ui_event_of((UiChord){ PIN_MENU[it].ch, UI_MOD_TYPED }).cmd : UK_NONE;
        const char *label = it == UM_TAB_OTHERS ? "Ctrl+Shift+W" : PIN_MENU[it].key;   // (F15's one new key)
        bool ok = strcmp(ui_menu_key((UiMenuItem)it), label) == 0 && e.cmd == was && (was ? e.key == UK_CMD && !e.ch : !e.key);
        if (!ok) fprintf(stderr, "menu entry %s: %s %d,%u (was %s %d,%u)\n", UI_MENU_ITEM[it].word, ui_menu_key((UiMenuItem)it), legacy_key(e), e.ch, PIN_MENU[it].key, PIN_MENU[it].uk, PIN_MENU[it].ch);
        CHECK(ok);
        CHECK(!UI_MENU_ITEM[it].runs || e.cmd == UI_MENU_ITEM[it].cmd);
    }
    // (The shortcuts panel's rows were pinned too until step 3 gave each command its row:
    // test_keys_panel.)
}

static void test_keys_table(void)
{
    // Every command named once, in ofx.conf's words (lower case, digits, dashes), each with a key;
    // every key bound to a command once, and every key's own meaning once.
    for (int i = 0; i < COUNT(UI_CMDS); i++) {
        const char *n = UI_CMDS[i].name;
        bool word = n[0] >= 'a' && n[0] <= 'z';
        for (const char *p = n; *p; p++) word &= (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-';
        CHECK(word && UI_CMDS[i].cmd != UK_NONE && UI_CMDS[i].cmd != UK_CMD);
        for (int j = 0; j < i; j++) CHECK(strcmp(UI_CMDS[j].name, n) != 0 && UI_CMDS[j].cmd != UI_CMDS[i].cmd);
        CHECK(UI_CMDS[i].cmd >= UK_ITEM0 || ui_cmd_key(UI_CMDS[i].cmd, 0)[0]);   // (a menu entry's own: no key as ofx has them)
    }
    for (int i = 0; i < COUNT(UI_BINDS_DEFAULT); i++) {
        int named = 0;
        for (int j = 0; j < COUNT(UI_CMDS); j++) named += UI_CMDS[j].cmd == UI_BINDS_DEFAULT[i].cmd;
        CHECK(named == 1);
        for (int j = 0; j < i; j++) CHECK(!ui_chord_eq(UI_BINDS_DEFAULT[j].c, UI_BINDS_DEFAULT[i].c));
    }
    for (int i = 0; i < COUNT(UI_FIXED); i++)
        for (int j = 0; j < i; j++) CHECK(!ui_chord_eq(UI_FIXED[j].c, UI_FIXED[i].c));
    // A bar menu's entry with a key is named as its command (the right-click menus' own are the
    // folder's: bm-edit shows Edit Bookmarks' key).
    for (int it = 1; it < UM_OPEN_TAB; it++) {
        if (!UI_MENU_ITEM[it].cmd) continue;
        const char *n = "";
        for (int j = 0; j < COUNT(UI_CMDS); j++)
            if (UI_CMDS[j].cmd == UI_MENU_ITEM[it].cmd) n = UI_CMDS[j].name;
        if (strcmp(n, UI_MENU_ITEM[it].word) != 0) fprintf(stderr, "menu entry %s has command %s\n", UI_MENU_ITEM[it].word, n);
        CHECK(strcmp(n, UI_MENU_ITEM[it].word) == 0);
    }

    // Names, as the menus write them.
    char b[UI_KEY_NAME_MAX];
    static const struct { UiChord c; const char *name; } NAMES[] = {
        { { 'n', UI_MOD_CTRL | UI_MOD_SHIFT }, "Ctrl+Shift+N" }, { { UKC_LEFT, UI_MOD_ALT }, "Alt+Left" }, { { UKC_F1 + 1, 0 }, "F2" },
        { { UKC_F1 + 24, 0 }, "F25" }, { { UKC_KP0 + 1, UI_MOD_CTRL }, "Ctrl+KP1" }, { { UKC_KP_ADD, 0 }, "KP+" }, { { '=', UI_MOD_CTRL }, "Ctrl+=" },
        { { ' ', UI_MOD_TYPED }, "Space" }, { { ' ', UI_MOD_CTRL }, "Ctrl+Space" }, { { 'G', UI_MOD_TYPED }, "G" }, { { '~', UI_MOD_TYPED }, "~" },
        { { 0xE9, UI_MOD_TYPED }, "\xC3\xA9" }, { { UKC_PGDN, UI_MOD_CTRL | UI_MOD_ALT | UI_MOD_SHIFT | UI_MOD_SUPER }, "Ctrl+Alt+Shift+Super+PgDn" },
        { { UKC_KP_EQUAL, 0 }, "KP=" }, { { UKC_KP0 + 9, 0 }, "KP9" }, { { 0x1F, 0 }, "?" },
    };
    for (int i = 0; i < COUNT(NAMES); i++) {
        ui_chord_name(NAMES[i].c, b, (int)sizeof b);
        if (strcmp(b, NAMES[i].name) != 0) fprintf(stderr, "chord name %s, not %s\n", b, NAMES[i].name);
        CHECK(strcmp(b, NAMES[i].name) == 0);
    }
    ui_chord_name((UiChord){ UKC_BACKSPACE, UI_MOD_CTRL | UI_MOD_ALT | UI_MOD_SHIFT | UI_MOD_SUPER }, b, 12);
    CHECK(strcmp(b, "Ctrl+Alt+Sh") == 0);   // (cut at the room given)

    // Shift with Esc, Tab, Backspace, PgUp or PgDn is the key without it, command and all; with a
    // key whose Shift means something else (Up, Enter), not.
    CHECK(ui_event_of((UiChord){ UKC_ESC, UI_MOD_SHIFT }).cmd == UK_ESC && ui_event_of((UiChord){ UKC_BACKSPACE, UI_MOD_SHIFT }).cmd == UK_PARENT);
    CHECK(ui_event_of((UiChord){ UKC_UP, UI_MOD_SHIFT }).cmd == UK_SEL_UP && ui_event_of((UiChord){ UKC_DELETE, UI_MOD_SHIFT }).key == UK_CMD);
    CHECK(ui_event_of((UiChord){ UKC_F1 + 1, UI_MOD_SHIFT }).key == UK_NONE && ui_event_of((UiChord){ UKC_F1 + 1, UI_MOD_SHIFT }).cmd == UK_NONE);
    // Exactly five chords inherit so (the step 1 review, 2026-11-08): every key code with each
    // modifier set that holds Shift, against the chord bound itself.
    int inherit = 0;
    for (u32 code = 1; code <= UKC_F25; code++)
        for (u8 m = UI_MOD_SHIFT; m < 16; m++) {
            if (!(m & UI_MOD_SHIFT)) continue;
            UiChord c = { code, m };
            UiEvent e = ui_event_of(c);
            if (e.cmd && !ui_bound(c)) {
                inherit++;
                CHECK(m == UI_MOD_SHIFT && (code == UKC_ESC || code == UKC_TAB || code == UKC_BACKSPACE || code == UKC_PGUP || code == UKC_PGDN));
            }
        }
    CHECK(inherit == 5);
    // A command's keys in order, "" past the last.
    CHECK(strcmp(ui_cmd_key(UK_PARENT, 0), "Alt+Up") == 0 && strcmp(ui_cmd_key(UK_PARENT, 1), "Backspace") == 0 && strcmp(ui_cmd_key(UK_PARENT, 2), "") == 0);
    CHECK(strcmp(ui_cmd_key(UK_NONE, 0), "") == 0);
    // A command with no key of its own picked from a menu: the command alone.
    UiEvent alone = ui_cmd_event(UK_TAB_JUMP);
    CHECK(alone.key == UK_CMD && alone.cmd == UK_TAB_JUMP && alone.ch == 0);
}

// The commands that were letters in the files, as their own: S reverses and reverses back; Ctrl+F
// in the find bar keeps its text (a second find_open would clear it).
static void test_keys_commands(void)
{
    Ui u = menu_ui(5);
    bool was = u.sort.reverse;
    CHECK(menu_keys(&u, 1, 0x100 + 'S').kind == UA_SORT && u.sort.reverse != was);
    CHECK(menu_keys(&u, 1, 0x100 + 'S').kind == UA_SORT && u.sort.reverse == was);
    menu_keys(&u, 1, UK_FIND);
    menu_keys(&u, 2, 0x100 + 'a', 0x100 + 'b');
    CHECK(u.finding && u.focus == UI_FOCUS_FIND && u.find.len == 2);
    CHECK(menu_keys(&u, 1, UK_FIND).kind == UA_NONE && u.finding && u.focus == UI_FOCUS_FIND && u.find.len == 2);
}

// One event through ui_handle.
static UiAction key_act(Ui *u, UiEvent e)
{
    UiInput in = ui_input_none();
    in.ev[in.nev++] = e;
    return ui_handle(u, (UiNames){ name_at, NULL, NULL }, &in);
}

// ofx.conf's key lines (F15 step 2), n of them from file line 1 on.
static UiKeysRead keys_set(int n, ...)
{
    static char line[16][64];
    static int no[16];
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) snprintf(line[i], sizeof line[i], "%s", va_arg(ap, const char *)), no[i] = i + 1;
    va_end(ap);
    UiKeysRead r = ui_binds_set(line, no, n);
    ui_clashes_told();   // (as conf_apply with no bad line)
    return r;
}

static UiEvent chord_ev(u32 code, u8 mods)
{
    return ui_event_of((UiChord){ code, mods });
}

static void test_keys_conf(void)
{
    // Every name the menus write reads back as its key (the step 1 review): every named key and
    // US character key with every modifier set, but those a line can't bind (a character alone is
    // typed; Shift and one is the character it types; Alt and a letter or digit stays).
    char b[UI_KEY_NAME_MAX], why[160];
    int named = 0;
    for (u32 code = ' '; code <= UKC_F25; code++) {
        if (code >= 0x7F && code < UKC_ESC) continue;
        if (code >= 'A' && code <= 'Z') continue;   // (a key's character is lower case)
        for (u8 m = 0; m < 16; m++) {
            bool chr = code < 0x7F;
            if (chr && (m == 0 || m == UI_MOD_SHIFT)) continue;
            if (m == UI_MOD_ALT && ((code >= 'a' && code <= 'z') || (code >= '1' && code <= '9'))) continue;
            UiChord c = { code, m }, back = { 0 };
            ui_chord_name(c, b, (int)sizeof b);
            bool ok = ui_chord_parse(str_from_c(b), &back, why, (int)sizeof why) && ui_chord_eq(back, c);
            if (!ok) fprintf(stderr, "key name %s doesn't read back\n", b);
            CHECK(ok);
            named++;
        }
    }
    CHECK(named == 1827);   // (69 characters x 14 modifier sets less Alt and 35, 56 named keys x 16)
    for (u32 cp = '!'; cp < 0x7F; cp++) {   // and the typed characters, and Space
        UiChord c = { cp, UI_MOD_TYPED }, back = { 0 };
        ui_chord_name(c, b, (int)sizeof b);
        CHECK(ui_chord_parse(str_from_c(b), &back, why, (int)sizeof why) && ui_chord_eq(back, c));
    }
    UiChord c = { 0 };
    CHECK(ui_chord_parse(S("Space"), &c, why, (int)sizeof why) && ui_chord_eq(c, (UiChord){ ' ', UI_MOD_TYPED }));
    CHECK(ui_chord_parse(S("\xC3\xA9"), &c, why, (int)sizeof why) && ui_chord_eq(c, (UiChord){ 0xE9, UI_MOD_TYPED }));
    // Any case for the modifiers and the names, any order; a key's character either case.
    CHECK(ui_chord_parse(S("shift+CTRL+n"), &c, why, (int)sizeof why) && ui_chord_eq(c, (UiChord){ 'n', UI_MOD_CTRL | UI_MOD_SHIFT }));
    CHECK(ui_chord_parse(S("ctrl+pgdn"), &c, why, (int)sizeof why) && ui_chord_eq(c, (UiChord){ UKC_PGDN, UI_MOD_CTRL }));
    CHECK(ui_chord_parse(S("Ctrl++"), &c, why, (int)sizeof why) && ui_chord_eq(c, (UiChord){ '+', UI_MOD_CTRL }));
    CHECK(ui_chord_parse(S("kp+"), &c, why, (int)sizeof why) && ui_chord_eq(c, (UiChord){ UKC_KP_ADD, 0 }));
    // What a line can't bind, and why.
    CHECK(!ui_chord_parse(S("Shift+g"), &c, why, (int)sizeof why) && strstr(why, "write the character"));
    CHECK(!ui_chord_parse(S("Alt+f"), &c, why, (int)sizeof why) && strstr(why, "stays"));
    CHECK(!ui_chord_parse(S("Alt+3"), &c, why, (int)sizeof why) && strstr(why, "stays"));
    CHECK(ui_chord_parse(S("Alt+Shift+f"), &c, why, (int)sizeof why));   // (only Alt alone with one)
    CHECK(!ui_chord_parse(S("Ctrl+\xC3\xA9"), &c, why, (int)sizeof why) && strstr(why, "US keyboard"));
    CHECK(!ui_chord_parse(S("Hyper+x"), &c, why, (int)sizeof why) && strcmp(why, "no key Hyper+x") == 0);
    CHECK(!ui_chord_parse(S("Ctrl+"), &c, why, (int)sizeof why) && !ui_chord_parse(S(""), &c, why, (int)sizeof why));

    // A key of your own: it acts, and the menu shows it (yours first); the command keeps its others.
    UiKeysRead r = keys_set(1, "Ctrl+E terminal");
    CHECK(!r.bad && !r.clashes && r.changed);
    CHECK(chord_ev('e', UI_MOD_CTRL).cmd == UK_TERMINAL && chord_ev(UKC_ENTER, UI_MOD_ALT).cmd == UK_TERMINAL);
    CHECK(strcmp(ui_menu_key(UM_TERMINAL), "Ctrl+E") == 0 && strcmp(ui_cmd_key(UK_TERMINAL, 1), "Alt+Enter") == 0);
    CHECK(!keys_set(1, "Ctrl+E terminal").changed);   // (the same lines again: nothing changed)
    CHECK(keys_set(1, "Ctrl+E reload").changed);      // (as many keys, one to another command)
    // Yours wins a clash: Ctrl+T trashes, New Tab has no key left, Delete still trashes; said once.
    r = keys_set(1, "ctrl+t trash");
    CHECK(r.changed && !r.bad && r.clashes == 1 && strcmp(r.clash, "Ctrl+T is trash now, not new-tab") == 0);
    CHECK(chord_ev('t', UI_MOD_CTRL).cmd == UK_DELETE && chord_ev('t', UI_MOD_CTRL).key == UK_CMD);
    CHECK(strcmp(ui_menu_key(UM_NEW_TAB), "") == 0 && chord_ev(UKC_DELETE, 0).cmd == UK_DELETE);
    Ui u = menu_ui(5);
    u.list.sel = 1;
    CHECK(key_act(&u, chord_ev('t', UI_MOD_CTRL)).kind == UA_TRASH);
    // In a text field Ctrl+T still means nothing of its own, and Delete still deletes a character.
    ui_edit_rename(&u, S("abc"), false);
    UiAction a = key_act(&u, chord_ev(UKC_HOME, 0));
    a = key_act(&u, chord_ev('t', UI_MOD_CTRL));
    CHECK(a.kind == UA_NONE && u.focus == UI_FOCUS_EDIT && u.edit.len == 3);
    key_act(&u, chord_ev(UKC_DELETE, 0));
    CHECK(u.focus == UI_FOCUS_EDIT && u.edit.len == 2);
    ui_edit_close(&u);
    // Two keys of yours for one command: the menu shows the first line's; yours before its own.
    keys_set(2, "F7 terminal", "F8 terminal");
    CHECK(strcmp(ui_cmd_key(UK_TERMINAL, 0), "F7") == 0 && strcmp(ui_cmd_key(UK_TERMINAL, 1), "F8") == 0 && strcmp(ui_cmd_key(UK_TERMINAL, 2), "Alt+Enter") == 0);
    // Two of your lines on one key: the later wins, both named.
    r = keys_set(2, "Ctrl+E terminal", "Ctrl+E reload");
    CHECK(r.clashes == 1 && strcmp(r.clash, "lines 1 and 2 both bind Ctrl+E: line 2 wins") == 0 && chord_ev('e', UI_MOD_CTRL).cmd == UK_RELOAD);
    CHECK(strcmp(ui_menu_key(UM_TERMINAL), "Alt+Enter") == 0);
    // Said once (the step 2 review): a key taken the read before isn't said again; a new one is,
    // and only it.
    r = keys_set(1, "Ctrl+T reload");
    CHECK(r.fresh == 1 && strcmp(r.clash, "Ctrl+T is reload now, not new-tab") == 0);
    r = keys_set(2, "Ctrl+T reload", "F7 terminal");
    CHECK(r.clashes == 1 && r.fresh == 0);
    r = keys_set(2, "Ctrl+T reload", "Ctrl+R terminal");
    CHECK(r.clashes == 2 && r.fresh == 1 && strcmp(r.clash, "Ctrl+R is terminal now, not reload") == 0);
    // Saved beside a bad line, whose note takes the status line: said once that is fixed (the
    // step 2 confirm).
    keys_set(0);
    static char two[2][64] = { "Ctrl+T reload", "Ctrl+Bogus find" };
    static int two_no[2] = { 1, 2 };
    r = ui_binds_set(two, two_no, 2);   // (no ui_clashes_told: conf_apply showed the bad line)
    CHECK(r.bad == 2 && r.fresh == 1);
    r = keys_set(1, "Ctrl+T reload");
    CHECK(!r.bad && r.fresh == 1 && strcmp(r.clash, "Ctrl+T is reload now, not new-tab") == 0);
    // The earlier line named right after a none dropped a line before it (the step 2 review).
    r = keys_set(4, "F7 reload", "KP1 sort-name", "none reload", "KP1 find");
    CHECK(strcmp(r.clash, "lines 2 and 4 both bind KP1: line 4 wins") == 0);
    // none: every key of the command so far, yours too; a line after it gives one again.
    r = keys_set(2, "F8 trash", "none trash");
    CHECK(!r.bad && !r.clashes && chord_ev(UKC_DELETE, 0).cmd == UK_NONE && chord_ev(UKC_DELETE, 0).key == UK_DELETE);
    CHECK(chord_ev(UKC_F1 + 7, 0).cmd == UK_NONE && strcmp(ui_menu_key(UM_TRASH), "") == 0);
    keys_set(2, "none trash", "F8 trash");
    CHECK(chord_ev(UKC_F1 + 7, 0).cmd == UK_DELETE && chord_ev(UKC_DELETE, 0).cmd == UK_NONE && strcmp(ui_menu_key(UM_TRASH), "F8") == 0);
    // A typed character bound: the files run it, the find bar still types it.
    keys_set(1, "x terminal");
    CHECK(ui_event_of((UiChord){ 'x', UI_MOD_TYPED }).cmd == UK_TERMINAL);
    u = menu_ui(5);
    CHECK(key_act(&u, ev_ch('x')).kind == UA_TERMINAL);
    key_act(&u, ev_key(UK_FIND)), key_act(&u, ev_ch('x'));
    CHECK(u.finding && u.find.len == 1);
    // A menu entry with no key of its own, bound: run as picked, when its menu has it enabled.
    keys_set(1, "F5 new-file");
    CHECK(chord_ev(UKC_F1 + 4, 0).cmd == UK_ITEM0 + UM_NEW_FILE && strcmp(ui_menu_key(UM_NEW_FILE), "F5") == 0);
    u = menu_ui(5);
    CHECK(key_act(&u, chord_ev(UKC_F1 + 4, 0)).kind == UA_NEWFILE);
    u.can_write = false;   // (greyed by its view: why, as a key's action says)
    CHECK(key_act(&u, chord_ev(UKC_F1 + 4, 0)).kind == UA_READ_ONLY);
    u.can_write = true, u.view = UI_VIEW_TRASH;
    CHECK(key_act(&u, chord_ev(UKC_F1 + 4, 0)).kind == UA_NOT_IN_TRASH);
    u.view = UI_VIEW_FOLDER;
    u.can_write = true, u.menu = UI_CTX_FOLDER;   // (a folder's right-click menu was the last open: the bar menu's rules apply)
    CHECK(key_act(&u, chord_ev(UKC_F1 + 4, 0)).kind == UA_NEWFILE);
    keys_set(1, "F5 side-tree");
    u = menu_ui(5);
    CHECK(key_act(&u, chord_ev(UKC_F1 + 4, 0)).kind == UA_SIDE_MODE && u.side_mode == SIDE_TREE);
    // Close Other Tabs (Michael, 2026-11-08): the tab menu's, on the current tab; with one tab, nothing.
    keys_set(1, "Ctrl+Alt+W tab-others");
    u = menu_ui(5);
    u.tab_n = 3;
    CHECK(key_act(&u, chord_ev('w', UI_MOD_CTRL | UI_MOD_ALT)).kind == UA_TAB_OTHERS && strcmp(ui_menu_key(UM_TAB_OTHERS), "Ctrl+Alt+W") == 0);
    u.tab_n = 1;
    CHECK(key_act(&u, chord_ev('w', UI_MOD_CTRL | UI_MOD_ALT)).kind == UA_NONE);
    // A line ofx can't read is skipped, the first named, the rest apply.
    r = keys_set(4, "Ctrl+E", "Ctrl+E nosuch", "Shift+q reload", "Ctrl+E reload");
    CHECK(r.bad == 1 && strcmp(r.why, "key wants a key, then a command") == 0 && chord_ev('e', UI_MOD_CTRL).cmd == UK_RELOAD);
    r = keys_set(2, "Ctrl+E nosuch", "Ctrl+E reload terminal");
    CHECK(r.bad == 1 && strcmp(r.why, "no command nosuch") == 0);
    r = keys_set(1, "Ctrl+E reload terminal");
    CHECK(r.bad == 1 && strcmp(r.why, "key wants a key, then a command") == 0);
    r = keys_set(1, "Shift+q reload");
    CHECK(r.bad == 1 && strstr(r.why, "write the character"));

    // The written file's key lines: every key as ofx has it, and the commands with none; all of
    // them uncommented read back as exactly the keys as ofx has them.
    static char text[8192];
    size_t n = ui_keys_conf_text(text, sizeof text);
    CHECK(n > 0 && n < sizeof text - 1);
    static char line[CONF_KEYS_MAX][64];
    static int no[CONF_KEYS_MAX];
    int k = 0;
    for (const char *p = text; *p && k < CONF_KEYS_MAX;) {
        const char *e = strchr(p, '\n');
        if (strncmp(p, "#key = ", 7) == 0) snprintf(line[k], sizeof line[k], "%.*s", (int)(e - p - 7), p + 7), no[k] = k + 1, k++;
        p = e + 1;
    }
    CHECK(k == COUNT(UI_BINDS_DEFAULT));
    r = ui_binds_set(line, no, k);
    CHECK(!r.bad && !r.clashes);
    CHECK(ui_binds.n == COUNT(UI_BINDS_DEFAULT));
    for (int i = 0; i < ui_binds.n && i < COUNT(UI_BINDS_DEFAULT); i++) CHECK(ui_chord_eq(ui_binds.b[i].c, UI_BINDS_DEFAULT[i].c) && ui_binds.b[i].cmd == UI_BINDS_DEFAULT[i].cmd);
    for (int i = 0; i < COUNT(UI_CMDS); i++)
        if (UI_CMDS[i].cmd >= UK_ITEM0) CHECK(strstr(text, UI_CMDS[i].name) != NULL);
    char small[16];
    CHECK(ui_keys_conf_text(small, sizeof small) == sizeof small - 1);
    // ofx.conf keeps each key line and its line in the file for ui/; past 256, a line it skips.
    static Conf cf;
    conf_parse(S("previews = on\n\nkey = Ctrl+E terminal\n# x\nkey = none trash\n"), &cf);
    CHECK(!cf.bad && cf.nkey == 2 && strcmp(cf.key[0], "Ctrl+E terminal") == 0 && cf.key_no[0] == 3 && cf.key_no[1] == 5);
    static char many[300 * 24];
    size_t m = 0;
    for (int i = 0; i < 300; i++) m += (size_t)snprintf(many + m, sizeof many - m, "key = F%d about\n", i % 25 + 1);
    conf_parse(str_from_c(many), &cf);
    CHECK(cf.nkey == CONF_KEYS_MAX && cf.bad == CONF_KEYS_MAX + 1 && strcmp(cf.why, "key wants a key, then a command") == 0);
    // none for every command: no key does anything (not the defaults back).
    for (k = 0; k < COUNT(UI_CMDS); k++) snprintf(line[k], sizeof line[k], "none %s", UI_CMDS[k].name), no[k] = k + 1;
    r = ui_binds_set(line, no, k);
    CHECK(!r.bad && ui_binds.n == 0 && chord_ev('t', UI_MOD_CTRL).cmd == UK_NONE && chord_ev(UKC_UP, 0).cmd == UK_NONE && strcmp(ui_menu_key(UM_NEW_TAB), "") == 0);
    // No lines: the keys as ofx has them again, none yours.
    r = ui_binds_set(line, no, 0);
    CHECK(!r.bad && r.changed && strcmp(ui_menu_key(UM_NEW_TAB), "Ctrl+T") == 0 && !ui_binds.user[0]);
}

// Keyboard Shortcuts after F15 step 3: each command has its row, with its keys as they are (yours
// marked) and its name, which the filter finds.
static void test_keys_panel(void)
{
    for (int i = 0; i < COUNT(UI_CMDS); i++) {
        int rows = 0;
        for (int r = 0; r < UI_KEYS_N; r++) rows += UI_KEYS[r].cmd == UI_CMDS[i].cmd;
        if (!rows) fprintf(stderr, "no shortcuts row for %s\n", UI_CMDS[i].name);
        CHECK(rows >= 1);
    }
    for (int r = 0; r < UI_KEYS_N; r++) {
        if (!UI_KEYS[r].cmd) {
            CHECK(UI_KEYS[r].key && UI_KEYS[r].key2 && !ui_keys_name(r)[0]);   // (fixed keys: written out)
            continue;
        }
        CHECK(strcmp(ui_keys_name(r), ui_cmd_name(UI_KEYS[r].cmd)) == 0);
        if (UI_KEYS[r].key) {   // (a key of the row's own first, then its command's: the job rows' Esc, then Ctrl+J)
            CHECK(strcmp(ui_keys_key(r, 0), UI_KEYS[r].key) == 0 && !UI_KEYS[r].key2 && strcmp(ui_keys_key(r, 1), ui_cmd_key(UI_KEYS[r].cmd, 0)) == 0);
            continue;
        }
        CHECK(strcmp(ui_keys_key(r, 0), ui_cmd_key(UI_KEYS[r].cmd, 0)) == 0);
        CHECK(UI_KEYS[r].key2 || strcmp(ui_keys_key(r, 1), ui_cmd_key(UI_KEYS[r].cmd, 1)) == 0);
        CHECK(!ui_keys_mine(r, 0) && !ui_keys_mine(r, 1));   // (the keys as ofx has them: none yours)
    }
    // Yours marked, and the row follows: Terminal Here shows Ctrl+E (yours) then Alt+Enter.
    keys_set(1, "Ctrl+E terminal");
    int term = -1;
    for (int r = 0; r < UI_KEYS_N; r++)
        if (UI_KEYS[r].cmd == UK_TERMINAL) term = r;
    CHECK(term >= 0 && strcmp(ui_keys_key(term, 0), "Ctrl+E") == 0 && ui_keys_mine(term, 0));
    CHECK(strcmp(ui_keys_key(term, 1), "Alt+Enter") == 0 && !ui_keys_mine(term, 1));
    // A hint where a second key would be is never yours (Back: Mouse back).
    keys_set(2, "F7 back", "F8 back");
    for (int r = 0; r < UI_KEYS_N; r++)
        if (UI_KEYS[r].cmd == UK_BACK) CHECK(ui_keys_mine(r, 0) && strcmp(ui_keys_key(r, 1), "Mouse back") == 0 && !ui_keys_mine(r, 1));
    // The job rows' "Back to the files": Esc, then the job rows' key as bound (no key in a heading).
    keys_set(1, "F6 job-rows");
    for (int r = 0; r < UI_KEYS_N; r++)
        if (UI_KEYS[r].sec == KS_JOBS && strcmp(UI_KEYS[r].action, "Back to the files") == 0)
            CHECK(strcmp(ui_keys_key(r, 0), "Esc") == 0 && strcmp(ui_keys_key(r, 1), "F6") == 0 && ui_keys_mine(r, 1) && !ui_keys_mine(r, 0));
    for (int s = 0; s < KS_SECTIONS; s++) CHECK(!strstr(UI_KEY_SECTION[s], "Ctrl+"));
    // A tooltip or hint names a command's first key as it is, or none (the code-health review).
    char tip[64];
    keys_set(1, "F7 back");
    CHECK(strcmp(ui_with_key(tip, sizeof tip, "Back", UK_BACK), "Back (F7)") == 0);   // (yours first)
    keys_set(1, "none back");
    CHECK(strcmp(ui_with_key(tip, sizeof tip, "Back", UK_BACK), "Back") == 0);
    keys_set(0);
    CHECK(strcmp(ui_with_key(tip, sizeof tip, "Back", UK_BACK), "Back (Alt+Left)") == 0);
    // "The menus" row's Alt letters are the menus' own.
    char letters[32] = "Alt+";
    for (int m = 0; m < UI_MENUS; m++) snprintf(letters + strlen(letters), sizeof letters - strlen(letters), "%s%c", m ? ", " : "", UI_MENU[m].letter - 'a' + 'A');
    for (int r = 0; r < UI_KEYS_N; r++)
        if (strcmp(UI_KEYS[r].action, "The menus") == 0) CHECK(strcmp(UI_KEYS[r].key2, letters) == 0);
    // The filter finds a command by its name in ofx.conf.
    Ui u = menu_ui(3);
    u.keys_list.rows = 10;
    menu_keys(&u, 1, UK_SHORTCUTS);
    UiInput in = chars("tab-others");
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    CHECK(u.keys_list.n == 2 && u.keys_line[0] == -1 - KS_TABS && strcmp(UI_KEYS[u.keys_line[1]].action, "Close Other Tabs") == 0);
    // and still by an action's words alone ("selection" is in no name or key).
    while (u.keys_filter.len) menu_keys(&u, 1, UK_BACKSPACE);
    in = chars("selection");
    ui_handle(&u, (UiNames){ name_at, NULL, NULL }, &in);
    bool inv = false;
    for (int v = 0; v < u.keys_list.n; v++) inv |= u.keys_line[v] >= 0 && strcmp(UI_KEYS[u.keys_line[v]].action, "Invert Selection") == 0;
    CHECK(inv);
    keys_set(0);
}

// A character bound to a command, typed into a text field, is the field's text (the F15
// code-health review): the rename and location lines, find, search and Containing, the panels'
// filters and Edit Bookmarks' name; in the files it runs the command.
static void test_keys_in_fields(void)
{
    static const char *const BOUND[] = { "q close-all", "m menus", "k path-buttons", "x context-menu", "z shortcuts", "w new-tab" };
    for (int b = 0; b < COUNT(BOUND); b++) {
        keys_set(1, BOUND[b]);
        u32 c = (u32)BOUND[b][0];
        for (int field = 0; field < 6; field++) {
            Ui u = menu_ui(3);
            u.keys_list.rows = 10;
            int *len = NULL;
            switch (field) {
            case 0: ui_edit_rename(&u, S("abc"), false), key_act(&u, ev_key(UK_END)), len = &u.edit.len; break;   // (End: the name no longer selected)
            case 1: ui_edit_start(&u, S("/tmp/"), true), key_act(&u, ev_key(UK_END)), len = &u.edit.len; break;
            case 2: key_act(&u, ev_key(UK_FIND)), len = &u.find.len; break;
            case 3: u.view = UI_VIEW_SEARCH, u.focus = UI_FOCUS_SEARCH, len = &u.search.len; break;
            case 4: u.view = UI_VIEW_SEARCH, u.search_inside = true, u.focus = UI_FOCUS_INSIDE, len = &u.inside.len; break;
            case 5: key_act(&u, ev_key(UK_SHORTCUTS)), len = &u.keys_filter.len; break;
            }
            int before = *len;
            UiAction a = key_act(&u, ev_ch(c));
            bool ok = a.kind == UA_NONE && *len == before + 1 && !u.menu_on && u.focus != UI_FOCUS_CRUMBS && (field != 5 || u.panel == UI_PANEL_KEYS);
            if (!ok) fprintf(stderr, "'%s' typed into field %d: action %d, length %d (was %d)\n", BOUND[b], field, a.kind, *len, before);
            CHECK(ok);
        }
    }
    // Ctrl+C with nothing selected leaves the find bar for the list's copy; unbound, it stays.
    Ui f = menu_ui(3);
    key_act(&f, ev_key(UK_FIND)), key_act(&f, ev_ch('a'));
    keys_set(1, "none copy");
    CHECK(key_act(&f, ev_key(UK_COPY)).kind == UA_NONE && f.focus == UI_FOCUS_FIND);
    keys_set(0);
    CHECK(key_act(&f, ev_key(UK_COPY)).kind == UA_COPY && f.focus != UI_FOCUS_FIND);
    // A question's answers and an open menu's letters, bound to close-all, are the answer, the
    // menu's (the group A review: y at "Delete for good?" closed every window).
    keys_set(1, "y close-all");
    Ui d = menu_ui(3);
    d.list.sel = 1, d.confirming = true, d.confirm_n = 1;
    UiAction ans = key_act(&d, ev_ch('y'));
    CHECK(ans.kind != UA_CLOSE_ALL && !d.confirming);
    keys_set(1, "s close-all");
    Ui q = menu_ui(3);
    q.asking = true;   // (a clash: s skips)
    CHECK(key_act(&q, ev_ch('s')).kind == UA_ANSWER);
    keys_set(1, "x close-all");
    Ui m = menu_ui(3);
    key_act(&m, ev_key(UK_F10));
    CHECK(m.menu_on && key_act(&m, ev_ch('x')).kind != UA_CLOSE_ALL);
    keys_set(1, "q close-all");   // (and in the files, the command)
    Ui u = menu_ui(3);
    CHECK(key_act(&u, ev_ch('q')).kind == UA_CLOSE_ALL);
    keys_set(0);
}
