#ifndef _EMUDSK_H_
#define _EMUDSK_H_

constexpr uint kDiskReadSize = 1 + 4 + 256;

template <class T>
struct DontEmudsk {
  constexpr static bool DoesEmudsk() { return false; }
};

template <class T>
struct DoEmudsk {
  constexpr static bool DoesEmudsk() { return true; }

  void static Install(uint base_addr) {

        // LSN(hi)
        IOWriters[ base_addr + 0]= [](uint addr, byte data) {};
        // LSN(mid)
        IOWriters[ base_addr + 1]= [](uint addr, byte data) {};
        // LSN(lo)
        IOWriters[ base_addr + 2]= [](uint addr, byte data) {};

        // buffer address
        IOWriters[ base_addr + 4]= [](uint addr, byte data) {};
        IOWriters[ base_addr + 5] = [](uint addr, byte data) {};

        // drive number
        IOWriters[ base_addr + 6]=  [](uint addr, byte data) {};

        // Run EMUDSK command
        IOWriters[ base_addr + 3]=   [](uint addr, byte data) {
            byte command = data;

            T::Logf(
                "-EMUDSK device %x sector $%02x.%04x bufffer $%04x diskop %x\n",
                T::peek(EMUDSK_PORT + 6), T::peek(EMUDSK_PORT + 0),
                T::peek2(EMUDSK_PORT + 1), T::peek2(EMUDSK_PORT + 4), command);

            uint lsn = T::peek2(EMUDSK_PORT + 1);
            emu_disk_buffer = T::peek2(EMUDSK_PORT + 4);
#if INCLUDED_DISK
            byte* dp = Disk + 256 * lsn;
#endif

            T::Logf("-EMUDSK VARS sector $%04x buffer $%04x diskop %x\n",
                        lsn, emu_disk_buffer, command);

            switch (command) {
              case 0:  // Disk Read
#if INCLUDED_DISK
                for (uint k = 0; k < 256; k++) {
                  T::Poke(emu_disk_buffer + k, dp[k]);
                }
#else
                putbyte(C_DISK_READ);
                putbyte(T::peek(EMUDSK_PORT + 6));  // device
                putbyte(lsn >> 16);
                putbyte(lsn >> 8);
                putbyte(lsn >> 0);

                while (1) {
                  T::PollUsbInput();
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
#endif
                break;

              case 1:  // Disk Write
#if INCLUDED_DISK
                for (uint k = 0; k < 256; k++) {
                  dp[k] = T::peek(emu_disk_buffer + k);
                }
#else
                putbyte(C_DISK_WRITE);
                putbyte(T::peek(EMUDSK_PORT + 6));  // device
                putbyte(lsn >> 16);
                putbyte(lsn >> 8);
                putbyte(lsn >> 0);
                for (uint k = 0; k < 256; k++) {
                  putbyte(T::peek(emu_disk_buffer + k));
                }
                break;

              default:
                printf("\nwut emudsk command %d.\n", command);
                DumpRamAndGetStuck("wut emudsk", command);
#endif
            }
        };
  } // Install()

}; // struct DoEmudsk

#endif // _EMUDSK_H_
