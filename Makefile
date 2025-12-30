# Purple C Compiler Makefile
# ASAP + ISMM 2024 Memory Management

CC = gcc
CFLAGS = -Wall -Wextra -g -I./src

# Source directories
SRC_DIR = src
ANALYSIS_DIR = $(SRC_DIR)/analysis
MEMORY_DIR = $(SRC_DIR)/memory
CODEGEN_DIR = $(SRC_DIR)/codegen
EVAL_DIR = $(SRC_DIR)/eval
PARSER_DIR = $(SRC_DIR)/parser

# Source files
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/types.c \
       $(ANALYSIS_DIR)/escape.c \
       $(ANALYSIS_DIR)/shape.c \
       $(ANALYSIS_DIR)/dps.c \
       $(MEMORY_DIR)/scc.c \
       $(MEMORY_DIR)/deferred.c \
       $(MEMORY_DIR)/arena.c \
       $(CODEGEN_DIR)/codegen.c \
       $(EVAL_DIR)/eval.c \
       $(PARSER_DIR)/parser.c

# Object files
OBJS = $(SRCS:.c=.o)

# Target executable
TARGET = purple_c

# Legacy monolithic build
LEGACY_TARGET = purple_legacy

.PHONY: all clean test legacy run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Pattern rule for object files
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Legacy monolithic build (for comparison)
legacy:
	$(CC) -Wall -o $(LEGACY_TARGET) main.c

run: all
	./$(TARGET)

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET) $(LEGACY_TARGET)
	rm -f output.c output

# Run test suite
test: all
	./tests.sh

# Generate and compile output
compile-output: all
	./$(TARGET) "(let ((x (lift 10))) (+ x (lift 5)))" > output.c
	$(CC) -o output output.c
	./output
