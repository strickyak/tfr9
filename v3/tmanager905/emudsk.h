#ifndef _EMUDSK_H_
#define _EMUDSK_H_

constexpr uint kDiskReadSize = 1 + 4 + 256;

uint emu_disk_buffer;

template <typename T>
struct DontEmudsk {
  constexpr static bool DoesEmudsk() { return false; }
};

template <typename T>
struct DoEmudsk {
  constexpr static bool DoesEmudsk() { return true; }

  void static Emudsk_Install(uint base_addr) {
    base_addr &= 0xFF;

    IOReaders[base_addr + 3] = [](uint addr, byte data) {
        // Good status, after command written to base_addr+3.
        return 0;
    };

    // LSN(hi)
    IOWriters[base_addr + 0] = [](uint addr, byte data) {};
    // LSN(mid)
    IOWriters[base_addr + 1] = [](uint addr, byte data) {};
    // LSN(lo)
    IOWriters[base_addr + 2] = [](uint addr, byte data) {};

    // buffer address
    IOWriters[base_addr + 4] = [](uint addr, byte data) {};
    IOWriters[base_addr + 5] = [](uint addr, byte data) {};

    // drive number
    IOWriters[base_addr + 6] = [](uint addr, byte data) {};

    // Run EMUDSK command
    IOWriters[base_addr + 3] = [](uint addr, byte data) {
      byte command = data;

      printf(  // T::Logf(
          "-EMUDSK device %x sector $%02x.%04x bufffer $%04x diskop %x\n",
          T::Peek(EMUDSK_PORT + 6), T::Peek(EMUDSK_PORT + 0),
          T::Peek2(EMUDSK_PORT + 1), T::Peek2(EMUDSK_PORT + 4), command);

      uint lsn = T::Peek2(EMUDSK_PORT + 1);
      emu_disk_buffer = T::Peek2(EMUDSK_PORT + 4);

      switch (command) {
        case 0:  // Disk Read
          ++quiet_ram;
          putbyte(C_DISK_READ);
          putbyte(T::Peek(EMUDSK_PORT + 6));  // device
          putbyte(lsn >> 16);
          putbyte(lsn >> 8);
          putbyte(lsn >> 0);
          --quiet_ram;

          while (1) {
            PollUsbInput();
            if (T::PeekDiskInput()) {
              for (uint k = 0; k < kDiskReadSize - 256; k++) {
                (void)disk_input.Take();  // 4-byte device & LSN.
              }
              for (uint k = 0; k < 256; k++) {
                T::Poke(emu_disk_buffer + k, disk_input.Take());
              }
              data = 0;  // Ready
              break;
            }
          }
          break;

        case 1:  // Disk Write
          putbyte(C_DISK_WRITE);
          putbyte(T::Peek(EMUDSK_PORT + 6));  // device
          putbyte(lsn >> 16);
          putbyte(lsn >> 8);
          putbyte(lsn >> 0);
          for (uint k = 0; k < 256; k++) {
            putbyte(T::Peek(emu_disk_buffer + k));
          }
          break;

        default:
          printf("\nwut emudsk command %d.\n", command);
          T::DumpRamAndGetStuck("wut emudsk", command);
      }
    };
  }  // Emudsk_Install()

};  // struct DoEmudsk

#endif  // _EMUDSK_H_
