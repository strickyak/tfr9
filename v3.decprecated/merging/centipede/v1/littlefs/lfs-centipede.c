#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "lfs.h"

#include "lfs-centipede.h"

// 1. Read Method
int pico_lfs_read(const struct lfs_config *c, lfs_block_t block, 
                  lfs_off_t off, void *buffer, lfs_size_t size) {
    // Calculate the absolute flash address for memory-mapped XIP reading
    uint32_t flash_target_addr = XIP_BASE + LFS_FLASH_OFFSET + 
                                 (block * c->block_size) + off;

    // Read directly out of the RP2350 memory-mapped XIP space
    memcpy(buffer, (const void *)flash_target_addr, size);
    
    return LFS_ERR_OK;
}

// 2. Program (Write) Method
int __not_in_flash_func(pico_lfs_prog)(const struct lfs_config *c, lfs_block_t block, 
                                       lfs_off_t off, const void *buffer, lfs_size_t size) {
    // Calculate the offset relative to the beginning of the flash chip
    uint32_t flash_target_offset = LFS_FLASH_OFFSET + (block * c->block_size) + off;
    
    // Core 0 must lock out interrupts before modifying flash
    uint32_t ints = save_and_disable_interrupts();
    
    flash_range_program(flash_target_offset, (const uint8_t *)buffer, size);
    
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

// 3. Erase Method
int __not_in_flash_func(pico_lfs_erase)(const struct lfs_config *c, lfs_block_t block) {
    uint32_t flash_target_offset = LFS_FLASH_OFFSET + (block * c->block_size);
    
    uint32_t ints = save_and_disable_interrupts();
    
    // LittleFS erases an entire block at a time (usually c->block_size = 4096)
    flash_range_erase(flash_target_offset, c->block_size);
    
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

// 4. Sync Method
int pico_lfs_sync(const struct lfs_config *c) {
    // The Pico hardware flash functions write synchronously to the chip.
    // There is no lazy hardware caching behind flash_range_program, 
    // so this is simply a no-op that returns success.
    return LFS_ERR_OK;
}
