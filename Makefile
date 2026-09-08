CC ?= gcc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -D_GNU_SOURCE
CPPFLAGS := -Iinclude -Ishell -Ielf -Iloader -Iallocator -Inetwork
LDFLAGS ?=
LDLIBS := -pthread

BUILD := build
BIN := bin

CORE_SRC := \
	common/common.c \
	shell/parser.c shell/jobs.c shell/shell.c shell/main.c \
	elf/elf_parser.c elf/elf_inspector.c \
	loader/loader.c loader/main.c \
	tracer/tracer.c \
	allocator/allocator.c allocator/demo.c \
	network/protocol.c network/server.c network/client.c \
	runtime/cli.c
CORE_OBJ := $(CORE_SRC:%.c=$(BUILD)/%.o)

.PHONY: all shell elf-inspector loader tracer allocator remote-shell asm test clean help

all: $(BIN)/mlrt

$(BIN)/mlrt: $(CORE_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

shell elf-inspector loader tracer allocator remote-shell: $(BIN)/mlrt
	@echo "built $@ via $(BIN)/mlrt"

asm:
	@command -v nasm >/dev/null || { echo "nasm is required for the optional 32-bit assembly target"; exit 1; }
	@mkdir -p $(BUILD)/asm $(BIN)
	nasm -f elf32 asm/demo32.asm -o $(BUILD)/asm/demo32.o
	ld -m elf_i386 -o $(BIN)/raw-syscall-demo $(BUILD)/asm/demo32.o
	nasm -f elf32 asm/syscalls32.asm -o $(BUILD)/asm/syscalls32.o
	@echo "built $(BIN)/raw-syscall-demo and $(BUILD)/asm/syscalls32.o"

$(BUILD)/tests/test_parser: tests/test_parser.c shell/parser.c common/common.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD)/tests/test_allocator: tests/test_allocator.c allocator/allocator.c common/common.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -pthread -o $@

$(BUILD)/tests/freestanding.o: tests/assets/freestanding.S
	@mkdir -p $(dir $@)
	$(CC) -c -nostdlib -fno-pie $< -o $@

$(BUILD)/tests/freestanding: $(BUILD)/tests/freestanding.o
	ld -nostdlib -static -Ttext=0x700000000000 -o $@ $<

$(BUILD)/tests/test_elf: tests/test_elf.c elf/elf_parser.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

test: $(BIN)/mlrt $(BUILD)/tests/test_parser $(BUILD)/tests/test_allocator $(BUILD)/tests/test_elf $(BUILD)/tests/freestanding
	$(BUILD)/tests/test_parser
	$(BUILD)/tests/test_allocator
	$(BUILD)/tests/test_elf $(BIN)/mlrt
	$(BIN)/mlrt load $(BUILD)/tests/freestanding --map
	bash ./tests/test_smoke.sh

clean:
	rm -rf $(BUILD) $(BIN)

help:
	@printf '%s\n' \
	  'make              build bin/mlrt' \
	  'make shell        build shell capability' \
	  'make elf-inspector' \
	  'make loader' \
	  'make tracer' \
	  'make allocator' \
	  'make remote-shell' \
	  'make asm          optional NASM 32-bit demo' \
	  'make test         run automated tests' \
	  'make clean'
