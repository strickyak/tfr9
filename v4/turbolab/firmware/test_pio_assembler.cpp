// test_pio_assembler.cpp — Host verification and test suite for PioAssembler
#include <iostream>
#include <iomanip>
#include <cassert>
#define RUNTIME_PIO_ASSEMBLER 1
#include "pio_assembler.h"
#include "hamster.h"

// Golden instructions from hamster.pio.h (29 instructions)
static const uint16_t golden_instructions[] = {
    0x001b, //  0: jmp    27              side 0
            //     .wrap_target
    0x80a0, //  1: pull   block           side 0
    0xa742, //  2: nop                    side 0 [7]
    0xb742, //  3: nop                    side 2 [7]
    0x5000, //  4: in     pins, 32        side 2
    0xb0e6, //  5: mov    osr, isr        side 2
    0x9820, //  6: push   block           side 3
    0x787f, //  7: out    null, 31        side 3
    0x7841, //  8: out    y, 1            side 3
    0x1891, //  9: jmp    y--, 17         side 3
    0xbf42, // 10: nop                    side 3 [7]
    0xba42, // 11: nop                    side 3 [2]
    0xaa42, // 12: nop                    side 1 [2]
    0xaf42, // 13: nop                    side 1 [7]
    0x4800, // 14: in     pins, 32        side 1
    0x8820, // 15: push   block           side 1
    0x0001, // 16: jmp    1               side 0
    0xb8eb, // 17: mov    osr, ~null      side 3
    0x7888, // 18: out    pindirs, 8      side 3
    0x98a0, // 19: pull   block           side 3
    0x7808, // 20: out    pins, 8         side 3
    0xba42, // 21: nop                    side 3 [2]
    0x5800, // 22: in     pins, 32        side 3
    0x8820, // 23: push   block           side 1
    0xaa42, // 24: nop                    side 1 [2]
    0xa842, // 25: nop                    side 1
    0xaf42, // 26: nop                    side 1 [7]
    0xa0e3, // 27: mov    osr, null       side 0
    0x6088, // 28: out    pindirs, 8      side 0
            //     .wrap
};

int main() {
    std::cout << ">>> Running Test 1: Assemble hamster program with PioAssembler <<<\n";
    PioAssembler hamster = build_hamster_program(/*verbose=*/true);

    const auto& gen = hamster.instructions();
    std::cout << "Generated instructions count: " << gen.size() << " (hardware limit: 32)\n";
    assert(gen.size() <= 32);
    assert(hamster.get_wrap_target() == 1);
    assert(hamster.get_wrap() == static_cast<int>(gen.size() - 1));

    std::cout << "\n>>> Running Test 2: Paranoid error checking tests <<<\n";

    // Subtest: Unresolved label
    {
        std::cout << "Testing detection of unresolved label...\n";
        PioAssembler p(2);
        p.set_verbose(false);
        auto l1 = p.forward_reference("UNRESOLVED");
        p.jmp(l1);
        bool ok = p.finish();
        assert(!ok);
        std::cout << "  Passed (caught unresolved label)\n";
    }

    // Subtest: Exceeding 32 instructions
    {
        std::cout << "Testing detection of > 32 instructions...\n";
        PioAssembler p(2);
        p.set_verbose(false);
        p[300]; // 300 cycles of delay will require ~38 NOPs
        bool ok = p.finish();
        assert(!ok);
        std::cout << "  Passed (caught instruction memory overflow)\n";
    }

    // Subtest: Negative delay
    {
        std::cout << "Testing detection of negative delay...\n";
        PioAssembler p(2);
        p.set_verbose(false);
        p[-5];
        bool ok = p.finish();
        assert(!ok);
        std::cout << "  Passed (caught negative delay)\n";
    }

    // Subtest: Duplicate label
    {
        std::cout << "Testing detection of duplicate label binding...\n";
        PioAssembler p(2);
        p.set_verbose(false);
        auto l = p.forward_reference("L");
        p.label(l);
        p.label(l);
        bool ok = p.finish();
        assert(!ok);
        std::cout << "  Passed (caught duplicate label)\n";
    }

    std::cout << "\n============================================\n";
    std::cout << " ALL PIO ASSEMBLER TESTS PASSED SUCCESSFULLY! \n";
    std::cout << "============================================\n";
    return 0;
}
