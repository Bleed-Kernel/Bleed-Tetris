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
CRIME_DIR  ?= $(if $(wildcard ../Crime-Scene/Makefile),../Crime-Scene,external/crime-scene)
CRIME_OUT  = libcrime
LIBCRIME   = $(CRIME_OUT)/libcrime.a

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
	-I$(CRIME_OUT)/include \
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

# a checkout handed in with CRIME_DIR is left as it is, only our own clone gets pulled.
# cp -p keeps the headers times so a pull that changed nothing doesnt rebuild anything
crime: blibc
	@echo "[CRIME] Preparing crime-scene"
	@if [ ! -d "$(CRIME_DIR)" ]; then \
		git clone $(CRIME_REPO) $(CRIME_DIR); \
	elif [ "$(CRIME_DIR)" = "external/crime-scene" ]; then \
		git -C $(CRIME_DIR) pull --ff-only || echo "[CRIME] couldnt pull, building whats there"; \
	fi
	@$(MAKE) -C $(CRIME_DIR) bin/libcrime.a BLIBC_DIR=$(abspath $(BLIBC_DIR))
	@echo "[CRIME] Syncing $(CRIME_OUT)"
	@rm -rf $(CRIME_OUT)/include
	@mkdir -p $(CRIME_OUT)/include/crime
	@cp -p $(CRIME_DIR)/include/crime/*.h $(CRIME_OUT)/include/crime/
	@cmp -s $(CRIME_DIR)/bin/libcrime.a $(LIBCRIME) || cp -p $(CRIME_DIR)/bin/libcrime.a $(LIBCRIME)

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
	rm -rf bin sysroot external $(CRIME_OUT)

.PHONY: all build clean distclean blibc crime rawdraw
