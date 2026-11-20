// Host unit test for Gep9MMU (always-active 2-level EMBIGGEN MMU).
#include <cstdio>
#include <cstdlib>

// Stubs required by gep9_mmu.h on host PC (no Pico SDK):
#define IN_RAM
#define __not_in_flash(x)
using byte = unsigned char;
byte ram[256 * 1024];
static inline void Say(char) {}

#include "gep9_mmu.h"

static int g_fails = 0;
#define CHECK(cond, fmt, ...) do { \
  if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    g_fails++; \
  } \
} while (0)

int main() {
  gep9_mmu.Init();

  // 1. Initial state: Task 0 owns slot 0; user tasks unallocated; default MMAP = 0..7.
  CHECK(gep9_mmu.HasTaskSlot(0), "Task 0 has permanent slot");
  CHECK(!gep9_mmu.HasTaskSlot(1), "Task 1 initially unallocated");
  CHECK(gep9_mmu.allocated_count == 0, "0 frames allocated initially");
  for (int s = 0; s < 8; s++) {
    CHECK(gep9_mmu.task_mmap[1][s] == s, "Task 1 slot %d identity", s);
  }

  // Reading unallocated page returns 0 without allocating frames or slots.
  CHECK(*gep9_mmu.TranslateRead(3, 0x4123) == 0, "unallocated read returns 0");
  CHECK(!gep9_mmu.HasTaskSlot(3) && gep9_mmu.allocated_count == 0, "read did not allocate");

  // 2. Remapping 8 KB windows ($FF40..$FF47 -> task_mmap) & per-task isolation.
  gep9_mmu.task_mmap[3][2] = 20; // Slot 2 ($4000..$5FFF) -> physical block 20
  *gep9_mmu.TranslateWrite(3, 0x4100) = 0xCD;
  CHECK(gep9_mmu.HasTaskSlot(3), "Task 3 slot allocated on write");
  CHECK(*gep9_mmu.TranslateRead(3, 0x4100) == 0xCD, "read back 0xCD via slot 2 -> block 20");
  CHECK(*gep9_mmu.TranslateRead(4, 0x4100) == 0x00, "Task 4 isolated from Task 3");

  // Restore slot 2 to block 2 (unwritten), map slot 5 ($A000..$BFFF) to block 20.
  gep9_mmu.task_mmap[3][2] = 2;
  CHECK(*gep9_mmu.TranslateRead(3, 0x4100) == 0x00, "block 2 still zero");
  gep9_mmu.task_mmap[3][5] = 20;
  CHECK(*gep9_mmu.TranslateRead(3, 0xA100) == 0xCD, "block 20 visible via slot 5");

  // 3. Shared Memory Curtain ($E000..$FEFF for Tasks 0..2).
  *gep9_mmu.TranslateWrite(0, 0xE200) = 0x99;
  CHECK(*gep9_mmu.TranslateRead(1, 0xE200) == 0x99, "Task 1 sees Task 0 via default curtain");
  CHECK(*gep9_mmu.TranslateRead(3, 0xE200) == 0x00, "Task 3 (>2) does not see curtain");

  // Task 1 remaps slot 7 away from 7 -> gets private block instead of Task 0 curtain.
  gep9_mmu.task_mmap[1][7] = 15;
  *gep9_mmu.TranslateWrite(1, 0xE200) = 0x44;
  CHECK(*gep9_mmu.TranslateRead(1, 0xE200) == 0x44, "Task 1 private block 15 at $E200");
  CHECK(*gep9_mmu.TranslateRead(0, 0xE200) == 0x99, "Task 0 $E200 unchanged");

  // 4. Vectors ($FFF0..$FFFF) stay pinned to Task 0 block 7 even if Task 0 remaps slot 7.
  *gep9_mmu.TranslateVectorWrite(0xFFFE) = 0x12;
  *gep9_mmu.TranslateVectorWrite(0xFFFF) = 0x34;
  gep9_mmu.task_mmap[0][7] = 30;
  CHECK(*gep9_mmu.TranslateVectorRead(0xFFFE) == 0x12 &&
        *gep9_mmu.TranslateVectorRead(0xFFFF) == 0x34, "vectors remain in Task 0 block 7");
  gep9_mmu.task_mmap[0][7] = 7;

  // 5. 24-bit DMA / disk physical addressing (addr24 >= 0x10000 -> blocks 8..255).
  *gep9_mmu.TranslateWrite24(3, 0x018005) = 0x7E; // 0x018000 / 8192 = block 12, offset 5
  gep9_mmu.task_mmap[3][1] = 12;
  CHECK(*gep9_mmu.TranslateRead(3, 0x2005) == 0x7E, "24-bit write to block 12 visible via slot 1");
  CHECK(*gep9_mmu.TranslateRead24(3, 0x018005) == 0x7E, "24-bit read from block 12");

  // 6. PurgeTask releases all Task 3 frames, slot, MMAP overrides, and flags.
  uint16_t before_purge = gep9_mmu.allocated_count;
  gep9_mmu.task_flags[3] = 0x01;
  gep9_mmu.PurgeTask(3);
  CHECK(!gep9_mmu.HasTaskSlot(3), "Task 3 slot freed on PurgeTask");
  CHECK(gep9_mmu.CountTaskFrames(3) == 0, "Task 3 has 0 frames after PurgeTask");
  CHECK(gep9_mmu.allocated_count < before_purge, "physical frames returned to free pool");
  CHECK(gep9_mmu.task_flags[3] == 0, "Task 3 flags cleared");
  for (int s = 0; s < 8; s++) {
    CHECK(gep9_mmu.task_mmap[3][s] == s, "Task 3 MMAP slot %d reset to %d", s, s);
  }

  if (g_fails) return 1;
  printf("gep9_mmu_test: OK\n");
  return 0;
}
