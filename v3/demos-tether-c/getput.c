#include <bios.h>

int main() {
  char c = BiosGetChar();
  BiosPutChar('<');
  BiosPutChar(c);
  BiosPutChar('>');
}
