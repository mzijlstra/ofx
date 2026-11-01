# Unity build: one compiler call per binary. `make` builds debug (sanitizers on), the everyday
# build; `make release` builds an optimised binary into build/release; `make install` installs it.
CC      ?= cc
WARN     = -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wno-unused-function
# -MMD -MP: a unity build includes everything from one file, so the compiler's own list of what it
# read is the exact dependency list.
CFLAGS   = -std=c11 -D_GNU_SOURCE -pthread -MMD -MP $(WARN)
DEBUG    = -g3 -O0 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
RELEASE  = -O2 -DNDEBUG
# The previews' decoders (fs/thumb.c, F6.4): system libraries, and raylib's copy of
# stb_image_resize2.
IMG_INC  = -isystem vendor/raylib/src
IMG_LIBS = -lturbojpeg -lwebp -lpng16 -lgif -lz

PREFIX  ?= $(HOME)/.local

.PHONY: all release test test-fm1 install clean FORCE

all: build/ofx build/ofx-clean build/ofx-fm1

# The GUI scripts' keyboard in the test sway (tools/gui/vkbd.c, 2026-10-28).
VKBD_XML = tools/gui/virtual-keyboard-unstable-v1.xml
build/vkbd: tools/gui/vkbd.c $(VKBD_XML) Makefile | build
	wayland-scanner client-header $(VKBD_XML) build/virtual-keyboard-unstable-v1-client-protocol.h
	wayland-scanner private-code $(VKBD_XML) build/virtual-keyboard-unstable-v1-protocol.c
	$(CC) $(CFLAGS) -Ibuild -o $@ tools/gui/vkbd.c build/virtual-keyboard-unstable-v1-protocol.c $(shell pkg-config --cflags --libs wayland-client xkbcommon)

build/ofx-probe: tools/probe.c $(wildcard base/*.c base/*.h fs/*.c) | build
	$(CC) $(CFLAGS) $(IMG_INC) $(DEBUG) -o $@ tools/probe.c $(IMG_LIBS) -lm

build/ofx-clean: tools/clean.c $(wildcard base/*.c base/*.h fs/*.c) | build
	$(CC) $(CFLAGS) $(IMG_INC) $(DEBUG) -o $@ tools/clean.c $(IMG_LIBS) -lm

build/release/ofx-clean: tools/clean.c $(wildcard base/*.c base/*.h fs/*.c) | build/release
	$(CC) $(CFLAGS) $(IMG_INC) $(RELEASE) -o $@ tools/clean.c $(IMG_LIBS) -lm

# FileManager1 on the session bus (2026-10-26): sd-bus, from libsystemd.
build/ofx-fm1: tools/fm1.c $(wildcard base/*.c base/*.h fs/*.c) | build
	$(CC) $(CFLAGS) $(IMG_INC) $(DEBUG) -o $@ tools/fm1.c $(IMG_LIBS) -lsystemd -lm

build/release/ofx-fm1: tools/fm1.c $(wildcard base/*.c base/*.h fs/*.c) | build/release
	$(CC) $(CFLAGS) $(IMG_INC) $(RELEASE) -o $@ tools/fm1.c $(IMG_LIBS) -lsystemd -lm

# The tests warn of a function nothing calls: they take in every fs/ and ui/ function, so one unused
# there is dead (2026-10-28: state_save had 20 checks on code ofx no longer ran). The binaries can't:
# each takes in all of fs/ and uses part of it.
build/test: $(wildcard tests/*.c) $(wildcard base/*.c base/*.h fs/*.c ui/*.c) | build
	$(CC) $(CFLAGS) -Wunused-function $(IMG_INC) $(DEBUG) -o $@ tests/test.c $(IMG_LIBS) -lm

# Then ofx-clean as its timer runs it, reading ofx.conf (a scratch cache and config, the guard on).
test: build/test build/ofx-clean
	./build/test
	tests/clean/run.sh "$(CURDIR)/build/ofx-clean"

# ofx-fm1 on a private bus (dbus-run-session, its own config: only the test's service folder),
# with fakes for uwsm-app and ofx: no call reaches Michael's bus, no window opens.
test-fm1: build/ofx-fm1
	tests/fm1/run.sh "$(CURDIR)/build/fm1-test" "$(CURDIR)/build/ofx-fm1"

# ---- raylib: native Wayland, built from here ---------------------------------------------------
#
# raylib 6.0 is vendored unedited under vendor/raylib (the same commit as prdash and uml-viewer). Its own Makefile
# writes generated Wayland headers into its source tree, so it is built from here instead:
# protocol headers go to build/raylib/wl, objects and libraylib.a to build/raylib. Only the 2D
# modules are compiled (core, shapes, textures, text) plus its bundled GLFW with the Wayland
# backend and no X11.

RL       = vendor/raylib/src
RL_OUT   = build/raylib
RL_WL    = $(RL_OUT)/wl
RL_CFLAGS = -std=gnu99 -O2 -w -fno-strict-aliasing -MMD -MP -D_GNU_SOURCE -DPLATFORM_DESKTOP_GLFW \
            -DGRAPHICS_API_OPENGL_33 \
            -D_GLFW_WAYLAND -DSUPPORT_MODULE_RMODELS=0 -DSUPPORT_MODULE_RAUDIO=0 \
            -DSUPPORT_CUSTOM_FRAME_CONTROL=1 \
            -I$(RL) -I$(RL)/external/glfw/include -I$(RL_WL)
RL_MODS  = rcore rshapes rtextures rtext rglfw
RL_PROTO = wayland:wayland-client-protocol \
           xdg-shell:xdg-shell-client-protocol \
           xdg-decoration-unstable-v1:xdg-decoration-unstable-v1-client-protocol \
           viewporter:viewporter-client-protocol \
           relative-pointer-unstable-v1:relative-pointer-unstable-v1-client-protocol \
           pointer-constraints-unstable-v1:pointer-constraints-unstable-v1-client-protocol \
           fractional-scale-v1:fractional-scale-v1-client-protocol \
           xdg-activation-v1:xdg-activation-v1-client-protocol \
           idle-inhibit-unstable-v1:idle-inhibit-unstable-v1-client-protocol
# No -lGL: on Wayland GLFW makes its context through EGL, loaded at run time, and glvnd's libGL
# would pull in libGLX and libX11. So the binary is not linked against X11 and never connects to
# it, although Mesa's libEGL_mesa still maps libX11 and libxcb at run time.
GUI_LIBS = $(RL_OUT)/libraylib.a $(shell pkg-config --libs wayland-client wayland-cursor wayland-egl xkbcommon fontconfig freetype2) \
           $(IMG_LIBS) -lm -lpthread -ldl -lrt

$(RL_WL)/stamp: | build
	mkdir -p $(RL_WL)
	$(foreach p,$(RL_PROTO),\
	  wayland-scanner client-header $(RL)/external/glfw/deps/wayland/$(word 1,$(subst :, ,$(p))).xml \
	    $(RL_WL)/$(word 2,$(subst :, ,$(p))).h && \
	  wayland-scanner private-code $(RL)/external/glfw/deps/wayland/$(word 1,$(subst :, ,$(p))).xml \
	    $(RL_WL)/$(word 2,$(subst :, ,$(p)))-code.h &&) true
	touch $@

# -MMD: each object also depends on every header it included (rlgl.h, config.h, the GLFW
# platform files), and on this Makefile, so a flag change rebuilds it.
$(RL_OUT)/%.o: $(RL)/%.c $(RL_WL)/stamp Makefile
	$(CC) $(RL_CFLAGS) -c -o $@ $<

-include $(wildcard $(RL_OUT)/*.d)

$(RL_OUT)/libraylib.a: $(addprefix $(RL_OUT)/,$(addsuffix .o,$(RL_MODS)))
	$(AR) rcs $@ $^

# ---- ofx ---------------------------------------------------------------------------------------

# The system clipboard (gui/clipboard.c) speaks ext-data-control-v1, from wayland-protocols.
OFX_WL    = build/wl
EDC_XML  = $(shell pkg-config --variable=pkgdatadir wayland-protocols)/staging/ext-data-control/ext-data-control-v1.xml

$(OFX_WL)/stamp: | build
	mkdir -p $(OFX_WL)
	wayland-scanner client-header $(EDC_XML) $(OFX_WL)/ext-data-control-v1-client-protocol.h
	wayland-scanner private-code $(EDC_XML) $(OFX_WL)/ext-data-control-v1-client-protocol-code.h
	touch $@

# -isystem: vendored and generated headers are not held to our warning flags. --wrap: see the
# app id note in gui/main.c.
VENDOR   = -iquote build -isystem vendor/clay -isystem vendor/nanosvg -isystem $(RL) -isystem $(RL)/external/glfw/include -isystem $(OFX_WL) $(patsubst -I%,-isystem %,$(shell pkg-config --cflags freetype2))
LINK     = -Wl,--wrap=glfwDefaultWindowHints $(GUI_LIBS)
SRC      = $(wildcard base/*.c base/*.h fs/*.c ui/*.c gui/*.c) build/version.h

# Help → About's version (2026-10-27): the last commit's date and short hash, +dirty with changes to
# tracked files; "unknown" outside git (the GUI mutants' copies). Rewritten only when it changes, so
# an unchanged tree doesn't rebuild.
build/version.h: FORCE | build
	@v="$$(git log -1 --format='%cd (%h' --date=format:%Y.%m.%d 2>/dev/null)"; \
	if [ -z "$$v" ]; then v=unknown; else git diff --quiet HEAD 2>/dev/null || v="$$v+dirty"; v="$$v)"; fi; \
	printf '#define OFX_VERSION "%s"\n' "$$v" > $@.$$$$.tmp; \
	cmp -s $@.$$$$.tmp $@ && rm $@.$$$$.tmp || mv $@.$$$$.tmp $@
FORCE:

build/ofx: $(SRC) $(RL_OUT)/libraylib.a $(OFX_WL)/stamp Makefile | build
	$(CC) $(CFLAGS) $(VENDOR) $(DEBUG) -o $@ gui/main.c $(LINK)

build/release/ofx: $(SRC) $(RL_OUT)/libraylib.a $(OFX_WL)/stamp Makefile | build/release
	$(CC) $(CFLAGS) $(VENDOR) $(RELEASE) -o $@ gui/main.c $(LINK)

release: build/release/ofx

# The GUI mutants' build (tools/mutation/gui.py): clang, with the sanitizers as `make` has them,
# builds main.c in about half gcc's time; Clay's code, a third of the rest, and nanosvg's (about
# 0.5 s, 2026-10-26) are compiled once in their own object (gui/vendor.c) and only linked after
# (Michael, 2026-10-24). Not for use: `make` is the build.
MUT_CC   = clang
build/mut/vendor.o: gui/vendor.c vendor/clay/clay.h vendor/nanosvg/nanosvg.h vendor/nanosvg/nanosvgrast.h Makefile | build/mut
	$(MUT_CC) -std=c11 -D_GNU_SOURCE -MMD -MP -w -isystem vendor/clay -isystem vendor/nanosvg $(DEBUG) -c -o $@ gui/vendor.c

build/ofx-mut: $(SRC) build/mut/vendor.o $(RL_OUT)/libraylib.a $(OFX_WL)/stamp Makefile | build
	$(MUT_CC) $(CFLAGS) $(VENDOR) $(DEBUG) -DOFX_VENDOR_APART -o $@ gui/main.c build/mut/vendor.o $(LINK)

# The cleanup's timer (F6.5) is installed and loaded, not enabled: Michael enables it after a look
# at `ofx-clean --dry-run`.
UNITS   ?= $(HOME)/.config/systemd/user
APPS    ?= $(PREFIX)/share/applications
DBUS_SVC ?= $(PREFIX)/share/dbus-1/services

install: build/release/ofx build/release/ofx-clean build/release/ofx-fm1
	install -d $(PREFIX)/bin $(UNITS)
	install -m 755 build/release/ofx build/release/ofx-clean build/release/ofx-fm1 $(PREFIX)/bin/
	rm -f $(PREFIX)/bin/fx   # the name before ofx (2026-10-22)
	install -d $(APPS)
	sed 's|@BIN@|$(PREFIX)/bin|' tools/desktop/ofx.desktop > $(APPS)/ofx.desktop   # the folder handler's entry (2026-10-26)
	update-desktop-database $(APPS) 2>/dev/null || true
	install -d $(DBUS_SVC)   # FileManager1 (2026-10-26): read before Nemo's in /usr/share
	sed 's|@BIN@|$(PREFIX)/bin|' tools/dbus/org.freedesktop.FileManager1.service > $(DBUS_SVC)/org.freedesktop.FileManager1.service
	busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig 2>/dev/null || true
	sed 's|@BIN@|$(PREFIX)/bin|' tools/systemd/ofx-clean.service > $(UNITS)/ofx-clean.service
	install -m 644 tools/systemd/ofx-clean.timer $(UNITS)/
	systemctl --user daemon-reload

build build/release build/mut:
	mkdir -p $@

clean:
	rm -rf build

# Last, so a target in a .d file can never become the default goal.
-include $(wildcard build/*.d build/release/*.d build/mut/*.d)
