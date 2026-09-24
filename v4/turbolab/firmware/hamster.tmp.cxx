// hamster.tmp.cxx — Runtime PioAssembler version of hamster.pio
// ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
// ; E = 1 ; pin 29
// ; Q = 2 ; pin 30
// ; RW pin 31
// ; Phase1 : 0 : neither
// ; Phase2 : 2 : just Q
// ; Phase3 : 3 : both
// ; Phase4 : 1 : just E
// ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

#include "pio_assembler.h"

// Side-Set Values
constexpr unsigned Phase1 = 0; // neither E nor Q
constexpr unsigned Phase2 = 2; // just Q
constexpr unsigned Phase3 = 3; // both E and Q
constexpr unsigned Phase4 = 1; // just E

// TUNABLE Side-Set Transition Offsets (clock ticks from label)
#define K1   9    // Start of Phase 2 in LOOP (after Phase 1 duration of 9 cycles)
#define K2   19   // Start of Phase 3 in LOOP (after Phase 2 duration of 10 cycles)
#define K3   11   // Start of Phase 4 in WRITE (after Phase 3 duration of 11 cycles)
#define K4   8    // Start of Phase 4 in READ (after Phase 3 duration of 8 cycles)

// TUNABLE Delay Durations (clock ticks)
#define T1   16   // Total delay in LOOP before pin capture (Phase 1 remainder + Phase 2)
#define T3   22   // Total delay in WRITE before pin capture (Phase 3 remainder + Phase 4)
#define T4   3    // Delay in READ before late pin capture (in Phase 3)
#define T5   12   // Delay in READ after push (in Phase 4)

inline PioAssembler build_hamster_program(bool verbose = true) {
    using A = PioAssembler;
    A hamster(/*sideset_bits=*/2);
    hamster.set_verbose(verbose);

    // Forward/backward label declarations upfront
    A::Label BEGIN = hamster.forward_reference("BEGIN");
    A::Label RESET = hamster.forward_reference("RESET");
    A::Label LOOP  = hamster.forward_reference("LOOP");
    A::Label WRITE = hamster.forward_reference("WRITE");
    A::Label READ  = hamster.forward_reference("READ");

    // Startup entry: jump to reset to ensure D[0:7] pindirs are configured as inputs
    hamster.label(BEGIN);
    hamster.jmp(RESET);

    hamster.wrap_target();
    hamster.label(LOOP);

    hamster.pull().block();
    hamster[T1];
    hamster.in(A::PINS, 32);
    hamster.mov(A::OSR, A::ISR);      // Save captured pins to OSR
    hamster.push();                   // Push address/control packet to Core 1 (clears ISR)
    hamster.out(A::ZERO, 31);         // Discard bits 0-30 from OSR
    hamster.out(A::Y, 1);             // Shift remaining bit (R/W) into Y
    hamster.jmp(A::Y_DECR, READ);      // Jump to READ if Y is nonzero (R/W=1), else fall through to WRITE

    hamster.label(WRITE);

    hamster[T3];                      // Delay until write data is stable on bus
    hamster.in(A::PINS, 32);          // Sample written data
    hamster.push();                   // Push written data packet to Core 1
    hamster.jmp(LOOP);                // Return to start of next cycle

    hamster.label(READ);

    hamster.mov(A::OSR, A::INV_ZERO); // Set all 1s in OSR
    hamster.out(A::PINDIRS, 8);       // Set D[0:7] to outputs
    hamster.pull(A::BLOCK);           // Wait for Core 1 to supply read data byte
    hamster.out(A::PINS, 8);          // Drive read data onto D[0:7]
    hamster[T4];                      // Wait for bus setup
    hamster.in(A::PINS, 32);          // Late pin sampling
    hamster.push();                   // Push late pin sample
    hamster[T5];                      // Hold data through Phase 4

    hamster.label(RESET);
    hamster.mov(A::OSR, A::ZERO);     // Set all 0s in OSR
    hamster.out(A::PINDIRS, 8);       // Restore D[0:7] pindirs to inputs (tri-state)

    hamster.wrap();

    // Declare SideSets, starting from labels plus cycle offsets
    hamster << A::SideSetStarting(BEGIN + 0, Phase1);
    hamster << A::SideSetStarting(RESET + 0, Phase1);
    hamster << A::SideSetStarting(LOOP + 0, Phase1);
    hamster << A::SideSetStarting(LOOP + K1, Phase2);
    hamster << A::SideSetStarting(LOOP + K2, Phase3);
    hamster << A::SideSetStarting(WRITE + 0, Phase3);
    hamster << A::SideSetStarting(WRITE + K3, Phase4);
    hamster << A::SideSetStarting(WRITE + 24, Phase1);
    hamster << A::SideSetStarting(READ + 0, Phase3);
    hamster << A::SideSetStarting(READ + K4, Phase4);
    hamster << A::SideSetStarting(READ + 21, Phase1);

    hamster.finish();
    return hamster;
}

/*
Example usage on RP2350 with Pico SDK:

    PioAssembler hamster = build_hamster_program();
    uint offset = hamster.pio_add_program(pio0);
    pio_sm_config cf = hamster.program_get_default_config(offset);
    // ... continue state machine initialization ...
*/
