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

# Every pure logic module, compiled into every suite. Adding a module here is
# the only change needed for the tests to see it.
PURE_LOGIC_SOURCES := remote_input/remote_input_model.c \
                      remote_display/remote_display_layout.c \
                      remote_display/remote_display_fixtures.c \
                      remote_display/remote_font_metrics.c \
                      remote_display/remote_font_measure.c
PURE_LOGIC_HEADERS := $(wildcard remote_input/*.h remote_display/*.h)

# One binary per suite, named after its source.
TEST_SOURCES := $(wildcard tests/test_*.c)
TEST_HEADERS := tests/test_support.h
TEST_BINARIES := $(patsubst tests/%.c,$(BUILD_DIR)/%,$(TEST_SOURCES))
SANITISED_TEST_BINARIES := $(patsubst tests/%.c,$(BUILD_DIR)/%_sanitised,$(TEST_SOURCES))

.PHONY: test test-sanitise check-typography check clean

test: $(TEST_BINARIES)
	@for suite in $(TEST_BINARIES); do echo "== $$suite"; $$suite || exit 1; done

test-sanitise: $(SANITISED_TEST_BINARIES)
	@for suite in $(SANITISED_TEST_BINARIES); do echo "== $$suite"; $$suite || exit 1; done

$(BUILD_DIR)/%: tests/%.c $(PURE_LOGIC_SOURCES) $(PURE_LOGIC_HEADERS) $(TEST_HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $< $(PURE_LOGIC_SOURCES)

$(BUILD_DIR)/%_sanitised: tests/%.c $(PURE_LOGIC_SOURCES) $(PURE_LOGIC_HEADERS) $(TEST_HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $(SANITISER_CFLAGS) -o $@ $< $(PURE_LOGIC_SOURCES)

check-typography:
	$(PYTHON) scripts/check_typography.py

check: check-typography test

clean:
	rm -rf build
