# Thalovant embedded C client library.
#
#   make            build build/libthalovant.a
#   make test       build and run the host-side test suite
#   make CC=clang   ... with a different compiler
#   make clean

CC      ?= cc
AR      ?= ar
BUILD   ?= build
CFLAGS  ?= -O2
WARNFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror
CPPFLAGS += -Iinclude

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)
HDRS := $(wildcard include/thalovant/*.h)
LIB  := $(BUILD)/libthalovant.a

TEST_SRCS := $(wildcard tests/*.c)
TEST_BIN  := $(BUILD)/thalovant-tests

.PHONY: all test clean

all: $(LIB)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

$(TEST_BIN): $(TEST_SRCS) tests/harness.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(TEST_SRCS) $(LIB) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -rf $(BUILD)
