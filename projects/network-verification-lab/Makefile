CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Werror
BIN := build/nver-datapath

.PHONY: all build test demo clean
all: build
build: $(BIN)
$(BIN): src/datapath.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@
test: build
	PYTHONPATH=python python3 -m pytest -q tests
demo: build
	PYTHONPATH=python python3 python/traffic_lab.py --binary ./$(BIN) --count 30 --corrupt-every 7 --replay-every 11
clean:
	rm -rf build
