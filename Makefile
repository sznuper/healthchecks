CC      = cosmocc
CFLAGS  = -msysv -Os -s
SOURCES = $(wildcard src/*.c)
TARGETS = $(patsubst src/%.c,build/%,$(SOURCES))

all: $(TARGETS)

build/%: src/%.c | build
	$(CC) $(CFLAGS) -o $@ $<
	rm -f $@.aarch64.elf $@.com.dbg

build:
	mkdir -p build

fmt:
	clang-format -i src/*.c

check-fmt:
	clang-format --dry-run --Werror src/*.c

clean:
	rm -rf build

.PHONY: all fmt check-fmt clean
