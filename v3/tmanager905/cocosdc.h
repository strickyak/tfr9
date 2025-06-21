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

  void static Cocosdc_Install() {
    IOWriters[0x48] = [](uint addr, byte data) {

          if (data == 0xC0) {  // Special CocoSDC Command Mode
            if (T::peek(0xFF40) == 0x43) {
              byte special_cmd = T::peek(0xFF49);
              printf("- yak - Special CocoSDC Command Mode: %x %x %x\n",
                     special_cmd, T::peek(0xFF4A), T::peek(0xFF4B));
              switch (special_cmd) {
                case 'Q':
                  T::poke(0xFF49, 0x00);
                  T::poke(0xFF4A, 0x02);
                  T::poke(0xFF4B, 0x10);  // Say size $000210 sectors.
                  break;
                case 'g':  // Set global flags.
                  // llcocosdc.0250230904: [Secondary command to "Set Global
                  // Flags"] [Disable Floppy Emulation capability in SDC
                  // controller] uses param $FF80
                  T::poke(0xFF49, 0x00);
                  T::poke(0xFF4A, 0x00);
                  T::poke(0xFF4B, 0x00);
                  break;
                default:
                  T::poke(0xFF49, 0x00);
                  T::poke(0xFF4A, 0x00);
                  T::poke(0xFF4B, 0x00);
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
          sdc_lsn = (T::peek(0xFF49) << 16) | (T::peek(0xFF4A) << 8) | (0xFF & data);
          printf("SET SDC SECTOR sdc_lsn=%x\n", sdc_lsn);
    };

  }; // Install
}; // struct DoCocosdc
#endif // _COCOSDC_H_
