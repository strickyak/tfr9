#ifndef _TMANAGER_METADATA_H_
#define _TMANAGER_METADATA_H_

constexpr static uint METADATA_MAX_LEN = 256;
constexpr static uint METADATA_ADDR = 0x10FFF000;

// +2 guarantees space for the EOF double-NUL even if the string is 256 bytes
static char MetadataLabel[METADATA_MAX_LEN + 2];

struct Metadata {
  static void InitLabel() {
    for (uint32_t i = 0; i < METADATA_MAX_LEN; i++) {
      uint8_t b = ((const uint8_t*)METADATA_ADDR)[i];

      // Treat 0xFF (erased flash) the same as 0x00 (EOF)
      if (b == 0x00 || b == 0xFF) {
        MetadataLabel[i] = '\0';
        MetadataLabel[i + 1] = '\0';
        return;
      }

      if (b == '=' || b == ',') {
        MetadataLabel[i] = '\0';
      } else {
        MetadataLabel[i] = (char)b;
      }
    }

    // Safety net: If the string was exactly 256 bytes without a NUL/0xFF,
    // force the double-NUL at the end.
    MetadataLabel[METADATA_MAX_LEN] = '\0';
    MetadataLabel[METADATA_MAX_LEN + 1] = '\0';
  }

  static void PrintLabel() {
    // CRITICAL: If you print over USB, wait for the terminal to connect!
    // (If you print over UART, you can remove this while-loop)
    while (!stdio_usb_connected()) {
      sleep_ms(100);
    }

    if (MetadataLabel[0] == 'p' && MetadataLabel[1] == '\0' &&
        MetadataLabel[2] == '1' && MetadataLabel[3] == '\0') {
      const char* p = MetadataLabel;

      while (*p) {
        const char* q = p + strlen(p) + 1;

        // Print the key and value
        printf("[%s=%s]\n", p, q);

        // Advance p to the next key
        p = q + strlen(q) + 1;
      }
    } else {
      printf("MetadataLabel did not start with p=1\n");
      printf("Memory dump at 0x10FFF000: ");
      for (int i = 0; i < 16; i++) {
        printf("%02X ", MetadataLabel[i]);
      }
      printf("\n");
    }
  }

  static const char* GetLabel(const char* key) {
    // Guard against null pointers or empty search keys
    if (!key || key[0] == '\0') {
      return nullptr;
    }

    const char* p = MetadataLabel;

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
};

#endif  // _TMANAGER_METADATA_H_
