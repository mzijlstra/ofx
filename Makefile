# Unity build: one compiler call per binary. `make` builds debug (sanitizers on), the everyday
# build; `make release` builds an optimised binary into build/release; `make install` installs it.
CC      ?= cc
WARN     = -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wno-unused-function
# -MMD -MP: a unity build includes everything from one file, so the compiler's own list of what it
# read is the exact dependency list.
CFLAGS   = -std=c11 -D_GNU_SOURCE -pthread -MMD -MP $(WARN)
DEBUG    = -g3 -O0 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
RELEASE  = -O2 -DNDEBUG

PREFIX  ?= $(HOME)/.local

.PHONY: all release test install clean

all: build/fx

build/test: tests/test.c | build
	$(CC) $(CFLAGS) $(DEBUG) -o $@ tests/test.c -lm

test: build/test
	./build/test

build build/release:
	mkdir -p $@

clean:
	rm -rf build

-include $(wildcard build/*.d build/release/*.d)
