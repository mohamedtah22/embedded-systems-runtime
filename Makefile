CC ?= gcc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -D_GNU_SOURCE
CPPFLAGS := -Iinclude -Ishell -Ielf -Iloader -Iallocator -Inetwork -Iprotocol -Irealtime -Iserial -Itarget -Ifirmware
LDFLAGS ?=
LDLIBS := -pthread -lm

AARCH64_CC ?= aarch64-linux-gnu-gcc
QEMU_AARCH64 ?= qemu-aarch64
AARCH64_SYSROOT ?= /usr/aarch64-linux-gnu

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
	protocol/embedded_protocol.c \
	realtime/ring_buffer.c realtime/rt_demo.c realtime/event_loop.c \
	serial/virtual_uart.c \
	target/target_core.c target/target_server.c target/target_client.c \
	firmware/image.c \
	benchmarks/benchmark.c \
	runtime/logger.c runtime/cli.c
CORE_OBJ := $(CORE_SRC:%.c=$(BUILD)/%.o)

TARGET_STANDALONE_SRC := \
	protocol/embedded_protocol.c realtime/ring_buffer.c runtime/logger.c \
	target/target_core.c target/target_server.c target/standalone_main.c

.PHONY: all shell elf-inspector loader tracer allocator remote-shell embedded rt-demo event-demo serial target firmware benchmark \
	asm test arm64 arm64-target qemu qemu-smoke clean help

all: $(BIN)/mlrt $(BIN)/embedded-target

$(BIN)/mlrt: $(CORE_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN)/embedded-target: $(TARGET_STANDALONE_SRC)
	@mkdir -p $(BIN)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

shell elf-inspector loader tracer allocator remote-shell embedded rt-demo event-demo serial target firmware benchmark: $(BIN)/mlrt
	@echo "built $@ via $(BIN)/mlrt"

arm64 arm64-target: $(BIN)/embedded-target-arm64

$(BIN)/embedded-target-arm64: $(TARGET_STANDALONE_SRC)
	@command -v $(AARCH64_CC) >/dev/null || { echo "$(AARCH64_CC) is required for ARM64 cross compilation"; exit 1; }
	@mkdir -p $(BIN)
	$(AARCH64_CC) $(CPPFLAGS) $(CFLAGS) $^ -pthread -lm -o $@
	@echo "built ARM64 target simulator: $@"

qemu qemu-smoke: $(BIN)/embedded-target-arm64
	@command -v $(QEMU_AARCH64) >/dev/null || { echo "$(QEMU_AARCH64) is required"; exit 1; }
	$(QEMU_AARCH64) -L $(AARCH64_SYSROOT) $(BIN)/embedded-target-arm64 --self-test

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

$(BUILD)/tests/test_protocol: tests/test_protocol.c protocol/embedded_protocol.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD)/tests/test_ring_buffer: tests/test_ring_buffer.c realtime/ring_buffer.c
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

test: $(BIN)/mlrt $(BIN)/embedded-target $(BUILD)/tests/test_parser $(BUILD)/tests/test_allocator $(BUILD)/tests/test_protocol $(BUILD)/tests/test_ring_buffer $(BUILD)/tests/test_elf $(BUILD)/tests/freestanding
	$(BUILD)/tests/test_parser
	$(BUILD)/tests/test_allocator
	$(BUILD)/tests/test_protocol
	$(BUILD)/tests/test_ring_buffer
	$(BUILD)/tests/test_elf $(BIN)/mlrt
	$(BIN)/mlrt load $(BUILD)/tests/freestanding --map
	bash ./tests/test_smoke.sh
	bash ./tests/test_embedded.sh

clean:
	rm -rf $(BUILD) $(BIN)

help:
	@printf '%s\n' \
	  'make                 build bin/mlrt + native embedded target simulator' \
	  'make shell           build shell capability' \
	  'make elf-inspector' \
	  'make loader' \
	  'make tracer' \
	  'make allocator' \
	  'make remote-shell' \
	  'make embedded        build embedded-simulation capabilities' \
	  'make arm64-target    cross-compile target simulator for AArch64' \
	  'make qemu-smoke      execute ARM64 target self-test with qemu-aarch64' \
	  'make asm             optional NASM 32-bit demo' \
	  'make test            run automated tests' \
	  'make clean'
