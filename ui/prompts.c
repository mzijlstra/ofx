// The path buttons by keyboard (Ctrl+K, F5.2), the yes/no questions and the job rows' keys (F5.3). Included by ui.c.

// ---- the path buttons by keyboard (F5.2) -------------------------------------------------------

static void ui_branch_open(Ui *u, bool keys);

// Ctrl+K: the keyboard on the path buttons, the rightmost shown marked (none there: nothing).
static void ui_crumbs_focus(Ui *u, const UiInput *in)
{
    if (in->crumb_n <= 0) return;
    u->focus = UI_FOCUS_CRUMBS;
    u->crumb_at = in->crumb_last < 0 ? 0 : in->crumb_last >= in->crumb_n ? in->crumb_n - 1 : in->crumb_last;
}

// A key while the path buttons have the keyboard (h and l as Left and Right): true when it was
// theirs (*a: what gui/ does). Right from the last steps on to the branch button when there is
// one (crumb_at == crumb_n, F12 step 5c): Enter, or the menu key, opens its menu.
// Any other key gives the keyboard back to the list and is the list's.
static bool ui_crumbs_event(Ui *u, const UiInput *in, UiEvent e, UiAction *a)
{
    *a = UI_NONE;
    e = ui_vi_key(e);   // (as in the list, Michael, 2026-10-21; j and k go on to it)
    if (e.cmd == UK_CRUMBS) return true;
    switch (ui_unshift(e.key)) {
    case UK_LEFT: if (u->crumb_at > 0) u->crumb_at--; return true;
    case UK_RIGHT: if (u->crumb_at < in->crumb_n - 1 + u->branch_on) u->crumb_at++; return true;
    case UK_HOME: u->crumb_at = 0; return true;
    case UK_END: u->crumb_at = in->crumb_n - 1 + u->branch_on; return true;   // (the branch button last, as Right reaches it)
    case UK_ENTER:
        if (u->crumb_at >= in->crumb_n) return ui_branch_open(u, true), true;   // (Esc back on the button)
        u->focus = UI_FOCUS_LIST, *a = (UiAction){ UA_CRUMB, u->crumb_at, false };
        return true;
    case UK_SHIFT_ENTER: if (u->crumb_at < in->crumb_n) *a = (UiAction){ UA_TAB_BG_CRUMB, u->crumb_at, false }; return true;
    case UK_ESC: case UK_TAB: u->focus = UI_FOCUS_LIST; return true;
    default: u->focus = UI_FOCUS_LIST; return false;
    }
}

// ---- the prompts -------------------------------------------------------------------------------

// A yes-or-no question (ui_is_question): Enter (Shift doesn't change it) and y say yes, n and Esc
// no; *answered when either.
static bool ui_yes_no(UiEvent e, bool *answered)
{
    bool yes = e.key == UK_ENTER || e.key == UK_SHIFT_ENTER || (e.key == UK_NONE && (e.ch == 'y' || e.ch == 'Y'));
    bool no = e.key == UK_ESC || (e.key == UK_NONE && (e.ch == 'n' || e.ch == 'N'));
    *answered = yes || no;
    return yes;
}

// The yes/no questions: delete for good, cancel a job, quit with jobs, Empty Trash, Bring back
// (F11), Clear Previews (F12).
static bool ui_is_question(UiOnTop on)
{
    return on == UI_ON_QUIT || on == UI_ON_CANCEL || on == UI_ON_DELETE || on == UI_ON_EMPTY || on == UI_ON_RESTORE || on == UI_ON_CLEAR;
}

// Question on answered, by a button or a key: it goes, and what it asks for comes back (yes; Bring
// back's no forgets the windows, so it is an action too). Both routes come here (F11 review,
// 2026-11-02: each cleared its flag in its own words, and the trash view gate was on one only).
static UiAction ui_answer(Ui *u, UiOnTop on, bool yes)
{
    switch (on) {
    case UI_ON_QUIT: u->quit_asking = false; return (UiAction){ yes ? UA_QUIT : UA_NONE, 0, false };
    case UI_ON_CANCEL: u->cancel_asking = false; return (UiAction){ yes ? UA_CANCEL_JOB : UA_NONE, u->cancel_id, false };
    case UI_ON_DELETE: u->confirming = false; return (UiAction){ yes ? UA_DELETE : UA_NONE, 0, false };
    case UI_ON_EMPTY: u->empty_asking = false; return (UiAction){ yes ? UA_EMPTY_TRASH : UA_NONE, 0, false };   // (from any view: the folder menu's on Trash, F8)
    case UI_ON_RESTORE: u->restore_n = 0; return (UiAction){ UA_RESTORE_WINDOWS, 0, yes };
    case UI_ON_CLEAR: u->clear_asking = false; return (UiAction){ yes ? UA_CLEAR_PREVIEWS : UA_NONE, 0, false };
    default: return UI_NONE;
    }
}

// A key or character while a clash waits: s skip, r replace (a folder: merge), k keep both, the
// capitals for all the rest; Esc cancels the job.
static UiAction ui_ask_event(Ui *u, UiEvent e)
{
    if (e.key == UK_ESC) return u->asking = false, (UiAction){ UA_ANSWER, UI_ANS_CANCEL, false };
    if (e.key != UK_NONE) return UI_NONE;
    int a = 0;
    switch (e.ch) {
    case 's': case 'S': a = UI_ANS_SKIP; break;
    case 'r': case 'R': a = UI_ANS_REPLACE; break;
    case 'k': case 'K': a = UI_ANS_KEEP; break;
    default: return UI_NONE;
    }
    u->asking = false;
    return (UiAction){ UA_ANSWER, a, (e.ch >= 'A' && e.ch <= 'Z') || u->ask_all };
}

// The job rows by keyboard: true when the key was theirs (*a: what gui/ does). Up, Down, k and j
// move the mark, Enter asks to cancel the marked job; any other key gives the keyboard back to the
// list and is the list's.
static bool ui_jobs_event(Ui *u, const UiInput *in, UiEvent e)
{
    e = ui_vi_key(e);
    if (e.cmd == UK_JOBS) return u->focus = UI_FOCUS_LIST, true;   // the third Ctrl+J: back to the list (list, find bar, jobs)
    switch (e.key) {
    case UK_UP: if (u->job_at > 0) u->job_at--; return true;
    case UK_DOWN: if (u->job_at < in->job_n - 1) u->job_at++; return true;
    case UK_ENTER: u->cancel_asking = true, u->cancel_id = in->job_id[u->job_at]; return true;
    case UK_ESC: case UK_TAB: u->focus = UI_FOCUS_LIST; return true;
    default: u->focus = UI_FOCUS_LIST; return false;
    }
}
