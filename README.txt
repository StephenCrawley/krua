krua

wip.
done: tags, token, compile, vm, bitbool, adverbs, syms, csv.
todo: float, dict, table, prims, k-sql, db, ipc.

make build       make test (run tests)       make leak (tests + leak check)

Verb                       Adverb                Noun
: assign    -              f'      each          bool    0b 1b 01b
+ add       -flip          f/      over          char    "abc"
- sub       neg            f\      scan          int     2 3 4
* mul       first          f':     prior         sym     `a`b
% -         -              x f'y   each          list    (1;"ab";`c)
& min       where          x f/y   -             lambda  {[a;b]a+b}
| max       -              x f\y   -
< less      -              x f':y  -
> more      -              x f/:y  each right
= eql       -group         x f\:y  each left
~ match     not
! -         til            I/O                   System
, join      enlist         . x    read file      \l f.k  load
# take      count          csv x  parse csv      \t e    time
_ drop      -                                    \       exit
$ -         -
? -         -
^ cut       -
@ at index  -type
. -         value

- is nyi

monadic keywords: flip neg first where group type value til count not csv
index: x@i x[i] x[i;j], oob fills 0 or " "
csv (1;"iicC";"f.csv") -> (header;cols), types i c C, ' ' skips, 1=parse header
/ comments
nyi: amend a[0]:9, projection g[;1], select .. by .. from .. where

src/
  krua.h        K type, tags, list header, type enum, macros
  limits.h      compile-time limits
  utils.h       helpers
  object.c      buddy alloc, refcount, list, print
  sym.c         sym interning: hash table over sym pool
  eval.c        tokenizer, bytecode compiler, stack vm, eval
  apply.c       apply/index dispatch, lambda invocation
  op_unary.c    monadic verbs
  op_binary.c   dyadic + promote
  adverb.c      each over scan prior left right
  file.c        file read, \l
  error.c       errors
  main.c        repl
tests/
  test.c        tests
  refcount.c    leak tracking

mit license.
