#include <bios.h>

int main() {
  while (TRUE) {
    BiosPutChar('=');
    BiosPutChar('>');

    char* s = BiosGetStr();
    if (!s) {
      BiosPutStr("*END*\n");
      return 13;
    }

    BiosPutStr("Hello (");
    BiosPutStr(s);
    BiosPutStr(")\n");
  }
}
