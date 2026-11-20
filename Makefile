# Convenience wrapper around the out-of-source CMake build.
# Owned by worker-3 (task #370).
#
#   make            -> configures/generates build/ and produces build/sober-native
#   make check      -> runs the native self-check via CTest
#   make run        -> launches the native ELF
#   make clean      -> removes the generated build/ tree
#
# Nothing in build/ is ever committed.

BUILD_DIR ?= build
CMAKE     ?= cmake
CMAKE_ARGS ?= -DCMAKE_BUILD_TYPE=Release

.PHONY: all check test run clean rebuild

all:
	$(CMAKE) -S . -B $(BUILD_DIR) $(CMAKE_ARGS)
	$(CMAKE) --build $(BUILD_DIR) --parallel

rebuild: clean all

check test: all
	ctest --test-dir $(BUILD_DIR) --output-on-failure

run: all
	$(BUILD_DIR)/sober-native

clean:
	rm -rf $(BUILD_DIR)
