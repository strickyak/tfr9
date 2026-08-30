#ifndef CENTIPEDE_FIRMWARE_HEURISTIC_FILE_H_
#define CENTIPEDE_FIRMWARE_HEURISTIC_FILE_H_

#include <string.h>
#include <ctype.h>
#include "vfs.h"

// Check if buffer looks like a DECB FAT
inline bool IsDecbFat(const uint8_t* buf, int expected_granules) {
  // Check that unused bytes are 0x00 or 0xFF (empty track padding)
  for (int i = expected_granules; i < 256; i++) {
    if (buf[i] != 0x00 && buf[i] != 0xFF) return false;
  }
  
  // Check that used bytes are valid FAT entries
  int valid = 0;
  for (int i = 0; i < expected_granules; i++) {
    uint8_t b = buf[i];
    if (b == 0xFF || b < expected_granules || (b >= 0xC0 && b <= 0xC9)) {
      valid++;
    }
  }
  // Allow a few corruptions, but mostly valid
  return valid >= (expected_granules * 9 / 10);
}

inline const char* HeuristicFileType(std::shared_ptr<VfsNode> node) {
  if (!node) return "unknown";

  struct vfs_info info;
  if (node->stat(&info) == 0) {
    if (info.type == LFS_TYPE_DIR) {
      if (node->get_name() == "pc") return "mount";
      return "directory";
    }
  }

  if (node->open_file(LFS_O_RDONLY) < 0) {
    return "unknown";
  }

  // Get size by seeking to end
  lfs_soff_t size = node->seek(0, LFS_SEEK_END, nullptr);
  if (size < 0) size = 0;
  node->seek(0, LFS_SEEK_SET, nullptr);

  uint8_t buf[256];
  lfs_ssize_t read_bytes = node->read(buf, 256);
  if (read_bytes <= 0) {
    node->close_file(nullptr);
    return "empty";
  }

  // 1. OS-9 Disk Check (LSN0)
  if (size >= 161280 && size % 256 == 0) {
    uint32_t total_sectors = (buf[0] << 16) | (buf[1] << 8) | buf[2];
    uint8_t sectors_per_track = buf[3];
    if ((total_sectors == size / 256) && (sectors_per_track == 18 || sectors_per_track == 36)) {
      node->close_file(nullptr);
      return "os9-disk";
    }
  }

  // 2. DECB Disk Check
  if (size >= 161280 && size % 256 == 0) {
    uint8_t fat_buf[256];
    
    // Check Single-Sided FAT at Track 17, Sector 2 (Offset 78592)
    if (size >= 78848) {
      node->seek(78592, LFS_SEEK_SET, nullptr);
      if (node->read(fat_buf, 256) == 256) {
        // 68 granules (35 tracks) or 78 granules (40 tracks)
        if (IsDecbFat(fat_buf, 68) || IsDecbFat(fat_buf, 78)) {
          node->close_file(nullptr);
          return "decb-disk-ss";
        }
      }
    }
    
    // Check Double-Sided FAT at Track 17, Side 0, Sector 2 (Offset 156928)
    if (size >= 157184) {
      node->seek(156928, LFS_SEEK_SET, nullptr);
      if (node->read(fat_buf, 256) == 256) {
        // 136 granules (35 tracks DS) or 156 granules (40 tracks DS)
        if (IsDecbFat(fat_buf, 136) || IsDecbFat(fat_buf, 156)) {
          node->close_file(nullptr);
          return "decb-disk-ds";
        }
      }
    }
  }

  // 3. DECB Binary Check
  if (buf[0] == 0x00 && size >= 5) {
    int len = (buf[1] << 8) | buf[2];
    if (len > 0 && len <= size - 5) {
      node->seek(5 + len, LFS_SEEK_SET, nullptr);
      uint8_t next_byte;
      if (node->read(&next_byte, 1) == 1) {
        if (next_byte == 0x00 || next_byte == 0xFF) {
          node->close_file(nullptr);
          return "decb-binary";
        }
      }
    }
  }

  node->close_file(nullptr);

  // 4. Zip Archive Check
  if (size >= 4 && buf[0] == 'P' && buf[1] == 'K' && buf[2] == 0x03 && buf[3] == 0x04) {
    return "zip archive";
  }

  // 5. Text vs Binary Check
  bool is_text = true;
  for (int i = 0; i < read_bytes; i++) {
    uint8_t b = buf[i];
    if (b > 127) {
      is_text = false;
      break;
    }
    // Allow printable ASCII, CR, LF, TAB
    if (b < 32 && b != '\r' && b != '\n' && b != '\t') {
      is_text = false;
      break;
    }
  }

  if (is_text) {
    return "text";
  }

  return "binary";
}

#endif // CENTIPEDE_FIRMWARE_HEURISTIC_FILE_H_
