# nwndgen - POSIX C (C11). `make`, `make test`, `make sanitize`.
CC      ?= cc
OPT     ?= -O2
WARN    ?= -Wall -Wextra -Wpedantic -Wshadow -Wformat=2
CFLAGS  += -std=c11 -D_POSIX_C_SOURCE=200809L $(OPT) $(WARN) $(EXTRA)
BUILD   ?= build

LIBSRC  := src/util.c src/sha256.c src/prng.c src/erf.c src/config.c \
           src/catalog.c src/plan.c src/game.c src/gff.c src/recipe.c src/roster.c src/spawn.c src/dungeon.c src/summary.c
PACKSRC := src/key.c src/tileset.c
PACKOBJ := $(PACKSRC:src/%.c=$(BUILD)/%.o)
LIBOBJ  := $(LIBSRC:src/%.c=$(BUILD)/%.o)
DEPS    := $(wildcard src/*.h)

all: $(BUILD)/nwndgen $(BUILD)/erfcheck $(BUILD)/erfpack $(BUILD)/gffrt $(BUILD)/dgdump $(BUILD)/nwnpack $(BUILD)/dgcheck

$(BUILD)/%.o: src/%.c $(DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nwndgen: $(LIBOBJ) $(BUILD)/main.o
	$(CC) $(CFLAGS) $^ -o $@ -lm

$(BUILD)/erfcheck: $(LIBOBJ) $(BUILD)/erfcheck.o
	$(CC) $(CFLAGS) $^ -o $@ -lm

$(BUILD)/erfpack: $(LIBOBJ) $(BUILD)/erfpack.o
	$(CC) $(CFLAGS) $^ -o $@ -lm

$(BUILD)/gffrt: $(LIBOBJ) $(BUILD)/gffrt.o
	$(CC) $(CFLAGS) $^ -o $@ -lm

$(BUILD)/nwnpack.o: tools/nwnpack.c $(DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/nwnpack: $(LIBOBJ) $(PACKOBJ) $(BUILD)/nwnpack.o
	$(CC) $(CFLAGS) $^ -o $@ -lm

$(BUILD)/dgcheck.o: tools/dgcheck.c $(DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/dgcheck: $(LIBOBJ) $(PACKOBJ) $(BUILD)/dgcheck.o
	$(CC) $(CFLAGS) $^ -o $@ -lm

$(BUILD)/dgdump: $(LIBOBJ) $(BUILD)/dgdump.o
	$(CC) $(CFLAGS) $^ -o $@ -lm

$(BUILD)/fixtool: tests/fixtool.c
	$(CC) $(CFLAGS) tests/fixtool.c -o $@

$(BUILD)/mkdgfix: tests/mkdgfix.c tests/dgfix.h $(LIBOBJ) $(DEPS)
	$(CC) $(CFLAGS) -Isrc tests/mkdgfix.c $(LIBOBJ) -o $@ -lm

$(BUILD)/test_unit: tests/test_unit.c tests/dgfix.h $(LIBOBJ) $(PACKOBJ) $(DEPS)
	$(CC) $(CFLAGS) -Isrc tests/test_unit.c $(LIBOBJ) $(PACKOBJ) -o $@ -lm

test: $(BUILD)/nwndgen $(BUILD)/dgcheck $(BUILD)/erfcheck $(BUILD)/dgdump $(BUILD)/mkdgfix $(BUILD)/fixtool $(BUILD)/test_unit
	$(BUILD)/test_unit
	sh tests/e2e.sh $(BUILD)/nwndgen nwn1
	sh tests/e2e.sh $(BUILD)/nwndgen nwn2
	sh tests/dungeon_e2e.sh $(BUILD)

# Rebuild everything with ASan+UBSan into a separate tree and run the tests.
sanitize:
	$(MAKE) BUILD=build-san EXTRA="-g -fsanitize=address,undefined -fno-omit-frame-pointer" OPT=-O1 test

clean:
	rm -rf build build-san

# Optional: recompile the objective scripts with the official compiler (the .ncs files are committed so the
# generator and the tests never need it). usage: make scripts NWN_ROOT=/path/to/NWN NWN_USERDIR=/some/dir
scripts:
	@test -n "$(NWN_ROOT)" -a -n "$(NWN_USERDIR)" || { echo "set NWN_ROOT and NWN_USERDIR"; exit 2; }
	cd assets/scripts && for n in scel_enter scel_acquire scel_use scel_beat scel_rest; do nwn_script_comp --root "$(NWN_ROOT)" --userdirectory "$(NWN_USERDIR)" -o $$n.ncs $$n.nss || exit 1; done

.PHONY: all test sanitize clean scripts
