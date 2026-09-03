CC       ?= cc
CSTD     := -std=c11
WARN     := -Wall -Wextra
DEFS     := -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
OPT      ?= -O2 -g

CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS   := $(shell pkg-config --libs libcurl 2>/dev/null || echo -lcurl)

DB_CFLAGS := $(shell pkg-config --cflags libmariadb 2>/dev/null || mariadb_config --cflags 2>/dev/null || mysql_config --cflags 2>/dev/null)
DB_LIBS   := $(shell pkg-config --libs libmariadb 2>/dev/null || mariadb_config --libs 2>/dev/null || mysql_config --libs 2>/dev/null || echo -lmysqlclient)

CFLAGS  := $(CSTD) $(WARN) $(DEFS) $(OPT) -Iinclude $(CURL_CFLAGS) $(DB_CFLAGS)
LDFLAGS := -lm $(CURL_LIBS) $(DB_LIBS)

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

.PHONY: all clean

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

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
