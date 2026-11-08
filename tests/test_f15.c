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
// Each Keyboard Shortcuts row's two keys.
static const struct { const char *key, *key2; } PIN_PANEL[] = {
    { "Up, Down", "k, j" },
    { "PgUp, PgDn", "" },
    { "Home, End", "g, G" },
    { "Alt+Left", "Mouse back" },
    { "Alt+Right", "Mouse forward" },
    { "Alt+Up", "Backspace" },
    { "Alt+Home", "~" },
    { "Alt+End", "" },
    { "Ctrl+L", "Ctrl+Right" },
    { "Enter", "" },
    { "Shift+Enter", "" },
    { "Alt+Enter", "t" },
    { "Ctrl+I", "" },
    { "Space", "" },
    { "Shift+arrows", "H, J, K, L" },
    { "Ctrl+A", "" },
    { "Ctrl+Shift+A", "" },
    { "Ctrl+Shift+I", "" },
    { "Esc", "" },
    { "Ctrl+X", "" },
    { "Ctrl+C", "" },
    { "Ctrl+Shift+C", "" },
    { "Ctrl+V", "" },
    { "Ctrl+Z", "" },
    { "Ctrl+M", "" },
    { "F2", "" },
    { "Ctrl+Shift+N", "" },
    { "Delete", "" },
    { "Shift+Delete", "" },
    { "r", "" },
    { "Ctrl+D", "" },
    { "Ctrl+B", "" },
    { "s, r, k", "S, R, K the rest" },
    { "Ctrl+T", "" },
    { "Ctrl+Shift+T", "" },
    { "Ctrl+N", "" },
    { "Ctrl+W", "" },
    { "Ctrl+Tab", "Ctrl+PgDn" },
    { "Ctrl+Shift+Tab", "Ctrl+PgUp" },
    { "Ctrl+Shift+PgUp", "" },
    { "Ctrl+Shift+PgDn", "" },
    { "Alt+1 \xE2\x80\xA6 Alt+9", "" },
    { "Middle click", "" },
    { "Ctrl+Q", "" },
    { "F10", "Alt+F, E, V, G, B, H" },
    { "Shift+F10", "Menu" },
    { "F1", "Ctrl+F1" },
    { "Ctrl+R", "" },
    { "Ctrl+H", "." },
    { "Ctrl+P", "" },
    { "Ctrl+G", "" },
    { "s", "a column's header" },
    { "S", "" },
    { "Ctrl+1", "" },
    { "Ctrl+2", "" },
    { "Ctrl+=", "Ctrl+wheel" },
    { "Ctrl+-", "" },
    { "Ctrl+0", "" },
    { "Tab", "Ctrl+Left" },
    { "F9", "Ctrl+S" },
    { "Enter", "" },
    { "Left, Right", "h, l" },
    { "Ctrl+K", "Ctrl+Up; in a search, the search field first" },
    { "Left, Right", "h, l" },
    { "Enter", "" },
    { "Shift+Enter", "" },
    { "Esc", "" },
    { "Ctrl+F", "/" },
    { "Up, Down", "the chevrons" },
    { "Enter", "" },
    { "Esc", "" },
    { "n, N", "" },
    { "Alt+C", "" },
    { "Alt+W", "" },
    { "Alt+R", "" },
    { "Ctrl+Shift+F", "the magnifier" },
    { "Ctrl+Enter", "" },
    { "Ctrl+Shift+Enter", "a result found inside files, in the editor" },
    { "Enter", "Esc, Down" },
    { "Ctrl+Shift+F", "a click in it" },
    { "*, ?", "*.ts" },
    { "Alt+C, W, R", "the name's shared with find; Containing's its own" },
    { "Alt+I", "the In files button" },
    { "Tab", "" },
    { "Shift+Left, Right", "Shift+Home, End" },
    { "Ctrl+Left, Right", "" },
    { "Ctrl+Shift+Left, Right", "" },
    { "Ctrl+Backspace", "Ctrl+Delete" },
    { "Ctrl+A", "" },
    { "Ctrl+C, X, V", "" },
    { "Enter", "" },
    { "Esc", "" },
    { "Ctrl+J", "Ctrl+Down" },
    { "Up, Down", "k, j" },
    { "Enter", "its \xC3\x97" },
    { "Esc", "Ctrl+J" },
    { "Tab", "Down" },
    { "Alt+D", "" },
    { "Enter", "a double click" },
    { "Delete", "" },
    { "F2", "" },
    { "Alt+Up, Alt+Down", "" },
    { "Enter", "a double click" },
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

    // The menus: each entry's key as before, and what picking it runs, the same event.
    for (int it = 0; it < UM_ITEMS; it++) {
        UiEvent e = ui_menu_runs((UiMenuItem)it);
        bool ok = strcmp(ui_menu_key((UiMenuItem)it), PIN_MENU[it].key) == 0 && e.ch == PIN_MENU[it].ch &&
                  (PIN_MENU[it].uk || PIN_MENU[it].ch ? legacy_key(e) == PIN_MENU[it].uk || (PIN_MENU[it].uk == UK_NONE && e.key == UK_NONE)
                                                      : !e.key && !e.cmd);
        if (!ok) fprintf(stderr, "menu entry %s: %s %d,%u (was %s %d,%u)\n", UI_MENU_ITEM[it].word, ui_menu_key((UiMenuItem)it), legacy_key(e), e.ch, PIN_MENU[it].key, PIN_MENU[it].uk, PIN_MENU[it].ch);
        CHECK(ok);
        CHECK(!UI_MENU_ITEM[it].runs || e.cmd == UI_MENU_ITEM[it].cmd);
    }
    // The shortcuts panel: every row's keys as before.
    CHECK(COUNT(PIN_PANEL) == UI_KEYS_N);
    for (int r = 0; r < UI_KEYS_N && r < COUNT(PIN_PANEL); r++) {
        bool ok = strcmp(ui_keys_key(r, 0), PIN_PANEL[r].key) == 0 && strcmp(ui_keys_key(r, 1), PIN_PANEL[r].key2) == 0;
        if (!ok) fprintf(stderr, "shortcuts row %s: %s / %s (was %s / %s)\n", UI_KEYS[r].action, ui_keys_key(r, 0), ui_keys_key(r, 1), PIN_PANEL[r].key, PIN_PANEL[r].key2);
        CHECK(ok);
    }
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
        CHECK(ui_cmd_key(UI_CMDS[i].cmd, 0)[0]);
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
