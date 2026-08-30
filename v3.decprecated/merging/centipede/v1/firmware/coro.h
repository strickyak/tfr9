#ifndef CENTIPEDE_FIRMWARE_CORO_H_
#define CENTIPEDE_FIRMWARE_CORO_H_

// Minimal cooperative coroutines for RP2350 (ARM Cortex-M33).
//
// Usage:
//   uint8_t my_stack[4096] __attribute__((aligned(8)));
//   Coro my_coro;
//   coro_create(&my_coro, my_function, my_stack, sizeof(my_stack));
//   coro_resume(&my_coro);  // Runs until my_function yields or returns
//
// Inside the coroutine function:
//   void my_function(Coro& self) {
//       while (true) {
//           do_work();
//           coro_yield(&self);  // Return to scheduler
//       }
//   }
//
// =====================================================================
// STACK BUDGET (Aug 2, 2026)
// =====================================================================
//
// Coroutine stacks are 4096 bytes.  This is tight.  The deepest call
// chain is in the spoon coroutine when AUTO_GLOB wraps a command
// through fs_cmd:
//
//   BackgroundSpoonFeeder      ~800 bytes  (history[20], line[256])
//     Tcl_Eval #1              ~300 bytes  (parser state, argv)
//       fs_cmd                 ~400 bytes  (3× std::vector, std::string, char[260])
//         glob()               ~200 bytes  (std::vector<std::string>)
//         Tcl_Eval #2          ~300 bytes  (recursive from fs_cmd)
//           cd_cmd / ls_cmd    ~100 bytes
//             vfs_stat          ~100 bytes
//               vfs_rpc_call   ~100 bytes
//                 PumpUsbCobs  ~200 bytes  (if self==nullptr fallback)
//                 ─────────────────────
//                 TOTAL:       ~2500 bytes
//
// That leaves ~1500 bytes of headroom.  With STL temporaries, alignment
// padding, and function prologues, this is dangerously close to overflow.
//
// RULES TO AVOID STACK OVERFLOW:
//
// 1. In vfs_rpc_call: when a Coro* is available, yield instead of
//    calling PumpUsbCobs().  The scheduler pumps on its own large stack
//    between every coro_resume().  Only fall back to PumpUsbCobs when
//    self==nullptr (can't yield).
//
// 2. Avoid large stack-allocated buffers inside functions called from
//    the spoon coroutine.  Use static or heap-allocated buffers for
//    anything over ~64 bytes.  Key offenders to watch:
//      - BackgroundSpoonFeeder: line[256], history[20] (already static)
//      - fs_cmd: char new_line[260], multiple std::vector/std::string
//      - glob(): std::vector<std::string> results
//      - Tcl_Eval: parser workspace
//      - cobs_printf: char buf[256]
//
// 3. Pass Coro* self through the call chain wherever possible, so
//    vfs_rpc_call can yield instead of busy-polling.
//
// 4. To debug stack overflow: place a canary value at the bottom of
//    each stack array (e.g. stack[0..3] = 0xDEADBEEF) and check it
//    periodically in the scheduler.  If corrupted, the coroutine
//    overflowed.
//

#include <csetjmp>
#include <cstdint>

struct Coro {
  jmp_buf caller_ctx;   // Saved context of whoever called resume()
  jmp_buf coro_ctx;     // Saved context of the coroutine
  bool ready;           // Has been initialized via coro_create
  bool finished;        // The coroutine function has returned
  void (*func)(Coro&);  // The coroutine's entry function
  uint8_t* stack_base;  // Bottom of the stack allocation
  uint32_t stack_size;  // Total allocated stack size in bytes
};

// Internal: the Coro* being set up during coro_create.
// Safe because only one background core runs coro_create.
static Coro* _coro_creating = nullptr;

// Internal: switch SP and branch to the entry point.
// Naked function: no prologue/epilogue, r0=new_sp, r1=entry.
__attribute__((naked)) static void _coro_switch_sp_and_branch(
    uint32_t /*new_sp*/, void (* /*entry*/)()) {
  asm volatile(
      "mov sp, r0\n"  // Set stack pointer to the new stack
      "bx r1\n"       // Branch to entry function
  );
}

// Internal: entry point that runs on the coroutine's stack.
// Called once during coro_create to set up coro_ctx, then
// called again each time the coroutine is resumed.
__attribute__((noinline)) static void _coro_entry_point() {
  Coro* c = _coro_creating;

  // Save this context (on the new stack) into coro_ctx.
  // Then longjmp back to coro_create's setjmp.
  if (setjmp(c->coro_ctx) == 0) {
    longjmp(c->caller_ctx, 1);
    // Does not return here during setup.
  }

  // When coro_resume longjmps to coro_ctx, we land here.
  c->func(*c);

  // The coroutine function returned.
  c->finished = true;
  longjmp(c->caller_ctx, 1);  // Return to the last coro_resume
}

// Initialize a coroutine with the given function and stack.
// Does NOT run the function — call coro_resume to start it.
static void coro_create(Coro* c, void (*func)(Coro&), uint8_t* stack_base,
                        uint32_t stack_size) {
  c->func = func;
  c->finished = false;
  c->ready = false;
  c->stack_base = stack_base;
  c->stack_size = stack_size;
  _coro_creating = c;

  if (setjmp(c->caller_ctx) == 0) {
    // 8-byte aligned stack top (ARM AAPCS requirement).
    uint32_t stack_top = ((uint32_t)(stack_base + stack_size)) & ~7u;
    _coro_switch_sp_and_branch(stack_top, _coro_entry_point);
    // Does not return — _coro_entry_point longjmps back.
  }
  // longjmp from _coro_entry_point returns here.
  // coro_ctx is now set up pointing to the new stack.
  c->ready = true;
}

// Resume a coroutine. Returns when the coroutine yields or finishes.
static inline void coro_resume(Coro* c) {
  if (c->finished || !c->ready) return;
  if (setjmp(c->caller_ctx) == 0) {
    longjmp(c->coro_ctx, 1);  // Jump into the coroutine
  }
  // Coroutine yielded or finished — we're back.
}

// Yield from inside a coroutine. Returns when the coroutine is resumed.
static inline void coro_yield(Coro* c) {
  if (setjmp(c->coro_ctx) == 0) {
    longjmp(c->caller_ctx, 1);  // Return to coro_resume
  }
  // coro_resume called us again — continue.
}

// Read the current ARM stack pointer.
static inline uint32_t _coro_get_sp() {
  uint32_t sp;
  asm volatile("mov %0, sp" : "=r"(sp));
  return sp;
}

// Return how many bytes of this coroutine's stack are currently in use.
// Must be called from INSIDE the coroutine (i.e. while it is running).
static inline uint32_t coro_stack_used(Coro* c) {
  uint32_t stack_top = ((uint32_t)(c->stack_base + c->stack_size)) & ~7u;
  uint32_t sp = _coro_get_sp();
  return stack_top - sp;
}

// Return how many bytes of this coroutine's stack are still free.
// Must be called from INSIDE the coroutine (i.e. while it is running).
static inline uint32_t coro_stack_free(Coro* c) {
  uint32_t sp = _coro_get_sp();
  return sp - (uint32_t)c->stack_base;
}

#endif  // CENTIPEDE_FIRMWARE_CORO_H_
