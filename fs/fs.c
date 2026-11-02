// ofx's file system side, raylib-free and unit-tested: the folder, MIME types, the icon theme, the
// Omarchy theme, the places, launching, changing files (F3), the trash, copy and move jobs, search,
// thumbnails, bookmarks, ofx.conf, and what ofx remembers (the state file: folders, tabs). Unity
// build: included once by each binary, after base.
#include "paths.c"
#include "list.c"
#include "mime.c"
#include "icons.c"
#include "theme.c"
#include "places.c"
#include "spawn.c"
#include "editors.c"
#include "conf.c"     // (after editors.c: an editor line is checked as a recipe)
#include "ops.c"
#include "apps.c"     // (after ops.c: Set as default writes through ops_allowed)
#include "trash.c"
#include "clip.c"
#include "job.c"
#include "props.c"
#include "search.c"
#include "undo.c"
#include "jobq.c"
#include "state.c"
#include "tree.c"
#include "sandbox.c"   // (before git.c and thumbers.c: they run in it)
#include "git.c"
#include "thumbers.c"   // (before thumb.c: it runs them)
#include "thumb.c"
#include "bookmarks.c"
#include "clean.c"
#include "folders.c"   // (after clean.c: its drive rules)
#include "fm1.c"
#include "windows.c"
#include "session.c"   // (after windows.c: a running window by its start time)
