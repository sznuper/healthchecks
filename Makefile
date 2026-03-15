CC      = cosmocc
CFLAGS  = -mtiny -Os -s
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

TEST_CC = gcc
TESTS   = build/test_sznuper build/test_ssh_journal

build/test_%: test/test_%.c test/munit.c | build
	$(TEST_CC) -o $@ $< test/munit.c

test: $(TESTS)
	@for t in $(TESTS); do echo "--- $$t ---"; ./$$t; done

.PHONY: all fmt check-fmt clean test
