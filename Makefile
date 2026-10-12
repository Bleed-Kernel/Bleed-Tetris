ELF_TARGET_64 = tetris

# Prefer the x86_64-elf cross compiler when installed (needed on macOS, where gcc is clang)
CC 	:= $(shell command -v x86_64-elf-gcc >/dev/null 2>&1 && echo x86_64-elf-gcc || echo gcc)

ELF_BIN_DIR_64 = bin
ELF_OBJ_DIR_64 = $(ELF_BIN_DIR_64)/obj

LIB_DIR = sysroot/lib
LIBC = $(LIB_DIR)/blibc.a
CRT0 = $(LIB_DIR)/start.o

BLIBC_REPO = https://github.com/Bleed-Kernel/blibc.git
BLIBC_DIR  ?= ../blibc

CRIME_REPO = https://github.com/Bleed-Kernel/Crime-Scene.git
CRIME_DIR  ?= ../Crime-Scene
LIBCRIME   = $(CRIME_DIR)/bin/libcrime.a

COMMON_CFLAGS = \
	-ffreestanding \
	-fno-stack-protector \
	-fno-stack-check \
	-Wall -Wextra \
	-O2 \
	-nostdlib \
	-no-pie \
	-m64 \
	-Isysroot/include \
	-Isysroot/include/bleed \
	-I$(CRIME_DIR)/include \
	-nostdinc

LDFLAGS = \
	-static \
	-nostdlib \
	-no-pie \
	-m64 \
	-L$(LIB_DIR)

SRCS = bleed.c
ELF_OBJS_64 = $(patsubst %.c,$(ELF_OBJ_DIR_64)/%.o,$(SRCS))

all: $(ELF_TARGET_64)

$(ELF_TARGET_64): blibc crime $(ELF_OBJS_64)
	@mkdir -p $(ELF_BIN_DIR_64)
	$(CC) $(LDFLAGS) \
		-o $(ELF_BIN_DIR_64)/$(ELF_TARGET_64) \
		$(CRT0) \
		$(ELF_OBJS_64) \
		$(LIBCRIME) \
		-l:blibc.a

$(ELF_OBJS_64): blibc crime

crime: $(LIBCRIME)

# built against the same blibc we use, not whatever its own makefile would go and fetch
$(LIBCRIME):
	@echo "[CRIME] Preparing the window library"
	@if [ ! -d "$(CRIME_DIR)" ]; then \
		git clone $(CRIME_REPO) $(CRIME_DIR); \
	fi
	$(MAKE) -C $(CRIME_DIR) bin/libcrime.a BLIBC_DIR=$(abspath $(BLIBC_DIR))

$(ELF_OBJ_DIR_64)/%.o: %.c ptetris.h
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

blibc:
	@echo "[BLIBC] Preparing blibc"
	@if [ ! -d "$(BLIBC_DIR)" ]; then \
		git clone $(BLIBC_REPO) $(BLIBC_DIR); \
	fi
	$(MAKE) -C $(BLIBC_DIR)

	@echo "[BLIBC] Syncing sysroot"
	@mkdir -p sysroot
	@cp -r $(BLIBC_DIR)/sysroot/* sysroot/

build: $(ELF_TARGET_64)

# desktop build, needs ../rawdraw and X11
rawdraw:
	gcc rawdraw.c -lX11 -o rawdraw.o
	./rawdraw.o

clean:
	rm -rf bin

distclean:
	rm -rf bin sysroot external

.PHONY: all build clean distclean blibc crime rawdraw
