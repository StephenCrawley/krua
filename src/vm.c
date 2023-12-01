#include "vm.h"
#include "object.h"
#include "compile.h"
#include "verb.h"
#include "apply.h"

#define STACK_MAX        256
#define POP()            ( *top++ )
#define PUSH(x)          ( *--top = (x) )
#define BYTECODE_PTR(x)  CHR( *OBJ(x) )
#define CONSTANT_PTR(x)  OBJ( OBJ(x)[1] )

K stack[STACK_MAX];
K globals; //K global variables

K run(K r, K *stack_base){
    // VM registers and variables
    K *top=stack_base;
    K *consts=CONSTANT_PTR(r);
    const u8 *ip=BYTECODE_PTR(r);
    u8 instr;  //current instruction

    // useful variables to execute opcodes
    K x,t[4];
    i64 n;
    MONAD u;
    DYAD v;

    for (;;){
        instr=*ip++;

        // ternary step function to convert monads/dyads/adverbs opcodes to base opcode
        switch (20u > (u32)(instr-OP_MONAD  ) ? OP_MONAD   :
                20u > (u32)(instr-OP_DYAD   ) ? OP_DYAD    :
                 6u > (u32)(instr-OP_ADVERB ) ? OP_ADVERB  : 
                 3u > (u32)(instr-OP_GET_ARG) ? OP_GET_ARG : instr)
        {

        case OP_MONAD:
            //printf("%03d OP_MONAD (%c)\n", instr, cverb(instr-OP_MONAD));
            u=monad_table[instr-OP_MONAD];
            x=(*u)(POP());
            if (IS_ERROR(x))
                goto run_error;
            PUSH(x);
            break;

        case OP_DYAD:
            //printf("%03d OP_DYAD (%c)\n", instr, cverb(instr-OP_DYAD));
            v=dyad_table[instr-OP_DYAD];
            x=POP();
            x=(*v)(x,POP());
            if (IS_ERROR(x))
                goto run_error;
            PUSH(x);
            break;

        case OP_ADVERB:
            //printf("%03d OP_ADVERB (%c%s)\n", instr, cadverb(instr-OP_ADVERB), instr-OP_ADVERB>2 ? ":" : "");
            x=POP();
            PUSH(kwx(K_ADVERB_START+(instr-OP_ADVERB),x));
            break;

        case OP_CONSTANT:
            //printf("%03d OP_CONSTANT (%d)\n", instr, *ip);
            PUSH(ref(consts[*ip++]));
            break;

        case OP_SET_GLOBAL:
            t[3]=ref(*top);
            t[2]=kv(TOK_COLON);
            t[1]=ref(consts[*ip++]);
            t[0]=ref(globals);
            globals=amend4(t);
            break;

        case OP_GET_GLOBAL:
            x=POP();
            n=symIndex(x,KEY(globals));
            unref(x);
            // if variable not defined
            if (n == HDR_CNT(KEY(globals))){
                x=kerr("'var");
                goto run_error;
            }
            // else push var onto stack
            PUSH(ref(OBJ(VAL(globals))[n]));
            break;

        case OP_APPLY_N:
            //printf("%03d OP_APPLY_N (%d)\n", instr, *ip);
            n=*ip++;
            x=POP();
            x=apply(x,top,n);
            top+=n;
            if (IS_ERROR(x))
                goto run_error;
            PUSH(x);
            break;

        case OP_POP:
            //printf("%03d OP_POP\n",instr);
            unref(POP());
            break;

        case OP_RETURN:
            //printf("%03d OP_RETURN\n",instr);
            return UNREF_R(POP());
        
        case OP_GET_ARG:
            PUSH(ref(stack_base[instr-OP_GET_ARG]));
            break;

        default:
            printf("%03d unknown instruction\n",instr);
        }
    }

run_error:
    // clean the stack
    while (top < stack_base) unref(*top++);
    // return error
    return UNREF_R(x);
}

// create the global variable dictionary
// inited as (,`)!,(::)
void initGlobals(){
    globals=makeKey(ks(0),ku(TOK_COLON));
}

// accepts parse tree (or error/null) as argument
// compiles to bytecode and runs on VM
K evalK(K x){
    if (IS_ERROR(x) || IS_NULL(x))
        return x;

    K r=compile(x);
    return IS_ERROR(r) ? r : run(r,stack+STACK_MAX);
}
