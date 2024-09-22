INCLUDE_DIRS := ./include
INCLUDES := $(foreach dir, $(INCLUDE_DIRS), -I$(dir))
USR_DEFS ?= 
USR_DEFS += #-DNDEBUG -DGENERATE_VECTOR_FUNCTIONS_INLINE
DEFS := -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
CFLAGS := -std=c99 -Wpedantic -Wall -Wextra -fno-common -Wconversion -g $(DEFS) $(USR_DEFS)
LDFLAGS := -lrt
 
CC := gcc
OBJ_DIR := objs
TARGETS := main

VPATH = src

src_to_objs = $(foreach file, $(notdir $(1:.c=.o)), $(2)/$(file))
asm_src_to_objs = $(foreach file, $(notdir $(1:.S=.o)), $(2)/$(file))

MAIN_SRCS := src/lthread.c 
MAIN_ASM_SRCS := src/start_thread.S
MAIN_OBJS := $(call src_to_objs, $(MAIN_SRCS), $(OBJ_DIR))
MAIN_OBJS += $(call asm_src_to_objs, $(MAIN_ASM_SRCS), $(OBJ_DIR))
TESTS := test_io test_produce_consume test_many_threads test_blocking test_freq

.PHONY: clean valgrind debug tests

all: $(TARGETS)

main: src/main.c $(MAIN_OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(INCLUDES)

$(OBJ_DIR)/%.o: %.c %.h | $(OBJ_DIR)
	$(CC) -c $< -o $@ $(CFLAGS) $(INCLUDES)

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(CC) -c $< -o $@ $(CFLAGS) $(INCLUDES)

$(OBJ_DIR)/%.o: %.S | $(OBJ_DIR)
	$(CC) -c $< -o $@ $(CFLAGS) $(INCLUDES)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

tests: $(TESTS)

%: test/%.c $(MAIN_OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(INCLUDES)

debug: CFLAGS += -g -O0 -DLTHREAD_DEBUG
debug: main $(TESTS)

valgrind: clean debug
	valgrind ./main

clean:
	rm -rf $(OBJ_DIR)/* $(TARGETS) $(TESTS)
