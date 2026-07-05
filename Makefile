# Krua Makefile
CC = clang
CFLAGS = -O3 -Wall -Wextra -std=c2x -march=native -Isrc -Wno-unused-variable -Wno-psabi -D_POSIX_C_SOURCE=199309L -g
SOURCES = src/object.c src/eval.c src/op_unary.c src/op_binary.c src/error.c src/apply.c src/file.c src/adverb.c src/sym.c
OBJECTS = src/object.o src/eval.o src/op_unary.o src/op_binary.o src/error.o src/apply.o src/file.o src/adverb.o src/sym.o
HEADERS = src/krua.h src/object.h src/eval.h src/limits.h src/op_unary.h src/op_binary.h src/error.h src/apply.h src/file.h src/adverb.h src/sym.h

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

# Build & run the suite across ISA levels: exercises both __AVX512F__ branches
# (scalar fallback for v2/v3, AVX-512 path for v4) and convertvector lowering.
# A level needing instructions the host lacks (e.g. v4 without AVX-512) will SIGILL.
ARCHES = x86-64-v2 x86-64-v3 x86-64-v4
test-allarch: $(SOURCES) $(HEADERS) tests/test.c
	@for a in $(ARCHES); do \
	  echo "=== -march=$$a ==="; \
	  $(CC) $(filter-out -march=native,$(CFLAGS)) -march=$$a -o test_$$a tests/test.c $(SOURCES) && ./test_$$a || exit 1; \
	done
	@rm -f $(addprefix test_,$(ARCHES))

# Pattern rule for object files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
clean:
	rm -f krua test_krua test_leak test_x86-64-* src/*.o tests/*.o

.PHONY: test leak test-allarch clean