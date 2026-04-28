#include "krua.h"
#include "eval.h"
#include "object.h"
#include "error.h"

int main(){
    printf("krua. mit license. "__DATE__".\n\n");
    
    GLOBALS = ksymdict();
    KEYWORDS = syms4chrs(cutStr(kcstr(KEYWORDS_STRING), ' '));

    // repl
    char buf[LINE_LEN];
    while (1){
        // print prompt, read input
        printf("  ");
        char *f = fgets(buf, LINE_LEN, stdin);
        if (!f) exit(0);

        // overwrite potential trailing newline
        K_char *nl = strchr(buf, '\n'); 
        if (nl) *nl = 0;

        // eval + print 
        K r = eval(kcstr(buf));
        r ? kprint(r) : kperror(buf);
    }
}