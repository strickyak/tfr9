#ifndef _COCOSDC_H_
#define _COCOSDC_H_

uint sdc_lsn;
uint emu_disk_buffer;
byte rtc_value;

template <class T>
struct DontCocosdc {
  constexpr static bool DoesCocosdc() { return false; }
};

template <class T>
struct DoCocosdc {
  constexpr static bool DoesCocosdc() { return true; }

  static void Cocosdc_Install() {
    // Readers
    IOReaders[0x42] = [](uint addr, byte data) { return Reader42(addr, data); };
    IOReaders[0x43] = [](uint addr, byte data) { return Reader43(addr, data); };
    IOReaders[0x48] = [](uint addr, byte data) { return Reader48(addr, data); };
    IOReaders[0x4b] = [](uint addr, byte data) { return Reader4b(addr, data); };

    // Writers
    IOWriters[0x48] = [](uint addr, byte data) {
      if (data == 0xC0) {  // Special CocoSDC Command Mode
        if (T::Peek(0xFF40) == 0x43) {
          byte special_cmd = T::Peek(0xFF49);
          printf("- yak - Special CocoSDC Command Mode: %x %x %x\n",
                 special_cmd, T::Peek(0xFF4A), T::Peek(0xFF4B));
          switch (special_cmd) {
            case 'Q':
              T::Poke(0xFF49, 0x00);
              T::Poke(0xFF4A, 0x02);
              T::Poke(0xFF4B, 0x10);  // Say size $000210 sectors.
              break;
            case 'g':  // Set global flags.
              // llcocosdc.0250230904: [Secondary command to "Set Global
              // Flags"] [Disable Floppy Emulation capability in SDC
              // controller] uses param $FF80
              T::Poke(0xFF49, 0x00);
              T::Poke(0xFF4A, 0x00);
              T::Poke(0xFF4B, 0x00);
              break;
            default:
              T::Poke(0xFF49, 0x00);
              T::Poke(0xFF4A, 0x00);
              T::Poke(0xFF4B, 0x00);
              break;
          }  // end switch special_cmd
        }  // if data

      } else if (0x84 <= data && data <= 0x87) {
        // Read Sector
        T::ReadDisk(data & 3, sdc_lsn, sdc_disk_read_data);
        sdc_disk_read_ptr = sdc_disk_read_data;
        sdc_disk_pending = data;
      }
    };

    IOWriters[0x4B] = [](uint addr, byte data) {
      // Finalize scd_lsn on 0x4B
      sdc_lsn =
          (T::Peek(0xFF49) << 16) | (T::Peek(0xFF4A) << 8) | (0xFF & data);
      printf("SET SDC SECTOR sdc_lsn=%x\n", sdc_lsn);
    };

  };  // Install

  // CocoSDC Flash Data?
  static byte Reader42(uint addr, byte data) {
    // We need to emulate just enough of the Flash register behavior
    // so the initial scan thinks it has found a CocoSDC.
    // Cycles I see in the scan:
    // r FF42 <- $64
    // w FF43 -> 0
    // clr FF42
    // r FF43 -> should differ by XOR $60
    return data;
  }

  // CocoSDC Flash Control?
  static byte Reader43(uint addr, byte data) {
    if ((T::Peek(0xFF7F) & 3) == 3) {
      // Respond to MPI slot 3.
      // Return what was saved at 0x42?
      // I don't know why, just guessing, but it works,
      // for this line:
      // "llcocosdc.0250230904"+01e0   lda -5,x ; get value from Flash Ctrl Reg
      data = 0x60 & T::Peek(addr - 1);
    } else {
      data = 0;
    }
    return data;
  }
  static byte Reader48(uint addr, byte data) {
    if (sdc_disk_pending) {
      data = 3;  // BUSY and READY
      sdc_disk_pending = 0;
    } else {
      data = 0;  // NOT BUSY
    }
    return data;
  }
  static byte Reader4b(uint addr, byte data) {
    if (sdc_disk_read_ptr) {
      data = *sdc_disk_read_ptr++;
      if (sdc_disk_read_ptr == (sdc_disk_read_data + 256)) {
        sdc_disk_read_ptr = nullptr;
      }
    }
    return data;
  }
};  // struct DoCocosdc
#endif  // _COCOSDC_H_
