#ifndef LFS_CENTIPEDE_H_
#define LFS_CENTIPEDE_H_

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "lfs.h"
#include "pico/stdlib.h"

// Copico Centipede 32z has a W25Q128 (U4) which is 16 MB flash storage.
// Use 11 MB of Flash for the littlefs centered in the 16 MB flash,
// skipping 4MB at front and 1MB at back.

// Define where your LittleFS partition starts relative to the beginning of
// flash
#define LFS_FLASH_OFFSET (4 * 1024 * 1024)
#define LFS_FLASH_SIZE (11 * 1024 * 1024)

#define LFS_BLOCK_SIZE 4096  // hardware-dependant
#define LFS_BLOCK_COUNT (LFS_FLASH_SIZE / LFS_BLOCK_SIZE)

// 1. Read Method
int pico_lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                  void *buffer, lfs_size_t size);

// 2. Program (Write) Method
int __not_in_flash_func(pico_lfs_prog)(const struct lfs_config *c,
                                       lfs_block_t block, lfs_off_t off,
                                       const void *buffer, lfs_size_t size);

// 3. Erase Method
int __not_in_flash_func(pico_lfs_erase)(const struct lfs_config *c,
                                        lfs_block_t block);

// 4. Sync Method
int pico_lfs_sync(const struct lfs_config *c);

// 5. Write 256-byte flash label page at the last 4 KB sector of 16 MB flash (0x00FFF000 -> XIP 0x10FFF000).
// Must only be called from Core 0 in Phase 1 (before Core 1 runs a guest bus loop).
#define LABEL_FLASH_OFFSET (16 * 1024 * 1024 - 4096)
int __not_in_flash_func(pico_flash_write_label)(const uint8_t *page256);

#endif  // LFS_CENTIPEDE_H_
