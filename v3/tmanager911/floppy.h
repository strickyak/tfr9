#ifndef _FLOPPY_H_
#define _FLOPPY_H_

constexpr static byte FloppyDeviceStart = 64;

/*
    strick@nand:~/modoc/coco-shelf/tfr9/v3$ g '^cy . ff4'  _log
    cy r ff40 ff #1135599                  // CLR (reads before it writes)
    cy w ff40 00 #1135601                  // CLR
    cy w ff48 d0 #1135608  (A)  208        // Force Interrupt but low nyb "0"
   means No interrupt, just stop any current command. cy r ff48 ff #1135629  (A)
   255 cy w ff40 29 #1598858  (A) `)` [)]#
    strick@nand:~/modoc/coco-shelf/tfr9/v3$

    strick@nand:~/modoc/coco-shelf/tfr9/v3$ - decb dskini -4 generated/floppy0
    strick@nand:~/modoc/coco-shelf/tfr9/v3$ - decb copy count.bas -0 -a -l
   generated/floppy0,count.bas strick@nand:~/modoc/coco-shelf/tfr9/v3$ - decb
   dir generated/floppy0

    cy r ff40 ff #1135599
    cy w ff40 00 #1135601
    cy w ff48 d0 #1135608  (A)  208
    cy r ff48 00 #1135629  (A)   0 [@]#
    cy w ff40 29 #2025246  (A) `)` [)]#
    cy r ff48 00 #3103488  (A)   0 [@]#
    cy w ff49 00 #3103558  (B)   0 [@]#
    cy w ff4b 11 #3103574  (A)  17 [Q]#
    cy w ff48 17 #3103585  (A)  23 [W]#
    cy r ff48 00 #3103626  (A)   0 [@]#
    cy w ff4a 02 #3175740  (A)   2 [B]#
    cy r ff48 00 #3175759  (A)   0 [@]#
    cy w ff48 80 #3175796  (B)  128
    cy r ff48 00 #3175823
    cy r ff48 00 #3175838
    cy r ff48 00 #3175853
    cy r ff48 00 #3175868
    cy r ff48 00 #3175883


*/

#if 0
struct floppy_data {
    int num;
    const char* data;
} FloppyData [] = {
    { 306, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" },
    { 307, "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xc1\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" },
    { 308, "COUNT   BAS\x00\xff\"\x00(\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" },
    { 324, "10 FOR I=1 TO 100\r20 PRINT I;\r30 NEXT I\r\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" },
    { -1, NULL },
};
#endif

byte drive_selected;
bool drive_density;

byte floppy_status;
byte floppy_command;
byte floppy_track;
byte floppy_sector;

uint floppy_i;
byte floppy_buf[256];

template <typename T>
struct DontDrive {
  constexpr static bool DoesDrive() { return false; }
};

template <typename T>
struct DoDrive {
  constexpr static bool DoesDrive() { return true; }

  void static Drive_Install(uint base_addr) {
    drive_selected = 255;

    IOWriters[(255 & base_addr) + 0] = [](uint addr,
                                          byte data) {  // Write Command
      if ((data & 0x4F) == 0x09) {
        drive_selected = 0;
      } else if ((data & 0x4F) == 0x0A) {
        drive_selected = 1;
      } else if ((data & 0x4F) == 0x0C) {
        drive_selected = 2;
      } else if ((data & 0x4F) == 0x48) {
        drive_selected = 3;
      } else {
        drive_selected = 255;
      }

      drive_density = ((data & 0x20) == 0x20);
    };
    IOReaders[(255 & base_addr) + 0] = [](uint addr,
                                          byte data) {  // Read Status
      return drive_selected;
    };
  }
};

//////////////////////////////////////////////

template <typename T>
struct DontFloppy {
  constexpr static bool DoesFloppy() { return false; }
};

template <typename T>
struct DoFloppy {
  constexpr static bool DoesFloppy() { return true; }

  void static Floppy_Install(uint base_addr) {
    ///// Command/Status

    IOWriters[(255 & base_addr) + 0] = [](uint addr,
                                          byte data) {  // (Write) Command
      nmi_needed = 0;
      floppy_command = data;
      printf("===== FLOPPY Command $%x=%d.\n", floppy_command, floppy_command);

      if (floppy_command == 0xD0) {
        // Force Interrupt (stop amy command) but don't interrupt.
        floppy_status = 0;  // not busy; no error.
      } else if (floppy_command == 0x29) {
        // Step.   (Motor on.)
        floppy_status = 0;                           // not busy; no error.
      } else if ((floppy_command & 0xF0) == 0x10) {  // Seek comand
        floppy_status = 0;                           // not busy; no error.
        floppy_track = floppy_buf[0];  // track number was written to data port
        printf("FLOPPY seek $%x\n", floppy_track);

      } else if (floppy_command == 0x80) {
        Floppy_ReadSector();

        floppy_status = 2;  // not busy; no error.
      } else if (floppy_command == 0xA0) {
        Floppy_WriteSector();

        floppy_status = 2;  // not busy; no error.
      } else {
        printf("FLOPPY Command unknown: $%x=%d.\n", floppy_command,
               floppy_command);
        floppy_status = 0x1C;  // lots of errors
        // T::DumpRamAndGetStuck("Floppy Command Unknown", floppy_command);
      }
    };

    IOReaders[(255 & base_addr) + 0] = [](uint addr,
                                          byte data) {  // (Read) Status
      printf("===== FLOPPY Status $%x=%d.\n", floppy_status, floppy_status);
      return floppy_status;
    };

    ///// Track

    IOWriters[(255 & base_addr) + 1] = [](uint addr,
                                          byte data) {  // Write Track Number
      floppy_track = data;
      floppy_i = 0;
    };

    IOReaders[(255 & base_addr) + 1] = [](uint addr,
                                          byte data) {  // Read Track Number
      return floppy_track;
    };

    ///// Sector

    IOWriters[(255 & base_addr) + 2] = [](uint addr,
                                          byte data) {  // Write Sector Number
      floppy_sector = data;
      floppy_i = 0;
    };

    IOReaders[(255 & base_addr) + 2] = [](uint addr,
                                          byte data) {  // Read Sector Number
      return floppy_sector;
    };

    ///// Data

    IOWriters[(255 & base_addr) + 3] = [](uint addr, byte data) {  // Write Data
      if (floppy_i < 256) {
        floppy_status = 0x02;
        floppy_buf[floppy_i] = data;
        printf("FLOPPY WRITE DATA [%d++] z=%x\n", floppy_i, data);
        floppy_i++;
      }

      if (floppy_i == 256) {
        nmi_needed = 1;
        printf("nmi_needed because now floppy_i = %x; Floppy_FinishWrite.\n", floppy_i);
        Floppy_FinishWrite();
        floppy_i = 0;
      }
    };

    IOReaders[(255 & base_addr) + 3] = [](uint addr, byte data) {  // Read Data
      uint z = 0;
      if (floppy_i < 256) {
        z = floppy_buf[floppy_i];
        floppy_status = 0x02;
        printf("FLOPPY READ DATA [%d++] z=%x\n", floppy_i, z);
        floppy_i++;
      }

      if (floppy_i == 256) {
        nmi_needed = 1;
        printf("nmi_needed because now floppy_i = %x\n", floppy_i);
        floppy_status = 0;
        floppy_i = 0;
      } 

      return z;
    };
  }

#if 0
  static void Floppy_ReadSector() {
      // First, need to read the disk sector into floppy_buf.
      memset(floppy_buf, 255, 256);

      uint lsn = 18 * floppy_track + floppy_sector-1;
      printf("FLOPPY READ-SECTOR: t %d s %d lsn %d\n", floppy_track, floppy_sector, lsn);

      for ( int i = 0; FloppyData[i].num >= 0; i++ ) {
          if (FloppyData[i].num  == lsn) {
              memcpy(floppy_buf, FloppyData[i].data, 256);
              break;
          }
      }

      floppy_i = 0;
      floppy_status = 0x02;
  }
#endif
  /*
byte drive_selected;
bool drive_density;

byte floppy_status;
byte floppy_command;
byte floppy_track;
byte floppy_sector;

uint floppy_i;
byte floppy_buf[256];
*/

  static void Floppy_ReadSector() {
    // First, need to read the disk sector into floppy_buf.
    memset(floppy_buf, 255, 256);

    uint lsn = 18 * floppy_track + floppy_sector - 1;
    printf("FLOPPY READ-SECTOR: t %d s %d lsn %d\n", floppy_track,
           floppy_sector, lsn);

    ++quiet_ram;
    putbyte(C_DISK_READ);
    putsz(4);
    putbyte(FloppyDeviceStart + drive_selected);
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
          floppy_buf[k] = disk_input.Take();
        }
        break;
      }
    }

    floppy_i = 0;
    floppy_status = 0x02;
  }

  static void Floppy_WriteSector() {
    uint lsn = 18 * floppy_track + floppy_sector - 1;
    printf("FLOPPY WRITE-SECTOR START: t %d s %d lsn %d\n", floppy_track,
           floppy_sector, lsn);
  }
  static void Floppy_FinishWrite() {
    uint lsn = 18 * floppy_track + floppy_sector - 1;
    printf("FLOPPY WRITE-SECTOR FINISH: t %d s %d lsn %d\n", floppy_track,
           floppy_sector, lsn);

    ++quiet_ram;
    putbyte(C_DISK_WRITE);
    putsz(4 + 256);
    putbyte(FloppyDeviceStart + drive_selected);
    putbyte(lsn >> 16);
    putbyte(lsn >> 8);
    putbyte(lsn >> 0);
    --quiet_ram;

    for (uint k = 0; k < 256; k++) {
      putbyte(floppy_buf[k]);
    }

    floppy_i = 0;
    floppy_status = 0x02;
  }
};

#endif  // _FLOPPY_H_
