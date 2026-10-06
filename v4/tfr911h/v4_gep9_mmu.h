#ifndef V4_GEP9_MMU_H_
#define V4_GEP9_MMU_H_

#include <cstdint>
#include <cstring>

#ifndef IN_RAM
#define IN_RAM __not_in_flash("tfr911")
#endif
#ifndef FORCE_INLINE
#define FORCE_INLINE inline __attribute__((always_inline))
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

using byte = unsigned char;
extern byte ram[256 * 1024];

// ═══════════════════════════════════════════════════════════════════
// gep9 MMU & Dynamic Page Allocation Subsystem
//
// Logical model: 256 tasks, each with 256 pages of 256 bytes (64 KB).
// Physical RAM: 256 KB on RP2350B co-processor = 1024 frames of 256 bytes.
//
// Key features:
//   - Frame 0 permanently zeroed: unallocated pages read as 0.
//   - On-demand write allocation from 1024-frame pool.
//   - Sparse task tables: up to 32 concurrently active tasks.
//   - PurgeTaskMem ($FF2F): releases all frames of a terminated task.
//   - Shared Memory Curtain ($FF28..$FF29, default $E000) for Tasks 0..2.
// ═══════════════════════════════════════════════════════════════════

#define GEP9_PAGE_SIZE        256
#define GEP9_PHYS_FRAMES     1024   // 256 KB / 256 bytes
#define GEP9_NUM_TASKS        256
#define GEP9_PAGES_PER_TASK   256
#define GEP9_INVALID_FRAME   0xFFFF
#define GEP9_ZERO_FRAME      0
#define GEP9_TABLE_SLOTS       32

typedef uint16_t FrameId;

struct Gep9MMU {
  // Pointers to 256-word page tables (512 bytes each)
  FrameId* task_tables[GEP9_NUM_TASKS];

  // Pre-allocated task table backing storage (32 active tasks = 16 KB)
  FrameId  table_storage[GEP9_TABLE_SLOTS][GEP9_PAGES_PER_TASK];
  uint32_t table_used_mask;
  uint8_t  task_to_slot[GEP9_NUM_TASKS];

  // Free physical frame pool (stack allocator for frames 1..1023)
  FrameId  free_frames[GEP9_PHYS_FRAMES];
  uint16_t free_head;
  uint16_t allocated_count;

  // Active task running on Hitachi 6309
  uint8_t  current_task;

  // TaskFuse mechanism ($FF20)
  bool     task_fuse_armed;
  uint8_t  task_fuse_target;
  uint8_t  task_fuse_step;

  // Task capabilities & security flags ($FF2D..$FF2E)
  uint8_t  task_flags_target;
  uint8_t  task_flags[GEP9_NUM_TASKS]; // Bit 0: TASK_FLAG_IO

  // Shared memory curtain ($FF28..$FF29, default $E000)
  uint16_t shared_curtain;

  void Init() {
    // 1. Frame 0 is permanently zeroed
    memset(&ram[0], 0, GEP9_PAGE_SIZE);

    // 2. Initialize free frame stack (frames 1..1023)
    free_head = 0;
    for (FrameId f = 1; f < GEP9_PHYS_FRAMES; f++) {
      free_frames[free_head++] = f;
    }
    allocated_count = 0;

    // 3. Clear task table directory & storage
    table_used_mask = 0;
    for (int t = 0; t < GEP9_NUM_TASKS; t++) {
      task_tables[t] = nullptr;
      task_flags[t] = 0;
      task_to_slot[t] = 0;
    }

    // 4. Reserve Slot 0 permanently for Task 0 (Kernel)
    table_used_mask |= (1U << 0);
    task_to_slot[0] = 0;
    task_tables[0] = table_storage[0];
    for (int p = 0; p < GEP9_PAGES_PER_TASK; p++) {
      table_storage[0][p] = GEP9_INVALID_FRAME;
    }

    // 5. Default state
    current_task = 0;
    task_fuse_armed = false;
    task_fuse_target = 0;
    task_fuse_step = 0;
    shared_curtain = 0xE000;
    task_flags_target = 1;
  }

  FrameId AllocFrame() {
    if (free_head == 0) {
      Say('['); Say('O'); Say('O'); Say('M'); Say(']');
      return GEP9_INVALID_FRAME; // Out of frames
    }
    FrameId f = free_frames[--free_head];
    allocated_count++;
    memset(&ram[f << 8], 0, GEP9_PAGE_SIZE);
    return f;
  }

  void FreeFrame(FrameId f) {
    if (f == GEP9_ZERO_FRAME || f == GEP9_INVALID_FRAME || f >= GEP9_PHYS_FRAMES) {
      return;
    }
    free_frames[free_head++] = f;
    if (allocated_count > 0) {
      allocated_count--;
    }
  }

  FrameId* AllocTaskTable(uint8_t task) {
    if (task_tables[task] != nullptr) {
      return task_tables[task];
    }
    for (uint32_t s = 0; s < GEP9_TABLE_SLOTS; s++) {
      if (!(table_used_mask & (1U << s))) {
        table_used_mask |= (1U << s);
        task_to_slot[task] = (uint8_t)s;
        task_tables[task] = table_storage[s];
        for (int p = 0; p < GEP9_PAGES_PER_TASK; p++) {
          table_storage[s][p] = GEP9_INVALID_FRAME;
        }
        return task_tables[task];
      }
    }
    Say('['); Say('N'); Say('O'); Say('T'); Say('B'); Say('L'); Say(']');
    return nullptr; // No table slots available
  }

  void FreeTaskTable(uint8_t task) {
    if (task == 0) return; // Never free Task 0
    FrameId* tbl = task_tables[task];
    if (!tbl) return;

    for (int p = 0; p < GEP9_PAGES_PER_TASK; p++) {
      if (tbl[p] != GEP9_INVALID_FRAME) {
        FreeFrame(tbl[p]);
        tbl[p] = GEP9_INVALID_FRAME;
      }
    }

    uint8_t s = task_to_slot[task];
    table_used_mask &= ~(1U << s);
    task_tables[task] = nullptr;
  }

  void PurgeTask(uint8_t task) {
    if (task == 0) return; // Protected kernel task
    FreeTaskTable(task);
    task_flags[task] = 0;
  }

  int CountTaskFrames(uint8_t t) const {
    if (!task_tables[t]) return 0;
    int count = 0;
    for (int p = 0; p < GEP9_PAGES_PER_TASK; p++) {
      if (task_tables[t][p] != GEP9_INVALID_FRAME) {
        count++;
      }
    }
    return count;
  }

  FORCE_INLINE byte* IN_RAM TranslateRead(uint8_t task, uint16_t addr) {
    // 1. Shared Memory Curtain (Tasks 0, 1, 2)
    if (task <= 2 && addr >= shared_curtain && addr < 0xFF00) {
      task = 0;
    }

    // 2. Lookup task table
    FrameId* tbl = task_tables[task];
    if (unlikely(!tbl)) {
      return &ram[addr & 0xFF]; // Returns 0 from ZERO_FRAME
    }

    FrameId f = tbl[addr >> 8];
    if (unlikely(f == GEP9_INVALID_FRAME)) {
      return &ram[addr & 0xFF]; // Returns 0 from ZERO_FRAME
    }

    return &ram[(f << 8) | (addr & 0xFF)];
  }

  FORCE_INLINE byte* IN_RAM TranslateWrite(uint8_t task, uint16_t addr) {
    // 1. Shared Memory Curtain (Tasks 0, 1, 2)
    if (task <= 2 && addr >= shared_curtain && addr < 0xFF00) {
      task = 0;
    }

    // 2. Lookup / allocate task table
    FrameId* tbl = task_tables[task];
    if (unlikely(!tbl)) {
      tbl = AllocTaskTable(task);
      if (unlikely(!tbl)) {
        return &ram[0]; // Emergency fallback if table pool exhausted
      }
    }

    // 3. Lookup / demand allocate physical frame
    FrameId f = tbl[addr >> 8];
    if (unlikely(f == GEP9_INVALID_FRAME)) {
      f = AllocFrame();
      if (unlikely(f == GEP9_INVALID_FRAME)) {
        return &ram[0]; // Emergency fallback if RAM pool exhausted
      }
      tbl[addr >> 8] = f;
    }

    return &ram[(f << 8) | (addr & 0xFF)];
  }
};

inline Gep9MMU gep9_mmu;

#endif  // V4_GEP9_MMU_H_
