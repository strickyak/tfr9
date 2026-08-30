#include "cobs_tx.h"
#ifndef _FIRMWARE_FLASH_LABEL_H_
#define _FIRMWARE_FLASH_LABEL_H_

struct FlashLabel {
  static const uint32_t LABEL_MAX_LEN = 256;
  static const unsigned long long LABEL_ADDR = 0x10FFF000;

  // +2 guarantees space for the EOF double-NUL even if the string is 256 bytes
  static char Label[LABEL_MAX_LEN + 2];

  static void InitLabel() {
    for (uint32_t i = 0; i < LABEL_MAX_LEN; i++) {
      uint8_t b = *(uint8_t*)(LABEL_ADDR + i);

      // Treat 0xFF (erased flash) the same as 0x00 (EOF)
      if (b == 0x00 || b == 0xFF) {
        Label[i] = '\0';
        Label[i + 1] = '\0';
        return;
      }

      if (b == '=' || b == ',') {
        Label[i] = '\0';
      } else {
        Label[i] = (char)b;
      }
    }

    // Safety net: If the string was exactly 256 bytes without a NUL/0xFF,
    // force the double-NUL at the end.
    Label[LABEL_MAX_LEN] = '\0';
    Label[LABEL_MAX_LEN + 1] = '\0';
  }

  static void PrintLabel() {
    // Skip label printing if USB is not connected.
    // This allows the firmware to boot without the tether attached.
    if (!stdio_usb_connected()) return;

    if (Label[0] == 'p' && Label[1] == '\0' && Label[2] == '1' &&
        Label[3] == '\0') {
      const char* p = Label;

      while (*p) {
        const char* q = p + strlen(p) + 1;

        // Print the key and value
        cobs_printf("[%s=%s]\n", p, q);

        // Advance p to the next key
        p = q + strlen(q) + 1;
      }
    } else {
      cobs_printf("Label did not start with p=1\n");
      cobs_printf("Memory dump at 0x%llx: ", LABEL_ADDR);
      for (int i = 0; i < 16; i++) {
        cobs_printf("%02X ", Label[i]);
      }
      cobs_printf("\n");
    }
  }

  static const char* GetLabel(const char* key) {
    // Guard against null pointers or empty search keys
    if (!key || key[0] == '\0') {
      return nullptr;
    }

    const char* p = Label;

    // Iterate through the array.
    // The loop breaks when p points to the final empty key (the double NUL).
    while (*p != '\0') {
      const char* current_key = p;

      // The value starts immediately after the current key's NUL terminator
      const char* current_value = current_key + strlen(current_key) + 1;

      // Check if we found a match
      if (strcmp(current_key, key) == 0) {
        return current_value;
      }

      // Advance 'p' to the start of the next key.
      // This is immediately after the current value's NUL terminator.
      p = current_value + strlen(current_value) + 1;
    }

    // Key was not found in the array
    return nullptr;
  }

};  // FlashLabel

char FlashLabel::Label[LABEL_MAX_LEN + 2];

#endif  // _FIRMWARE_FLASH_LABEL_H_
