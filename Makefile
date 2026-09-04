CC       ?= cc
CSTD     := -std=c11
WARN     := -Wall -Wextra
DEFS     := -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
OPT      ?= -O2 -g

# Compiler/linker hardening, requested explicitly rather than relied upon as
# a distro-toolchain default (see the audit's B1 finding):
#   -D_FORTIFY_SOURCE=2   compile-time + runtime buffer-overflow checks in
#                         the common libc string/memory functions (needs -O1+)
#   -fstack-protector-strong  stack-smashing canaries on functions with
#                         local arrays/address-taken locals
#   -fPIE (+ -pie below)  full ASLR for the executable itself, not just
#                         shared libraries
#   -Wl,-z,relro,-z,now   read-only GOT after startup (full RELRO)
HARDEN_CFLAGS  := -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
HARDEN_LDFLAGS := -pie -Wl,-z,relro,-z,now

# Set WERROR=-Werror (e.g. `make WERROR=-Werror`) to fail the build on any
# warning -- used in CI; left off by default so a local build with a newer/
# older compiler than expected still produces a usable binary.
WERROR ?=

CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS   := $(shell pkg-config --libs libcurl 2>/dev/null || echo -lcurl)

DB_CFLAGS := $(shell pkg-config --cflags libmariadb 2>/dev/null || mariadb_config --cflags 2>/dev/null || mysql_config --cflags 2>/dev/null)
DB_LIBS   := $(shell pkg-config --libs libmariadb 2>/dev/null || mariadb_config --libs 2>/dev/null || mysql_config --libs 2>/dev/null || echo -lmysqlclient)

CFLAGS  := $(CSTD) $(WARN) $(WERROR) $(DEFS) $(OPT) $(HARDEN_CFLAGS) -Iinclude $(CURL_CFLAGS) $(DB_CFLAGS)
LDFLAGS := $(HARDEN_LDFLAGS) -lm $(CURL_LIBS) $(DB_LIBS)

BUILD_DIR := build
BIN_DIR   := bin

COMMON_SRCS := \
	src/util.c \
	src/json.c \
	src/config.c \
	src/log.c \
	src/lock.c \
	src/http.c \
	src/event.c \
	src/content_security.c \
	src/ctftime_client.c \
	src/db.c \
	src/formatter.c \
	src/telegram_bot.c

COMMON_OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(COMMON_SRCS))

PARSER_OBJ    := $(BUILD_DIR)/parser_main.o
PUBLISHER_OBJ := $(BUILD_DIR)/publisher_main.o

TEST_DIR    := tests
TEST_BIN_DIR := $(BUILD_DIR)/tests
TEST_SRCS   := $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS   := $(patsubst $(TEST_DIR)/%.c,$(TEST_BIN_DIR)/%,$(TEST_SRCS))

.PHONY: all clean test

all: $(BIN_DIR)/parser $(BIN_DIR)/publisher

$(BIN_DIR)/parser: $(COMMON_OBJS) $(PARSER_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(COMMON_OBJS) $(PARSER_OBJ) $(LDFLAGS)

$(BIN_DIR)/publisher: $(COMMON_OBJS) $(PUBLISHER_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(COMMON_OBJS) $(PUBLISHER_OBJ) $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

-include $(COMMON_OBJS:.o=.d) $(PARSER_OBJ:.o=.d) $(PUBLISHER_OBJ:.o=.d)

# Unit tests: each tests/test_*.c is a standalone program linked against the
# non-main .c files it needs (declared via a "// deps: file1.c file2.c"
# comment on the first line) plus -lm; run every resulting binary and stop
# at the first failure (a test signals failure via a non-zero exit code).
$(TEST_BIN_DIR)/%: $(TEST_DIR)/%.c src/*.c include/*.h | $(TEST_BIN_DIR)
	@deps=$$(sed -n '1s#^// deps: ##p' $<); \
	srcs=""; \
	for d in $$deps; do srcs="$$srcs src/$$d"; done; \
	echo "$(CC) $(CSTD) $(WARN) $(DEFS) -g -Iinclude -o $@ $< $$srcs -lm"; \
	$(CC) $(CSTD) $(WARN) $(DEFS) -g -Iinclude -o $@ $< $$srcs -lm

$(TEST_BIN_DIR):
	mkdir -p $(TEST_BIN_DIR)

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		echo "-- $$t --"; \
		"$$t" || exit 1; \
	done
	@echo "All tests passed."

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
