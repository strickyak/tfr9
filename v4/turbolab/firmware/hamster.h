// hamster.h — Header-only runtime PIO assembler program and configuration
#pragma once

#include "pio_assembler.h"

#if RUNTIME_PIO_ASSEMBLER

// Side-Set Values for 6309E bus quadrature clocks (E on GPIO 29, Q on GPIO 30)
constexpr unsigned Phase1 = 0; // neither E nor Q
constexpr unsigned Phase2 = 2; // just Q
constexpr unsigned Phase3 = 3; // both E and Q
constexpr unsigned Phase4 = 1; // just E

// Tunable system clock frequency (MHz)
inline uint32_t tuning_mhz = 250;

// Tunable Side-Set transition offsets (clock ticks from label, 1-indexed K1..K9)
// Default values calibrated for 250MHz system clock / ~3.3MHz 6309 bus:
//   K1 = 9   (Phase 2 start in LOOP)
//   K2 = 19  (Phase 3 start in LOOP)
//   K3 = 11  (Phase 4 start in WRITE)
//   K4 = 8   (Phase 4 start in READ)
inline uint32_t tuning_k[10] = {0, 9, 19, 11, 8, 0, 0, 0, 0, 0};

// Tunable delay durations (clock ticks, 1-indexed T1..T9)
// Default values calibrated for 250MHz system clock / ~3.3MHz 6309 bus:
//   T1 = 16  (delay in LOOP before pin capture)
//   T2 = 0   (optional delay between pin capture and push)
//   T3 = 22  (delay in WRITE before pin capture)
//   T4 = 3   (delay in READ before late pin capture)
//   T5 = 12  (delay in READ Phase 4 trailing hold)
inline uint32_t tuning_t[10] = {0, 16, 0, 22, 3, 12, 0, 0, 0, 0};

inline void reset_tuning_to_defaults() {
    tuning_mhz = 250;
    const uint32_t def_k[10] = {0, 9, 19, 11, 8, 0, 0, 0, 0, 0};
    const uint32_t def_t[10] = {0, 16, 0, 22, 3, 12, 0, 0, 0, 0};
    for (int i = 0; i < 10; i++) {
        tuning_k[i] = def_k[i];
        tuning_t[i] = def_t[i];
    }
}

// Convenience references for T[1-9], K[1-9], and MHZ
#ifdef MHZ
#undef MHZ
#endif
inline uint32_t& MHZ = tuning_mhz;
inline uint32_t& K1  = tuning_k[1];
inline uint32_t& K2  = tuning_k[2];
inline uint32_t& K3  = tuning_k[3];
inline uint32_t& K4  = tuning_k[4];
inline uint32_t& K5  = tuning_k[5];
inline uint32_t& K6  = tuning_k[6];
inline uint32_t& K7  = tuning_k[7];
inline uint32_t& K8  = tuning_k[8];
inline uint32_t& K9  = tuning_k[9];

inline uint32_t& T1  = tuning_t[1];
inline uint32_t& T2  = tuning_t[2];
inline uint32_t& T3  = tuning_t[3];
inline uint32_t& T4  = tuning_t[4];
inline uint32_t& T5  = tuning_t[5];
inline uint32_t& T6  = tuning_t[6];
inline uint32_t& T7  = tuning_t[7];
inline uint32_t& T8  = tuning_t[8];
inline uint32_t& T9  = tuning_t[9];

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
    if (T1 > 0) {
        hamster[T1];
    }
    hamster.in(A::PINS, 32);
    hamster.mov(A::OSR, A::ISR);      // Save captured pins to OSR
    if (T2 > 0) {
        hamster[T2];
    }
    hamster.push();                   // Push address/control packet to Core 1 (clears ISR)
    hamster.out(A::ZERO, 31);         // Discard bits 0-30 from OSR
    hamster.out(A::Y, 1);             // Shift remaining bit (R/W) into Y
    hamster.jmp(A::Y_DECR, READ);      // Jump to READ if Y is nonzero (R/W=1), else fall through to WRITE

    hamster.label(WRITE);

    if (T3 > 0) {
        hamster[T3];                  // Delay until write data is stable on bus
    }
    hamster.in(A::PINS, 32);          // Sample written data
    hamster.push();                   // Push written data packet to Core 1
    hamster.jmp(LOOP);                // Return to start of next cycle

    hamster.label(READ);

    hamster.mov(A::OSR, A::INV_ZERO); // Set all 1s in OSR
    hamster.out(A::PINDIRS, 8);       // Set D[0:7] to outputs
    hamster.pull(A::BLOCK);           // Wait for Core 1 to supply read data byte
    hamster.out(A::PINS, 8);          // Drive read data onto D[0:7]
    if (T4 > 0) {
        hamster[T4];                  // Wait for bus setup
    }
    hamster.in(A::PINS, 32);          // Late pin sampling
    hamster.push();                   // Push late pin sample
    if (T5 > 0) {
        hamster[T5];                  // Hold data through Phase 4
    }

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
    hamster << A::SideSetStarting(WRITE + (T3 + 2), Phase1);
    hamster << A::SideSetStarting(READ + 0, Phase3);
    hamster << A::SideSetStarting(READ + K4, Phase4);
    hamster << A::SideSetStarting(READ + (K4 + 1 + T5), Phase1);

    hamster.finish();
    return hamster;
}

#if PIO_ASSEMBLER_ON_DEVICE
inline void hamster_program_init(PIO pio, uint sm, uint offset, const PioAssembler& hamster) {
    constexpr uint CLOCK_DIVISOR = 1;

    // pico-examples/pio/hub75/hub75.pio shows order of inits.
    pio_sm_set_consecutive_pindirs(pio, sm, 0, 8, false/*in*/);
    for (uint i = 0; i < 8; i++) {
        pio_gpio_init(pio, i);
    }
    pio_sm_set_consecutive_pindirs(pio, sm, 0, 8, false/*in*/);
    pio_gpio_init(pio, 29);
    pio_gpio_init(pio, 30);
    pio_sm_set_consecutive_pindirs(pio, sm, 29, 2, true/*out*/);
    pio_gpio_init(pio, 31);
    gpio_pull_up(31);
    pio_sm_set_consecutive_pindirs(pio, sm, 31, 1, false/*in*/);

    pio_sm_config cf = hamster.program_get_default_config(offset);
    sm_config_set_jmp_pin(&cf, 31);  // R/W
    sm_config_set_in_pins(&cf, 0);   // D[0:7]
    sm_config_set_out_pins(&cf, 0, 8);   // D[0:7]
    sm_config_set_sideset(&cf, 2, false, false);  // E & Q
    sm_config_set_sideset_pins(&cf, 29);  // E & Q
    sm_config_set_clkdiv(&cf, CLOCK_DIVISOR);

    // IN: Shift from left, do autopush
    const bool IN_SHIFT_RIGHT = true;
    const bool AUTOPUSH = false;
    const uint PUSH_THRESHOLD = 32;
    sm_config_set_in_shift(&cf, IN_SHIFT_RIGHT, AUTOPUSH, PUSH_THRESHOLD);

    // OUT: Shift to right, don't autopull
    const bool OUT_SHIFT_RIGHT = true;
    const bool AUTOPULL = false;
    const uint PULL_THRESHOLD = 8;
    sm_config_set_out_shift(&cf, OUT_SHIFT_RIGHT, AUTOPULL, PULL_THRESHOLD);

    pio_sm_init(pio, sm, offset, &cf);
    pio_sm_exec(pio, sm, offset);
    pio_sm_set_enabled(pio, sm, true);
}
#endif // PIO_ASSEMBLER_ON_DEVICE

#endif // RUNTIME_PIO_ASSEMBLER
