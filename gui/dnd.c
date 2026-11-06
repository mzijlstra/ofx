// Drag and drop with other programs (docs/plan.md, F14). GLFW's own data device carries the drags
// (Hyprland sends a drag to a client's first data device only, and that is GLFW's), and the
// patched GLFW hands them to this file (glfwSetWaylandDragHook, vendor/patches/glfw-dnd.patch)
// instead of reading a drop itself. Its events run on the main thread, inside GLFW's dispatch.
// A registry of ofx's own on GLFW's connection binds a data device manager (ofx's sources) and the
// seat, and a pointer once the seat has one: a drag out needs the serial of the press that holds
// the button.
//
// What can block runs on a short thread of its own, so the window keeps answering: the read of
// another program's drag, as it enters (the other program writes it in its own time) and each write of a source of ours (the other
// program reads it in its own). A drop of ofx's own drag back on its window isn't read: its offer
// carries a type naming this process (own_mime), its files are the drag's already, and it drops
// as a drag inside the window does (F13; the spike, reading it on the main thread, waited on
// itself).
// Included by main.c.

#include <linux/input-event-codes.h>
#include <sys/mman.h>

#define GLFW_EXPOSE_NATIVE_WAYLAND
#include "GLFW/glfw3native.h"

enum { DND_OFFERS = 8, DND_READ_MAX = MiB(1), DND_WAIT_MS = 2000 };
enum { DND_TYPE_URIS = 1, DND_TYPE_TEXT = 2, DND_TYPE_OWN = 4 };
typedef enum { DND_OUT_NONE, DND_OUT_DRAGGING, DND_OUT_DROPPED, DND_OUT_DONE, DND_OUT_CANCELLED } DndOut;
static const char *const DND_OUT_WORD[] = { "none", "dragging", "dropped", "done", "cancelled" };

static char dnd_read_mem[DND_READ_MAX];   // a drop's text/uri-list, written by dnd_reader alone while dnd.reading
static u8   dnd_got_mem[MiB(2)];          // dnd.got's paths
static u8   dnd_src_mem[MiB(2)];          // our source's payloads, read by the writers alone while dnd.writers

static struct {
    bool ok;                              // the hook is set: drags reach ofx
    struct wl_display *dpy;
    struct wl_registry *reg;
    struct wl_seat *seat;
    struct wl_pointer *ptr;
    struct wl_data_device_manager *mgr;
    struct wl_compositor *comp;           // the drag image's surface (version 3 for its scale)
    struct wl_shm *shm;                   // and its buffer
    struct wl_data_device *dev;           // GLFW's
    struct wl_surface *surf;              // GLFW's window
    char own_mime[64];                    // "application/x-ofx-drag-<pid>": another ofx window's is another program's
    bool changed;                         // something a frame shows changed (dnd_poll)

    // Every offer announced on the device, its types and the source's actions: Hyprland announces a
    // clipboard offer on every keyboard focus change, through the same device, so the types belong
    // to an offer, never to "the latest". Slots of offers never entered are only forgotten: those are
    // GLFW's (the selection) to destroy.
    // Its action too: Hyprland sends an offer's action before the enter (2026-11-05, Michael's log),
    // so one kept only for the offer entered was lost, and its drop read as refused.
    struct { struct wl_data_offer *o; unsigned types; uint32_t actions, action; u32 age; } offers[DND_OFFERS];
    u32 ages;
    int over;                             // the slot entered, -1 for none: a drag over the window
    uint32_t serial;                      // its enter's
    float x, y;                           // where it is, in the surface's logical pixels
    uint32_t action;                      // the action the compositor settled on (offer.action)

    // Our own drag over the window (F14 step 2): what the window answers (dnd_own_answer), and
    // its drop, once, for the window to drop as a drag inside it does (dnd_own_dropped).
    bool own_ok, own_move, own_dropped;
    float own_x, own_y;

    // A drag of files from another program (F14 step 3): its text/uri-list read as it enters, so
    // the files are known before the drop (Nemo's rule asks their drive, a folder can't go into
    // itself), on a thread of its own (dnd_reader), DND_WAIT_MS at most. in_offer is the offer the
    // read is of: the one entered, or dropped and waiting for its read; in_id names it for the window
    // (its drag is that offer's), and read_id tells a read for an offer a leave took (whose address
    // can come back) from one for the offer now.
    struct wl_data_offer *in_offer;
    u32 in_id, read_id;
    bool reading;                         // a read's thread runs, one at a time: the offer entered meanwhile is read next (dnd_read_next)
    bool in_read;                         // in_offer's read is done: got holds its files (none: no files, a web link)
    bool in_dropped;                      // in_offer was dropped: dnd_in_dropped hands it to the window
    bool in_ok;                           // the window's answer (dnd_in_answer), the action it takes
    uint32_t in_act;
    bool in_took;                         // at the drop, the compositor had been told the window takes it
    uint32_t in_drop_actions, in_drop_c;  // the dropped offer's source actions, the compositor's action then
    float in_x, in_y;                     // where it was dropped
    int read_fd;
    isize read_n;
    bool read_whole;                      // to its end within DND_WAIT_MS and DND_READ_MAX
    atomic_bool read_done;
    Arena gota;
    ClipFiles got;                        // in_offer's files
    int drops, own_drops, drops_failed;   // for the scripts' state line

    // Our drag out (dnd_start).
    struct wl_data_source *src;
    uint32_t press;                       // the serial of the left press holding the button, 0 for none
    Arena srca;
    Str payload[CLIP_FMTS];
    atomic_int writers;                   // writer threads still reading payload
    atomic_llong sent;                    // bytes the writers wrote, for the scripts
    atomic_int wrote;                     // writers ended, for the scripts
    DndOut out;
    uint32_t out_action;                  // the action the target took (source.action)
    struct wl_surface *icon;              // the drag image, while our drag goes
    struct wl_buffer *icon_buf;
} dnd;   // (no initializer, so in .bss rather than 33 KB of .data: dnd_init sets the -1s)

static bool dnd_debug;   // OFX_GUI_DEBUG: every drag event on stderr, as input.c's keys and buttons
#define DND_LOG(...) (dnd_debug ? fprintf(stderr, "ofx: dnd " __VA_ARGS__), fputc('\n', stderr) : 0)

static int dnd_slot(struct wl_data_offer *o)
{
    for (int i = 0; o && i < DND_OFFERS; i++)
        if (dnd.offers[i].o == o) return i;
    return -1;
}

static void dnd_offer_type(void *data, struct wl_data_offer *o, const char *mime)
{
    (void)data;
    int i = dnd_slot(o);
    DND_LOG("offer %p slot %d type %s", (void *)o, i, mime);
    if (i < 0) return;
    if (strcmp(mime, CLIP_MIME_URIS) == 0) dnd.offers[i].types |= DND_TYPE_URIS;
    else if (strcmp(mime, CLIP_MIME_TEXT[0]) == 0 || strcmp(mime, CLIP_MIME_TEXT[1]) == 0) dnd.offers[i].types |= DND_TYPE_TEXT;
    else if (strcmp(mime, dnd.own_mime) == 0) dnd.offers[i].types |= DND_TYPE_OWN;
}
static void dnd_offer_actions(void *data, struct wl_data_offer *o, uint32_t a)
{
    (void)data;
    int i = dnd_slot(o);
    DND_LOG("offer %p slot %d source actions %u", (void *)o, i, a);
    if (i >= 0) dnd.offers[i].actions = a;
}
static void dnd_offer_action(void *data, struct wl_data_offer *o, uint32_t a)
{
    (void)data;
    int i = dnd_slot(o);
    DND_LOG("offer %p slot %d action %u", (void *)o, i, a);
    if (i >= 0) dnd.offers[i].action = a;
    if (i >= 0 && i == dnd.over) dnd.action = a, dnd.changed = true;
}
static const struct wl_data_offer_listener dnd_offer_l = { dnd_offer_type, dnd_offer_actions, dnd_offer_action };

// A new offer: its own slot (a slot of the same address is one GLFW destroyed: the address came
// back), else a free one, else the oldest one not entered.
static void dnd_data_offer(void *data, struct wl_data_device *d, struct wl_data_offer *o)
{
    (void)data, (void)d;
    int i = dnd_slot(o);
    for (int k = 0; i < 0 && k < DND_OFFERS; k++)
        if (!dnd.offers[k].o) i = k;
    if (i < 0)
        for (int k = 0; k < DND_OFFERS; k++)
            if (k != dnd.over && (i < 0 || dnd.offers[k].age < dnd.offers[i].age)) i = k;
    dnd.offers[i].o = o, dnd.offers[i].types = 0, dnd.offers[i].actions = dnd.offers[i].action = 0, dnd.offers[i].age = ++dnd.ages;
    DND_LOG("data_offer %p slot %d", (void *)o, i);
}

static void dnd_forget(int i)
{
    if (i >= 0) dnd.offers[i].o = NULL;
}

// What the drag over the window is told, as the window answers it: ofx's own drag back by its own
// type (dnd_own_answer), another program's files once they are read (dnd_in_answer), by the target
// under the pointer and its copy or move; anything else refused.
static void dnd_answer(void)
{
    int i = dnd.over;
    if (i < 0) return;
    struct wl_data_offer *o = dnd.offers[i].o;
    unsigned t = dnd.offers[i].types;
    if (t & DND_TYPE_OWN) {   // ofx's own: as the window answers (the target under the pointer, its copy or move)
        enum { C = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY, M = WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE };
        wl_data_offer_accept(o, dnd.serial, dnd.own_ok ? dnd.own_mime : NULL);
        wl_data_offer_set_actions(o, dnd.own_ok ? C | M : 0, dnd.own_ok ? (dnd.own_move ? M : C) : 0);
        return;
    }
    bool ok = o == dnd.in_offer && dnd.in_read && dnd.got.n > 0 && dnd.in_ok;
    wl_data_offer_accept(o, dnd.serial, ok ? CLIP_MIME_URIS : NULL);
    wl_data_offer_set_actions(o, ok ? dnd.in_act : 0, ok ? dnd.in_act : 0);
}

// Another program's files over the window, read, and where they are, what its source allows
// (WL_DATA_DEVICE_MANAGER_DND_ACTION_*) and which offer they are (id: another id is another drag);
// NULL while there are none (no drag, not read yet, not files).
static const ClipFiles *dnd_in_over(float *x, float *y, uint32_t *actions, u32 *id)
{
    if (dnd.over < 0 || dnd.offers[dnd.over].o != dnd.in_offer || !dnd.in_read || dnd.got.n == 0) return NULL;
    *x = dnd.x, *y = dnd.y, *actions = dnd.offers[dnd.over].actions, *id = dnd.in_id;
    return &dnd.got;
}

// Another program's drag over the window that holds no files (text, a web address: refused), and
// where: the window says so, as Hyprland's pointer doesn't change for a refused drag.
static bool dnd_in_nofiles(float *x, float *y)
{
    if (dnd.over < 0) return false;
    unsigned t = dnd.offers[dnd.over].types;
    bool none = !(t & DND_TYPE_OWN) && (!(t & DND_TYPE_URIS) || (dnd.offers[dnd.over].o == dnd.in_offer && dnd.in_read && dnd.got.n == 0));
    if (none) *x = dnd.x, *y = dnd.y;
    return none;
}

// What the window says to another program's files over it: ok and the action it takes (copy or
// move); told the compositor when it changes.
static void dnd_in_answer(bool ok, uint32_t action)
{
    if (ok == dnd.in_ok && action == dnd.in_act) return;
    dnd.in_ok = ok, dnd.in_act = action;
    DND_LOG("answer: %s %u", ok ? "takes it" : "refused", action);
    if (dnd.over >= 0 && dnd.offers[dnd.over].o == dnd.in_offer) dnd_answer(), wl_display_flush(dnd.dpy);
}

// Another program's drop, once its read is done (it may end after the drop: the window then takes
// the files only now): its files (none: nothing to drop), where, what its source allows, the action
// the compositor settled on (0: none), and which offer (as dnd_in_over). The window then calls
// dnd_in_finish.
static const ClipFiles *dnd_in_dropped(float *x, float *y, uint32_t *actions, uint32_t *action, u32 *id)
{
    if (!dnd.in_dropped || !dnd.in_read) return NULL;
    *x = dnd.in_x, *y = dnd.in_y, *actions = dnd.in_drop_actions, *action = dnd.in_drop_c, *id = dnd.in_id;
    return &dnd.got;
}

static void dnd_read_next(void);

// The drop dealt with: told the source it is done (done: the files are taken, as a job; only when
// the compositor had been told the window takes them, as finishing a drop it refused is untimely),
// and given back.
static void dnd_in_finish(bool done)
{
    if (!dnd.in_dropped) return;
    if (done && dnd.in_took) wl_data_offer_finish(dnd.in_offer);
    wl_data_offer_destroy(dnd.in_offer);
    wl_display_flush(dnd.dpy);
    DND_LOG("drop %s", done ? "taken" : "given back");
    if (done) dnd.drops++;
    else dnd.drops_failed++;
    dnd.in_offer = NULL, dnd.in_dropped = false, dnd.changed = true;
    dnd_read_next();   // (the offer entered while it waited)
}

// Whether our own drag is over the window, and where (the surface's logical pixels).
static bool dnd_own_over(float *x, float *y)
{
    if (dnd.over < 0 || !(dnd.offers[dnd.over].types & DND_TYPE_OWN)) return false;
    *x = dnd.x, *y = dnd.y;
    return true;
}

// What the window says to our own drag over it: whether the target under it takes the files, and
// as a move or a copy; told the compositor when it changes, for its pointer.
static void dnd_own_answer(bool ok, bool move)
{
    if (ok == dnd.own_ok && move == dnd.own_move) return;
    dnd.own_ok = ok, dnd.own_move = move;
    DND_LOG("own answer: %s%s", ok ? "takes it" : "refused", ok && move ? ", a move" : "");
    float x, y;
    if (dnd_own_over(&x, &y)) dnd_answer(), wl_display_flush(dnd.dpy);
}

// Our own drag dropped on the window, once: where.
static bool dnd_own_dropped(float *x, float *y)
{
    if (!dnd.own_dropped) return false;
    dnd.own_dropped = false, *x = dnd.own_x, *y = dnd.own_y;
    return true;
}

static void dnd_leave(void *data, struct wl_data_device *d);
static void *dnd_reader(void *arg);

// Offer o's text/uri-list read, on a thread of its own: it is in_offer now. A read that can't start
// is done at once, with no files.
static void dnd_read_start(struct wl_data_offer *o)
{
    dnd.in_offer = o, dnd.in_id++, dnd.in_read = false, dnd.in_ok = false, dnd.in_act = 0;
    dnd.got.n = 0;
    int p[2];
    if (pipe2(p, O_CLOEXEC) != 0) return (void)(dnd.in_read = true);
    wl_data_offer_receive(o, CLIP_MIME_URIS, p[1]);
    wl_display_flush(dnd.dpy);
    close(p[1]);
    dnd.read_fd = p[0], dnd.read_id = dnd.in_id, dnd.reading = true;
    pthread_t th;
    if (pthread_create(&th, NULL, dnd_reader, NULL) == 0) {
        pthread_detach(th);
        return;
    }
    close(p[0]);
    dnd.reading = false, dnd.in_read = true;
}

// The offer over the window read, when it is another program's files not read yet, and no read
// runs or drop waits for one: at its enter, or when the one before it ends (an enter meanwhile
// would have stayed refused until it left and came back).
static void dnd_read_next(void)
{
    int i = dnd.over;
    if (i < 0 || dnd.reading || dnd.in_dropped || dnd.offers[i].o == dnd.in_offer) return;
    unsigned t = dnd.offers[i].types;
    if ((t & DND_TYPE_OWN) || !(t & DND_TYPE_URIS)) return;
    dnd_read_start(dnd.offers[i].o);
}

static void dnd_enter(void *data, struct wl_data_device *d, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *o)
{
    (void)data, (void)d;
    if (dnd.over >= 0) dnd_leave(NULL, d);   // (a second enter without a leave: the first is over)
    dnd.serial = serial, dnd.x = (float)wl_fixed_to_double(x), dnd.y = (float)wl_fixed_to_double(y);
    dnd.action = 0, dnd.changed = true;
    int i = dnd_slot(o);
    DND_LOG("enter %p slot %d types %u on %s at %.0f,%.0f", (void *)o, i, i >= 0 ? dnd.offers[i].types : 0, s == dnd.surf ? "the window" : "another surface", dnd.x, dnd.y);
    if (!o) return;   // (a drag of another client's own, with no data: nothing to take)
    if (i < 0 || s != dnd.surf) {   // (not announced, or GLFW's decorations: refused, and the offer is ours)
        wl_data_offer_accept(o, serial, NULL);
        wl_data_offer_destroy(o);
        dnd_forget(i);
        return;
    }
    dnd.over = i, dnd.action = dnd.offers[i].action;
    dnd_read_next();
    dnd_answer();
}

static void dnd_leave(void *data, struct wl_data_device *d)
{
    (void)data, (void)d;
    DND_LOG("leave (slot %d)", dnd.over);
    if (dnd.over < 0) return;
    if (dnd.offers[dnd.over].o == dnd.in_offer) dnd.in_offer = NULL;   // (a read still going is for nothing now)
    wl_data_offer_destroy(dnd.offers[dnd.over].o);
    dnd_forget(dnd.over);
    dnd.over = -1, dnd.changed = true;
}

static void dnd_motion(void *data, struct wl_data_device *d, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
    (void)data, (void)d, (void)t;
    if (dnd.over < 0) return;
    dnd.x = (float)wl_fixed_to_double(x), dnd.y = (float)wl_fixed_to_double(y), dnd.changed = true;
}

// An offer's text/uri-list, read off the main thread: DND_WAIT_MS at most, DND_READ_MAX bytes at
// most; then dnd_poll takes its files on the main thread.
static void *dnd_reader(void *arg)
{
    (void)arg;
    int fd = dnd.read_fd;
    isize n = 0;
    bool whole = false;
    i64 until = now_ms() + DND_WAIT_MS;
    while (n < DND_READ_MAX - 1 && now_ms() < until) {
        if (poll(&(struct pollfd){ .fd = fd, .events = POLLIN }, 1, 50) <= 0) continue;
        ssize_t k = read(fd, dnd_read_mem + n, (size_t)(DND_READ_MAX - 1 - n));
        if (k < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (k <= 0) {
            whole = k == 0;
            break;
        }
        n += k;
    }
    close(fd);
    dnd_read_mem[n] = 0;
    dnd.read_n = n, dnd.read_whole = whole;
    atomic_store(&dnd.read_done, true);
    wake_main();
    return NULL;
}

static void dnd_drop(void *data, struct wl_data_device *d)
{
    (void)data, (void)d;
    int i = dnd.over;
    if (i < 0) return;
    struct wl_data_offer *o = dnd.offers[i].o;
    unsigned t = dnd.offers[i].types;
    DND_LOG("drop %p slot %d types %u action %u at %.0f,%.0f", (void *)o, i, t, dnd.action, dnd.x, dnd.y);
    dnd.over = -1, dnd.changed = true;   // (the leave after a drop, if one comes, finds nothing entered)
    dnd_forget(i);
    if (t & DND_TYPE_OWN) {   // ofx's own drag: not read (above)
        if (dnd.action) wl_data_offer_finish(o);
        wl_data_offer_destroy(o);
        dnd.own_drops++, dnd.own_dropped = dnd.action != 0, dnd.own_x = dnd.x, dnd.own_y = dnd.y;
        return;
    }
    // Another program's: kept for the window, once its read is done (dnd_in_dropped), even when it
    // ends after the drop (sway never sends the drop of a drag the window refused; Hyprland does).
    // Refused (no files, the window's answer no, or not read at all): given back.
    if (o != dnd.in_offer || (dnd.in_read && (!dnd.got.n || !dnd.in_ok))) {
        if (o == dnd.in_offer) dnd.in_offer = NULL;
        wl_data_offer_destroy(o);
        dnd.drops_failed++;
        dnd_read_next();
        return;
    }
    dnd.in_dropped = true, dnd.in_took = dnd.in_read, dnd.in_x = dnd.x, dnd.in_y = dnd.y;
    dnd.in_drop_actions = dnd.offers[i].actions, dnd.in_drop_c = dnd.action;
}

static void dnd_selection(void *data, struct wl_data_device *d, struct wl_data_offer *o)
{
    (void)data, (void)d, (void)o;   // (the selection is GLFW's: the hook isn't asked)
}

static const struct wl_data_device_listener dnd_dev_l = { dnd_data_offer, dnd_enter, dnd_leave, dnd_motion, dnd_drop, dnd_selection };

// ---- our drag out --------------------------------------------------------------------------------

// One of a source's payloads to fd, off the main thread: DND_WAIT_MS at most (a reader that leaves
// early gets EPIPE: SIGPIPE is blocked in every thread, main.c).
static void *dnd_writer(void *arg)
{
    int fd = (int)((intptr_t)arg >> 8);
    Str p = dnd.payload[(intptr_t)arg & 0xff];
    fcntl(fd, F_SETFL, O_NONBLOCK);
    i64 until = now_ms() + DND_WAIT_MS;
    isize at = 0;
    while (at < p.len && now_ms() < until) {
        ssize_t k = write(fd, p.p + at, (size_t)(p.len - at));
        if (k > 0) at += k;
        else if (k < 0 && errno != EAGAIN && errno != EINTR) break;
        else poll(&(struct pollfd){ .fd = fd, .events = POLLOUT }, 1, 50);
    }
    close(fd);
    atomic_fetch_add(&dnd.sent, (long long)at);
    atomic_fetch_add(&dnd.wrote, 1);
    atomic_fetch_sub(&dnd.writers, 1);
    wake_main();
    return NULL;
}

static void dnd_src_target(void *data, struct wl_data_source *s, const char *mime) { (void)data, (void)s, (void)mime; }
static void dnd_src_send(void *data, struct wl_data_source *s, const char *mime, int32_t fd)
{
    (void)data, (void)s;
    DND_LOG("source send %s", mime);
    int f = strcmp(mime, CLIP_MIME_URIS) == 0 ? CLIP_FMT_URIS : strcmp(mime, CLIP_MIME_TEXT[0]) == 0 || strcmp(mime, CLIP_MIME_TEXT[1]) == 0 ? CLIP_FMT_TEXT : -1;
    pthread_t th;
    atomic_fetch_add(&dnd.writers, 1);
    if (f >= 0 && pthread_create(&th, NULL, dnd_writer, (void *)(((intptr_t)fd << 8) | f)) == 0) {
        pthread_detach(th);
        return;
    }
    atomic_fetch_sub(&dnd.writers, 1);   // (our own type, which is never read, or no thread)
    close(fd);
}
// The drag image's surface and buffer, given back once the drag is over.
static void dnd_icon_drop(void)
{
    if (dnd.icon) wl_surface_destroy(dnd.icon), dnd.icon = NULL;
    if (dnd.icon_buf) wl_buffer_destroy(dnd.icon_buf), dnd.icon_buf = NULL;
}

static void dnd_src_end(struct wl_data_source *s, DndOut how)
{
    DND_LOG("source %s", DND_OUT_WORD[how]);
    wl_data_source_destroy(s);
    if (dnd.src == s) dnd.src = NULL, dnd.out = how, dnd.changed = true, dnd_icon_drop();
}
static void dnd_src_cancelled(void *data, struct wl_data_source *s) { (void)data; dnd_src_end(s, DND_OUT_CANCELLED); }
static void dnd_src_performed(void *data, struct wl_data_source *s)
{
    (void)data;
    DND_LOG("source dropped");
    if (dnd.src == s) dnd.out = DND_OUT_DROPPED, dnd.changed = true;
}
static void dnd_src_finished(void *data, struct wl_data_source *s) { (void)data; dnd_src_end(s, DND_OUT_DONE); }
static void dnd_src_action(void *data, struct wl_data_source *s, uint32_t a)
{
    (void)data;
    DND_LOG("source action %u", a);
    if (dnd.src == s) dnd.out_action = a, dnd.changed = true;
}
static const struct wl_data_source_listener dnd_src_l = { dnd_src_target, dnd_src_send, dnd_src_cancelled, dnd_src_performed, dnd_src_finished, dnd_src_action };

// The drag image: w x h ARGB pixels, premultiplied, at scale (its buffer's), the pointer at hx, hy
// on it (logical pixels); a surface of its own, or none when it can't be made.
typedef struct {
    const u32 *px;
    int w, h, scale, hx, hy;
} DndImage;

static struct wl_surface *dnd_icon_make(const DndImage *im)
{
    if (!im || !im->px || !dnd.comp || !dnd.shm) return NULL;
    int stride = im->w * 4, size = stride * im->h;
    int fd = memfd_create("ofx-drag", MFD_CLOEXEC);
    if (fd < 0) return NULL;
    void *p = ftruncate(fd, size) == 0 ? mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
    if (p == MAP_FAILED) return close(fd), NULL;
    memcpy(p, im->px, (size_t)size);
    munmap(p, (size_t)size);
    struct wl_shm_pool *pool = wl_shm_create_pool(dnd.shm, fd, size);
    close(fd);   // (the compositor has its own)
    dnd.icon_buf = wl_shm_pool_create_buffer(pool, 0, im->w, im->h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    dnd.icon = wl_compositor_create_surface(dnd.comp);
    return dnd.icon;
}

// c's files dragged out of the window, as copy (and with move, a move too), from the left press
// that holds the button now, im under the pointer (NULL: the compositor's own): false when there is
// no press, or a drag of ours is still going or being read.
static bool dnd_start(const ClipFiles *c, bool move, const DndImage *im)
{
    if (!dnd.ok || !dnd.press || dnd.src || atomic_load(&dnd.writers) > 0 || c->n == 0) return false;
    arena_reset(&dnd.srca);
    dnd.payload[CLIP_FMT_URIS] = clip_payload(&dnd.srca, c, CLIP_FMT_URIS);
    dnd.payload[CLIP_FMT_TEXT] = clip_payload(&dnd.srca, c, CLIP_FMT_TEXT);
    if (dnd.payload[CLIP_FMT_URIS].len == 0) return false;   // (too many to fit)
    dnd.src = wl_data_device_manager_create_data_source(dnd.mgr);
    wl_data_source_add_listener(dnd.src, &dnd_src_l, NULL);
    wl_data_source_offer(dnd.src, CLIP_MIME_URIS);
    wl_data_source_offer(dnd.src, CLIP_MIME_TEXT[0]);
    wl_data_source_offer(dnd.src, CLIP_MIME_TEXT[1]);
    wl_data_source_offer(dnd.src, dnd.own_mime);
    wl_data_source_set_actions(dnd.src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | (move ? WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE : 0));
    struct wl_surface *icon = dnd_icon_make(im);
    wl_data_device_start_drag(dnd.dev, dnd.src, dnd.surf, icon, dnd.press);
    if (icon) {   // (a drag icon's offset moves it from the pointer: the pointer at hx, hy on it)
        wl_surface_set_buffer_scale(icon, im->scale);
        wl_surface_attach(icon, dnd.icon_buf, -im->hx, -im->hy);
        wl_surface_damage(icon, 0, 0, im->w, im->h);
        wl_surface_commit(icon);
    }
    wl_display_flush(dnd.dpy);
    DND_LOG("start_drag (%s), offering %s", dnd.own_mime, move ? "copy and move" : "copy");
    dnd.out = DND_OUT_DRAGGING, dnd.out_action = 0, dnd.changed = true;
    dnd.own_ok = false, dnd.own_move = false, dnd.own_dropped = false;
    return true;
}

// Our drag ended by the window (Esc): the compositor ends it as the source goes.
static void dnd_cancel(void)
{
    if (!dnd.src) return;
    wl_data_source_destroy(dnd.src);
    dnd.src = NULL, dnd.out = DND_OUT_CANCELLED, dnd.changed = true;
    dnd_icon_drop();
    wl_display_flush(dnd.dpy);
}

// ---- the pointer's press serial, the seat, the registry ------------------------------------------

// A press's serial lasts until its release, which a drag's grab takes (ours or another's), so it
// goes at an enter or a leave too: a drag's start is a leave.
static void dnd_ptr_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y) { (void)data, (void)p, (void)serial, (void)s, (void)x, (void)y; dnd.press = 0; }
static void dnd_ptr_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) { (void)data, (void)p, (void)serial, (void)s; dnd.press = 0; }
static void dnd_ptr_motion(void *data, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) { (void)data, (void)p, (void)t, (void)x, (void)y; }
static void dnd_ptr_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
    (void)data, (void)p, (void)t;
    if (button == BTN_LEFT) dnd.press = state == WL_POINTER_BUTTON_STATE_PRESSED ? serial : 0, dnd.changed = true;
}
static void dnd_ptr_axis(void *data, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) { (void)data, (void)p, (void)t, (void)a, (void)v; }
static const struct wl_pointer_listener dnd_ptr_l = { .enter = dnd_ptr_enter, .leave = dnd_ptr_leave, .motion = dnd_ptr_motion, .button = dnd_ptr_button, .axis = dnd_ptr_axis };

// The pointer once the seat has one (asked for on a seat without one is a protocol error: the
// headless test sway has none until vptr makes one), bound at version 1, which has no other events.
static void dnd_seat_caps(void *data, struct wl_seat *s, uint32_t caps)
{
    (void)data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !dnd.ptr) dnd.ptr = wl_seat_get_pointer(s), wl_pointer_add_listener(dnd.ptr, &dnd_ptr_l, NULL);
    else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && dnd.ptr) wl_pointer_destroy(dnd.ptr), dnd.ptr = NULL, dnd.press = 0;
}
static void dnd_seat_name(void *data, struct wl_seat *s, const char *name) { (void)data, (void)s, (void)name; }
static const struct wl_seat_listener dnd_seat_l = { dnd_seat_caps, dnd_seat_name };

static void dnd_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
    (void)data;
    if (strcmp(iface, wl_data_device_manager_interface.name) == 0 && !dnd.mgr && version >= 3)
        dnd.mgr = wl_registry_bind(r, name, &wl_data_device_manager_interface, 3);
    if (strcmp(iface, wl_seat_interface.name) == 0 && !dnd.seat) dnd.seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
    if (strcmp(iface, wl_compositor_interface.name) == 0 && !dnd.comp && version >= 3) dnd.comp = wl_registry_bind(r, name, &wl_compositor_interface, 3);
    if (strcmp(iface, wl_shm_interface.name) == 0 && !dnd.shm) dnd.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
}
static void dnd_global_remove(void *data, struct wl_registry *r, uint32_t name) { (void)data, (void)r, (void)name; }
static const struct wl_registry_listener dnd_reg_l = { dnd_global, dnd_global_remove };

// After the window opens. Without Wayland, a version 3 data device manager or a seat, drags stay
// GLFW's (raylib's drop list, which ofx ignores).
static void dnd_init(void)
{
    dnd.over = -1, dnd.read_fd = -1;
    const char *dbg = getenv("OFX_GUI_DEBUG");
    dnd_debug = dbg && dbg[0];
    arena_init(&dnd.gota, "drop", dnd_got_mem, sizeof dnd_got_mem);
    arena_init(&dnd.srca, "drag source", dnd_src_mem, sizeof dnd_src_mem);
    snprintf(dnd.own_mime, sizeof dnd.own_mime, "application/x-ofx-drag-%d", (int)getpid());
    dnd.dpy = glfwGetWaylandDisplay();
    dnd.surf = GetWindowHandle();   // (raylib's is GLFW's wl_surface on Wayland)
    dnd.dev = dnd.dpy ? glfwGetWaylandDataDevice() : NULL;
    if (!dnd.dev || !dnd.surf) return;
    dnd.reg = wl_display_get_registry(dnd.dpy);   // (GLFW's queue: dispatched as GLFW dispatches)
    wl_registry_add_listener(dnd.reg, &dnd_reg_l, NULL);
    wl_display_roundtrip(dnd.dpy);
    if (!dnd.mgr || !dnd.seat) return;
    wl_seat_add_listener(dnd.seat, &dnd_seat_l, NULL);
    glfwSetWaylandDragHook(&dnd_dev_l, &dnd_offer_l, NULL);
    dnd.ok = true;
}

// Each frame: a read ended, its files kept for the offer it is of (only local file:// URIs count:
// a web link is no file); whether anything a frame shows changed.
static bool dnd_poll(void)
{
    if (atomic_exchange(&dnd.read_done, false)) {
        dnd.reading = false, dnd.changed = true;   // (another enter may read now)
        if (dnd.in_offer && dnd.read_id == dnd.in_id) {
            arena_reset(&dnd.gota);
            if (!dnd.read_whole || !clip_parse(&dnd.gota, (Str){ dnd_read_mem, dnd.read_n }, false, &dnd.got)) dnd.got.n = 0;
            dnd.in_read = true, dnd.changed = true;
            DND_LOG("read %td bytes%s: %td files", dnd.read_n, dnd.read_whole ? "" : " (cut short)", dnd.got.n);
        }
        dnd_read_next();   // (the offer entered while it ran)
    }
    bool c = dnd.changed;
    dnd.changed = false;
    return c;
}

// At the quit, the window still open: our source and every offer we hold given back, the hook
// unset. A thread still reading or writing has its own fd, and ends within DND_WAIT_MS.
static void dnd_close(void)
{
    if (!dnd.ok) return;
    glfwSetWaylandDragHook(NULL, NULL, NULL);
    if (dnd.src) wl_data_source_destroy(dnd.src), dnd.src = NULL;
    dnd_icon_drop();
    if (dnd.comp) wl_compositor_destroy(dnd.comp), dnd.comp = NULL;
    if (dnd.shm) wl_shm_destroy(dnd.shm), dnd.shm = NULL;
    if (dnd.over >= 0) wl_data_offer_destroy(dnd.offers[dnd.over].o), dnd.over = -1;
    if (dnd.in_dropped) wl_data_offer_destroy(dnd.in_offer), dnd.in_dropped = false;
    dnd.in_offer = NULL;
    if (dnd.ptr) wl_pointer_destroy(dnd.ptr), dnd.ptr = NULL;
    wl_seat_destroy(dnd.seat), wl_data_device_manager_destroy(dnd.mgr), wl_registry_destroy(dnd.reg);
    dnd.ok = false;
}
