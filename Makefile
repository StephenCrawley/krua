# Krua Makefile
CC = gcc
CFLAGS = -O3 -Wall -Wextra -std=c2x -Isrc -Wno-pointer-sign -Wno-sign-compare -Wno-unused-variable
SOURCES = src/object.c src/eval.c src/dyad.c src/error.c src/apply.c
OBJECTS = src/object.o src/eval.o src/dyad.o src/error.o src/apply.o
HEADERS = src/krua.h src/object.h src/eval.h src/limits.h src/dyad.h src/error.h src/apply.h

# Default target - build main interpreter
all: krua

# Main interpreter
krua: src/main.o $(OBJECTS)
	$(CC) $(CFLAGS) -o krua src/main.o $(OBJECTS)

# Test target - build and run tests (no leak checking)
test: test_krua
	./test_krua

# Test executable (without leak tracking)
test_krua: tests/test.o $(OBJECTS)
	$(CC) $(CFLAGS) -o test_krua tests/test.o $(OBJECTS)

# Leak target - build and run tests with leak checking
leak: clean test_leak
	./test_leak

# Test executable with leak tracking (rebuild all .o files with TRACK_REFS)
test_leak: tests/test.c tests/refcount.c $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -DTRACK_REFS -o test_leak tests/test.c tests/refcount.c $(SOURCES)

# Pattern rule for object files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
clean:
	rm -f test_krua test_leak src/*.o tests/*.o

.PHONY: all test leak clean