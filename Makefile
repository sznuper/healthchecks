CC      = cosmocc
CFLAGS  = -Os -s
SOURCES = $(wildcard checks/*.c)
TARGETS = $(patsubst checks/%.c,build/%,$(SOURCES))

all: $(TARGETS)

build/%: checks/%.c | build
	$(CC) $(CFLAGS) -o $@ $<
	rm -f $@.aarch64.elf $@.com.dbg

build:
	mkdir -p build

fmt:
	clang-format -i checks/*.c

check-fmt:
	clang-format --dry-run --Werror checks/*.c

clean:
	rm -rf build

.PHONY: all fmt check-fmt clean
