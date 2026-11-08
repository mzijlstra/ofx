// GLFW's keys as ui/'s chords (F15, ui/keys.c), and the event one key callback sends. Apart from
// input.c so the unit tests take it in (GLFW's header only): tests/test_f15.c pins every key and
// modifier to what ofx did before F15. Included by input.c.

// A GLFW key and its modifiers as a chord; code 0 for a key ui/ has no name for (Shift itself).
static UiChord glfw_chord(int key, int mods)
{
    u8 m = (u8)((mods & GLFW_MOD_CONTROL ? UI_MOD_CTRL : 0) | (mods & GLFW_MOD_SHIFT ? UI_MOD_SHIFT : 0) |
                (mods & GLFW_MOD_ALT ? UI_MOD_ALT : 0) | (mods & GLFW_MOD_SUPER ? UI_MOD_SUPER : 0));
    u32 c = 0;
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) c = (u32)('a' + key - GLFW_KEY_A);
    else if (key >= GLFW_KEY_SPACE && key <= GLFW_KEY_GRAVE_ACCENT) c = (u32)key;   // (GLFW's are the US layout's characters)
    else if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25) c = UKC_F1 + (u32)(key - GLFW_KEY_F1);
    else if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) c = UKC_KP0 + (u32)(key - GLFW_KEY_KP_0);
    else {
        static const struct { int glfw; u32 code; } SPECIAL[] = {
            { GLFW_KEY_ESCAPE, UKC_ESC }, { GLFW_KEY_ENTER, UKC_ENTER }, { GLFW_KEY_KP_ENTER, UKC_ENTER },   // (the keypad's Enter is Enter)
            { GLFW_KEY_TAB, UKC_TAB }, { GLFW_KEY_BACKSPACE, UKC_BACKSPACE }, { GLFW_KEY_INSERT, UKC_INSERT },
            { GLFW_KEY_DELETE, UKC_DELETE }, { GLFW_KEY_RIGHT, UKC_RIGHT }, { GLFW_KEY_LEFT, UKC_LEFT }, { GLFW_KEY_DOWN, UKC_DOWN },
            { GLFW_KEY_UP, UKC_UP }, { GLFW_KEY_PAGE_UP, UKC_PGUP }, { GLFW_KEY_PAGE_DOWN, UKC_PGDN }, { GLFW_KEY_HOME, UKC_HOME },
            { GLFW_KEY_END, UKC_END }, { GLFW_KEY_MENU, UKC_MENU }, { GLFW_KEY_KP_DECIMAL, UKC_KP_DECIMAL },
            { GLFW_KEY_KP_DIVIDE, UKC_KP_DIVIDE }, { GLFW_KEY_KP_MULTIPLY, UKC_KP_MULTIPLY }, { GLFW_KEY_KP_SUBTRACT, UKC_KP_SUBTRACT },
            { GLFW_KEY_KP_ADD, UKC_KP_ADD }, { GLFW_KEY_KP_EQUAL, UKC_KP_EQUAL },
        };
        for (int i = 0; i < COUNT(SPECIAL); i++)
            if (SPECIAL[i].glfw == key) c = SPECIAL[i].code;
    }
    return (UiChord){ c, c ? m : 0 };
}

// What a key callback sends ui/ (action GLFW_PRESS or GLFW_REPEAT; none with neither a key's own
// meaning nor a command). Ctrl+Q's command, and Alt with a letter (a menu) or a digit (a tab),
// once a press: not again while the key is held.
static UiEvent glfw_event(int key, int mods, int action)
{
    if (action == GLFW_RELEASE) return (UiEvent){ UK_NONE, 0, UK_NONE };
    UiEvent e = ui_event_of(glfw_chord(key, mods));   // (a key with no name means nothing)
    if (action != GLFW_PRESS && (e.cmd == UK_QUIT_ALL || e.key == UK_MENU || e.key == UK_TAB_JUMP)) return (UiEvent){ UK_NONE, 0, UK_NONE };
    return e;
}
