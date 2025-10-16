# a Makefile usually looks like:
# targets: dependencies
# 	command
# 	command
#
# $@ is shorthand for the target name
#
# $< is shorthand for the 1st dependency
# 
# previous test shared lib compile commands:
#   gcc *.c util/*.c -lm -pedantic -Wall -Wextra -Wno-maybe-uninitialized -Wno-sign-compare -g -O2 -o ktest #general skrawl exe
#   gcc -c -Wall -Werror -fpic *.c util/*.c                #compile files
#   gcc -shared -o libk.so *.o                             #create shared lib
#   gcc -L. -Lutil -Wall -o test ../test/test.c -lk -lm    #create test exe to run tests
#
# current compile command:
#   alias clangO2='clang *.c -std=c17 -pedantic -pedantic-errors -Wall -Wextra -pedantic -g -O2 -gdwarf-4 -flto -o'
#   clangO2 kl

CC=clang
CFLAGS=-std=c17 -pedantic -pedantic-errors -Wall -Wextra -O2 -gdwarf-4 -DDBG_WS
LFLAGS=$(CFLAGS) -fPIC  #library flags
DEP=src/skrawl.h

# build skrawl
_KDEPS=main.o parse.o object.o vm.o compile.o verb.o apply.o adverb.o io.o dyad.o
KDEPS=$(patsubst %,src/%,$(_KDEPS))
skrawl: t_parse t_compile $(KDEPS)
	$(CC) -o $@ src/*.o $(CFLAGS)

# build and test compile lib
_COMPILEDEPS=parse.o object.o compile.o
COMPILEDEPS=$(patsubst %,src/%,$(_COMPILEDEPS))
t_compile: t_parse libkcompile.so test/t_compile.c # test/t_compile.h
	$(CC) -L. -Wall -o test/$@ test/t_compile.c -lkcompile
	valgrind --leak-check=full --show-leak-kinds=all ./test/t_compile
libkcompile.so: $(COMPILEDEPS)
	$(CC) -shared -o $@ $(COMPILEDEPS) $(CFLAGS)

# build and test parse lib
_PARSEDEPS=object.o parse.o compile.o 
PARSEDEPS=$(patsubst %,src/%,$(_PARSEDEPS))
t_parse: libkparse.so test/t_parse.h test/t_parse.c 
# $(CC) -o kparse src/main.c $(PARSEDEPS) $(CFLAGS)
	$(CC) -Wall -o test/$@ test/t_parse.c -lkparse -L.
	valgrind --leak-check=full --show-leak-kinds=all ./test/t_parse
libkparse.so: $(PARSEDEPS)
	$(CC) -shared -o $@ $(PARSEDEPS) $(CFLAGS) 

# general build targets
%.o: %.c %.h $(DEP)
	$(CC) -c -o $@ $< $(LFLAGS)
cleanup:
	rm src/*.o *.so 
