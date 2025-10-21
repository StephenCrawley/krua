#ifndef DEBUG_H
#define DEBUG_H

#define DEBUG_TOKENS() \
    DEBUG("TOKENS:"); \
    FOR_EACH(x){ K_char c=CHR_PTR(x)[i]; c < OP_CONST ? printf("%c\n", c<20?OPS[c]:c) : printf("%.3d %.3d\n", c/32, c%32); fflush(stdout); }

#define DEBUG_BYTECODE() \
    DEBUG("BYTECODE:");FOR_EACH(x){ K_char c=CHR_PTR(x)[i]; \
        switch(c>>5){ \
        case 1:  \
        case 2: printf("V%d %c\n",c/32,OPS[c%32]); break; \
        case 4: printf("PUSH  "),out(ref(OBJ_PTR(consts)[c%32])); break; \
        case 5: printf("GET %.4s\n", CHR_PTR(&INT_PTR(vars)[c%32])); break; \
        case 6: printf("SET %.4s\n", CHR_PTR(&INT_PTR(vars)[c%32])); break; \
        }}putchar('\n');fflush(stdout);

#endif