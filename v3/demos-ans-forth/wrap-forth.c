#include <bios.h>

char term_buffer[SYM_INPUT_SIZE];

__attribute__((naked))
void asm_putchar() {
    asm volatile (
        "  PSHS CC,D,DP,X,Y,U          \n"

        "  ; got char in B, want in D.   \n"
        "  CLRA                        \n"
        "  LBSR _BiosPutChar           \n"

        "  PULS CC,D,DP,X,Y,U,PC       \n"
    );
}
__attribute__((naked))
void asm_getchar() {
    asm volatile (
        "  PSHS CC,DP,X,Y,U            \n"

        "  LBSR _BiosGetChar           \n"
        "  ; Result is type char, in B \n"
        "  CLRA                        \n"
        "  ; Return it in D.           \n"

        "  PSHS D                      \n"
        "  LBSR _BiosPutChar  ; ECHO   \n"
        "  PULS D                      \n"

        "  CMPB #13  ; is it CR?       \n"
        "  BNE  skip_change_to_LF      \n"
        "  LDB  #10  ; change to LF    \n"
        "  PSHS D                      \n"
        "  LBSR _BiosPutChar  ; ECHO   \n"
        "  PULS D                      \n"
        "skip_change_to_LF: NOP        \n"

        "  PULS CC,DP,X,Y,U,PC         \n"
    );
}

__attribute__((naked))
void asm_bye() {
    for (const char* s = " *BYE*\n"; *s; s++) {
        BiosPutChar(*s);
    }

    asm volatile (
        "\nbye_stuck: BRA bye_stuck\n"
    );
}

__attribute__((naked))
void asm_launch() {
    asm volatile (
        "\n"
        "   LDS     SYM_RS_TOP         ;  forth__rs_top            \n"
        "   LDU     SYM_DS_TOP         ;  forth__ds_top            \n"
        "   LDX     #SYM_QUIT    ;  forth_core_quit.xt       \n"
        "   JMP     SYM_EXECUTE ;  forth_core_execute.asm   \n"
        "\n"
    );
}

#define HERE_TOP  0xEFFE
#define DS_BOTTOM 0xF002
#define DS_TOP    0xF1FE
#define RS_BOTTOM 0xF202
#define RS_TOP    0xF3FE

int main() {
    volatile void** p = (volatile void**) FORTH_ORIGIN;
    *p = BiosPutChar;
    *p = BiosGetChar;

    *p++ = asm_bye;
    *p++ = asm_getchar;
    *p++ = asm_putchar;
    *p++ = (char*)DS_BOTTOM;
    *p++ = (char*)DS_TOP;
    *p++ = (char*)RS_BOTTOM;
    *p++ = (char*)RS_TOP;
    *p++ = (char*)HERE_TOP;

    p = (volatile void**)SYM_SOURCE;
    *p = term_buffer;

    for (const char* s = "[github.com/spc476/ANS-Forth] tfr9/v3/demos-ans-forth\r\nOK ";
            *s; s++) {
        BiosPutChar(*s);
    }

    asm_launch();
}
