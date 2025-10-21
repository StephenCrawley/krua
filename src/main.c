#include "krua.h"
#include "eval.h"
#include "object.h"

int main(){
    printf("krua. mit license. "__DATE__".\n\n");
    
    K GLOBALS = ksymdict();

    // repl
    char buf[LINE_LEN];
    while (1){
        // print prompt, read input
        printf("  ");
        fgets(buf, LINE_LEN, stdin);

        // overwrite potential trailing newline
        K_char *nl = strchr(buf, '\n'); 
        if (nl) *nl = 0;

        // eval + print 
        K r = eval(kcstr(buf), GLOBALS);
        r ? kprint(r) : kperror(buf);
    }
}