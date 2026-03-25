# Krua Makefile
CC = clang
CFLAGS = -O3 -Wall -Wextra -std=c2x -march=native -Isrc -Wno-pointer-sign -Wno-unused-variable -D_POSIX_C_SOURCE=199309L -g
RELEASE_CFLAGS = -O3 -std=c2x -march=native -flto -DNDEBUG -Isrc -Wno-pointer-sign -Wno-unused-variable -D_POSIX_C_SOURCE=199309L
SOURCES = src/object.c src/eval.c src/op_unary.c src/op_binary.c src/error.c src/apply.c
OBJECTS = src/object.o src/eval.o src/op_unary.o src/op_binary.o src/error.o src/apply.o
HEADERS = src/krua.h src/object.h src/eval.h src/limits.h src/op_unary.h src/op_binary.h src/error.h src/apply.h

# Default target - build main interpreter
all: krua

# Main interpreter
krua: src/main.o $(OBJECTS)
	$(CC) $(CFLAGS) -o krua src/main.o $(OBJECTS)

# Release build - optimized, no debug symbols
release: clean
	$(CC) $(RELEASE_CFLAGS) -o krua src/main.c $(SOURCES)

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
	rm -f krua test_krua test_leak src/*.o tests/*.o

.PHONY: all release test leak clean