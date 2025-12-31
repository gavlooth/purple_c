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
UTIL_DIR = $(SRC_DIR)/util

# Source files
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/types.c \
       $(UTIL_DIR)/dstring.c \
       $(UTIL_DIR)/hashmap.c \
       $(ANALYSIS_DIR)/escape.c \
       $(ANALYSIS_DIR)/shape.c \
       $(ANALYSIS_DIR)/dps.c \
       $(ANALYSIS_DIR)/rcopt.c \
       $(MEMORY_DIR)/scc.c \
       $(MEMORY_DIR)/deferred.c \
       $(MEMORY_DIR)/arena.c \
       $(MEMORY_DIR)/symmetric.c \
       $(MEMORY_DIR)/region.c \
       $(MEMORY_DIR)/genref.c \
       $(MEMORY_DIR)/constraint.c \
       $(MEMORY_DIR)/exception.c \
       $(MEMORY_DIR)/concurrent.c \
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

# Unit test sources (subset needed for each test)
UTIL_OBJS = $(UTIL_DIR)/dstring.o $(UTIL_DIR)/hashmap.o
TYPE_OBJS = $(SRC_DIR)/types.o
ANALYSIS_OBJS = $(ANALYSIS_DIR)/escape.o $(ANALYSIS_DIR)/shape.o $(ANALYSIS_DIR)/rcopt.o

# Run unit tests
unit-test: $(UTIL_OBJS) $(TYPE_OBJS) $(ANALYSIS_OBJS)
	@echo "Building and running unit tests..."
	$(CC) $(CFLAGS) -o tests/test_rcopt tests/unit_rcopt.c \
		$(ANALYSIS_DIR)/rcopt.o $(ANALYSIS_DIR)/shape.o \
		$(TYPE_OBJS) $(UTIL_OBJS)
	./tests/test_rcopt
	@rm -f tests/test_rcopt

# Generate and compile output
compile-output: all
	./$(TARGET) "(let ((x (lift 10))) (+ x (lift 5)))" > output.c
	$(CC) -o output output.c
	./output
