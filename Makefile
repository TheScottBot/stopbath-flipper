# Host side build: the pure logic and its tests, with no SDK present.
# The FAP itself is built by ufbt (see README.md); this file never touches it.
#
# The warning set is the one the SDK applies to the application build
# (sdk.opts in the deployed SDK, recorded in
# docs/evaluation/ACTUAL_CONTRACT_EVALUATION.md 4.1) plus -Wshadow and
# -Wconversion, so the pure logic is held to at least the device standard.

# make predefines CC as "cc", which the MinGW distribution on the Windows host
# does not provide, so only a caller supplied CC survives; the default is gcc.
ifeq ($(origin CC),default)
CC := gcc
endif
PYTHON ?= python3
HOST_WARNINGS := -std=gnu2x -Wall -Wextra -Werror -Wstrict-prototypes -Wredundant-decls \
                 -Wdouble-promotion -Wundef -Wshadow -Wconversion
HOST_CFLAGS := $(HOST_WARNINGS) -O1 -g
SANITISER_CFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all

BUILD_DIR := build/host
PURE_LOGIC_SOURCES := remote_input/remote_input_model.c
PURE_LOGIC_HEADERS := remote_input/remote_input_model.h
TEST_SOURCES := tests/test_remote_input_model.c
TEST_HEADERS := tests/test_support.h

.PHONY: test test-sanitise check-typography check clean

test: $(BUILD_DIR)/test_remote_input_model
	$(BUILD_DIR)/test_remote_input_model

test-sanitise: $(BUILD_DIR)/test_remote_input_model_sanitised
	$(BUILD_DIR)/test_remote_input_model_sanitised

$(BUILD_DIR)/test_remote_input_model: $(PURE_LOGIC_SOURCES) $(TEST_SOURCES) $(PURE_LOGIC_HEADERS) $(TEST_HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $(PURE_LOGIC_SOURCES) $(TEST_SOURCES)

$(BUILD_DIR)/test_remote_input_model_sanitised: $(PURE_LOGIC_SOURCES) $(TEST_SOURCES) $(PURE_LOGIC_HEADERS) $(TEST_HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $(SANITISER_CFLAGS) -o $@ $(PURE_LOGIC_SOURCES) $(TEST_SOURCES)

check-typography:
	$(PYTHON) scripts/check_typography.py

check: check-typography test

clean:
	rm -rf build
