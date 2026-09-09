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

# The overflow test rebuilds src/topics.c with a reduced THALOVANT_TOPIC_MAX
# and ships its own main(), so it is built as a separate binary rather than
# linked into the shared suite.
OVERFLOW_SRC := tests/test_topics_overflow.c
OVERFLOW_BIN := $(BUILD)/thalovant-topics-overflow-tests

TEST_SRCS := $(filter-out $(OVERFLOW_SRC),$(wildcard tests/*.c))
TEST_BIN  := $(BUILD)/thalovant-tests

.PHONY: all test fuzz clean

all: $(LIB)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

$(TEST_BIN): $(TEST_SRCS) tests/harness.h $(wildcard tests/fixtures/*.h) $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(TEST_SRCS) $(LIB) -o $@

$(OVERFLOW_BIN): $(OVERFLOW_SRC) src/topics.c $(HDRS) tests/harness.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(OVERFLOW_SRC) -o $@

test: $(TEST_BIN) $(OVERFLOW_BIN)
	./$(TEST_BIN)
	./$(OVERFLOW_BIN)

# Development/CI only: Clang's instrumentation, no runtime library dependency.
FUZZ_CC ?= clang
FUZZ_SECONDS ?= 60
$(BUILD)/thalovant-fuzz: tests/fuzz/protocol.c $(SRCS) $(HDRS) | $(BUILD)
	$(FUZZ_CC) $(CPPFLAGS) -O1 -g $(WARNFLAGS) -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer tests/fuzz/protocol.c $(SRCS) -o $@

fuzz: $(BUILD)/thalovant-fuzz
	mkdir -p $(BUILD)/fuzz-corpus $(BUILD)/fuzz-artifacts
	cp tests/fuzz/corpus/* $(BUILD)/fuzz-corpus/
	./$(BUILD)/thalovant-fuzz $(BUILD)/fuzz-corpus -max_total_time=$(FUZZ_SECONDS) -max_len=8192 -timeout=10 -artifact_prefix=$(BUILD)/fuzz-artifacts/

clean:
	rm -rf $(BUILD)
