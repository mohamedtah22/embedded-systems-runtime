CC ?= cc
CPPFLAGS += -Ielf
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
BUILD_DIR ?= build
COMMON = $(BUILD_DIR)/elf/elf_parser.o $(BUILD_DIR)/elf/elf_inspector.o
OBJECTS = $(COMMON) $(BUILD_DIR)/cli/main.o $(BUILD_DIR)/cli/elf_main.o

.PHONY: all elf-inspector test sanitize clean
all: $(BUILD_DIR)/mini-runtime $(BUILD_DIR)/elf-inspector
elf-inspector: $(BUILD_DIR)/elf-inspector

$(BUILD_DIR)/mini-runtime: $(COMMON) $(BUILD_DIR)/cli/main.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/elf-inspector: $(COMMON) $(BUILD_DIR)/cli/elf_main.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

test: all
	python3 tests/test_elf.py $(BUILD_DIR)/mini-runtime

sanitize:
	$(MAKE) BUILD_DIR=build-sanitize CFLAGS='-O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' LDFLAGS='-fsanitize=address,undefined -no-pie' test

clean:
	rm -rf build build-sanitize

-include $(OBJECTS:.o=.d)
