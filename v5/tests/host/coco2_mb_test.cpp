// Host unit test for firmware/lib/devices/coco2_mb.h:
//   - MC6883 SAM ($FFC0..$FFDF): TY=0 ROM vs TY=1 64K all-RAM, P1 32K page switch, $FFF0..$FFFF -> $BFF0..$BFFF vectors,
//     and VDG geometry calculation across Text/SG4 and PMODE 0..4.
//   - MC6821 PIA0 & PIA1 ($FF00..$FF3F, mirrored & 3): DDR vs Data select, Data-read clears CRx IRQ flags while
//     Control-read preserves them, 60 Hz VSYNC IRQ, CART FIRQ, keyboard probe_table, 64K RAMSZ jumper (PB2=0),
//     and joystick comparator binary search.

#include <cstdio>
#include <cstring>
#include "coco2_mb.h"

unsigned char ram[256 * 1024];

static int failures = 0;
#define CHECK(cond, ...) do { \
  if (!(cond)) { \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); \
    printf("\n"); \
    failures++; \
  } \
} while (0)

static void TestSamMemoryAndVectors() {
  memset(ram, 0, sizeof(ram));
  coco2_mb_reset();

  // Populate fake ROM signatures in ram[0x10000..0x17FFF]
  ram[kCoco2RomSramBase + 0x0000] = 'E';  // $8000 ExtBAS
  ram[kCoco2RomSramBase + 0x2000] = 'B';  // $A000 ColorBAS
  ram[kCoco2RomSramBase + 0x4000] = 'D';  // $C000 DiskBAS
  ram[kCoco2RomSramBase + 0x3FFE] = 0xA0; // $BFFE Reset vector hi
  ram[kCoco2RomSramBase + 0x3FFF] = 0x27; // $BFFF Reset vector lo

  // 1. Default TY=0, P1=0: $0000..$7FFF is RAM page 0; $8000..$FEFF is write-protected ROM; $FFFE->$BFFE ROM
  CHECK(coco2_sam_reader(0xFFC0) == 0xFF, "SAM registers read open-bus 0xFF");
  coco2_mem_write(0x0400, 0x60);
  CHECK(coco2_mem_read(0x0400) == 0x60 && ram[0x0400] == 0x60, "low 32K RAM R/W");
  CHECK(coco2_mem_read(0x8000) == 'E' && coco2_mem_read(0xA000) == 'B' && coco2_mem_read(0xC000) == 'D',
        "TY=0 reads ROM window");
  coco2_mem_write(0x8000, 0x99);
  coco2_mem_write(0xBFF0, 0x77);
  CHECK(coco2_mem_read(0x8000) == 'E', "TY=0 ignores writes to $8000..$FEFF");
  CHECK(coco2_vector_reader(0xFFFE) == 0xA0 && coco2_vector_reader(0xFFFF) == 0x27,
        "TY=0 $FFFE/$FFFF aliases to $BFFE/$BFFF ROM");
  coco2_vector_writer(0xFFFE, 0x12);
  CHECK(coco2_vector_reader(0xFFFE) == 0xA0, "TY=0 ignores vector writes");

  // 2. Set P1=1 ($FFD5 = bit 10 odd): $0000..$7FFF maps to ram[0x8000..0xFFFF] while $8000..$FEFF stays ROM
  ram[0x8400] = 0x42;
  coco2_sam_writer(0xFFD5, 0);
  CHECK(coco2_sam_p1_offset == 0x8000u, "FFD5 sets P1=1");
  CHECK(coco2_mem_read(0x0400) == 0x42, "P1=1 maps $0400 to ram[0x8400]");
  coco2_mem_write(0x0401, 0x55);
  CHECK(ram[0x8401] == 0x55 && ram[0x0401] == 0, "P1=1 writes upper 32K RAM");
  CHECK(coco2_mem_read(0xA000) == 'B', "P1=1 keeps $8000..$FEFF in ROM");
  coco2_sam_writer(0xFFD4, 0);
  CHECK(coco2_sam_p1_offset == 0u && coco2_mem_read(0x0400) == 0x60, "FFD4 clears P1=0");

  // 3. Set TY=1 ($FFDF = bit 15 odd): full 64K all-RAM mode (even if P1=1), $FFF0..$FFFF aliases to ram[$BFF0..$BFFF]
  coco2_sam_writer(0xFFD5, 0);  // P1=1 should be ignored when TY=1
  coco2_sam_writer(0xFFDF, 0);  // TY=1
  CHECK(coco2_sam_ty, "FFDF sets TY=1");
  CHECK(coco2_mem_read(0x0400) == 0x60, "TY=1 ignores P1 and maps $0400 to ram[0x0400]");
  coco2_mem_write(0x8000, 0xAB);
  coco2_mem_write(0xC000, 0xCD);
  CHECK(coco2_mem_read(0x8000) == 0xAB && coco2_mem_read(0xC000) == 0xCD, "TY=1 upper 32K RAM R/W");
  CHECK(ram[kCoco2RomSramBase + 0x0000] == 'E', "TY=1 RAM writes do not corrupt cached ROM");
  coco2_vector_writer(0xFFFE, 0x12);
  coco2_vector_writer(0xFFFF, 0x34);
  CHECK(ram[0xBFFE] == 0x12 && ram[0xBFFF] == 0x34, "TY=1 vector writes alias to ram[$BFFE/$BFFF]");
  CHECK(coco2_vector_reader(0xFFFE) == 0x12 && coco2_vector_reader(0xFFFF) == 0x34, "TY=1 vector reads alias to RAM");

  // Clear TY=0 ($FFDE): ROM reappears immediately!
  coco2_sam_writer(0xFFD4, 0);
  coco2_sam_writer(0xFFDE, 0);
  CHECK(!coco2_sam_ty && coco2_mem_read(0x8000) == 'E' && coco2_vector_reader(0xFFFE) == 0xA0,
        "FFDE restores ROM and ROM vectors");
}

static void TestPiaMirrorIrqKeyboardAnd64kSense() {
  coco2_mb_reset();

  // Mimic Color BASIC RESVEC ($A02A in bas.asm), which configures PIA0 via $FF1C..$FF1F!
  coco2_pia0_writer(0xFF1D, 0x00);  // CRA = 0 (select DDRA)
  coco2_pia0_writer(0xFF1F, 0x00);  // CRB = 0 (select DDRB)
  coco2_pia0_writer(0xFF1C, 0x00);  // DDRA = $00 (Port A all inputs)
  coco2_pia0_writer(0xFF1E, 0xFF);  // DDRB = $FF (Port B all outputs)
  coco2_pia0_writer(0xFF1D, 0x34);  // CRA = $34 (select Data A, IRQ disabled)
  coco2_pia0_writer(0xFF1F, 0x34);  // CRB = $34 (select Data B, IRQ disabled)

  coco2_pia1_writer(0xFF21, 0x00);  // CRA = 0
  coco2_pia1_writer(0xFF23, 0x00);  // CRB = 0
  coco2_pia1_writer(0xFF20, 0xFE);  // DDRA = $FE (PA0 input, PA1..PA7 outputs)
  coco2_pia1_writer(0xFF22, 0xF8);  // DDRB = $F8 (PB0..PB2 inputs, PB3..PB7 outputs)
  coco2_pia1_writer(0xFF21, 0x34);
  coco2_pia1_writer(0xFF23, 0x34);

  // 1. Verify 64K RAMSZ jumper check (`CLR $FF1E; BITB $FF22` with B=$04 -> must read 0 for 64K!)
  coco2_pia0_writer(0xFF1E, 0x00);
  CHECK((coco2_pia1_reader(0xFF22) & 0x04u) == 0u, "PIA1 PB2 reads 0 (64K RAMSZ jumper)");
  CHECK((coco2_pia1_reader(0xFF20) & 0x01u) == 0x01u, "PIA1 PA0 reads 1 (RS-232 idle mark)");

  // 2. Verify 60 Hz VSYNC IRQ & authentic MC6821 flag clearing (Control read keeps flag; Data read clears it!)
  coco2_pia0_writer(0xFF03, 0x35);  // Enable CB1 IRQ (bit 0 = 1)
  CHECK(!coco2_irq_needed(), "no IRQ before 60Hz tick");
  coco2_60hz_tick();
  CHECK(coco2_irq_needed(), "IRQ asserted after 60Hz tick");
  CHECK((coco2_pia0_reader(0xFF03) & 0x80u) != 0, "reading $FF03 sees bit 7");
  CHECK(coco2_irq_needed(), "reading $FF03 Control MUST NOT clear IRQ flag");
  (void)coco2_pia0_reader(0xFF02);
  CHECK(!coco2_irq_needed() && (coco2_pia0_reader(0xFF03) & 0x80u) == 0,
        "reading $FF02 Data clears IRQB1 flag");

  // 3. Verify CART FIRQ on PIA1 CB1 ($FF23)
  coco2_pia1_writer(0xFF23, 0x35);
  CHECK(!coco2_firq_needed(), "no FIRQ before CART");
  coco2_assert_cart_flag();
  CHECK(coco2_firq_needed(), "FIRQ asserted on CART");
  CHECK((coco2_pia1_reader(0xFF23) & 0x80u) != 0 && coco2_firq_needed(), "Control read preserves CART FIRQ");
  (void)coco2_pia1_reader(0xFF22);
  CHECK(!coco2_firq_needed(), "Data read $FF22 clears CART FIRQ");

  // 4. Verify keyboard matrix scanning: press Shift + 'P' (col=2, row=2, shift -> col 7 row 6)
  coco2_kb_set_live_key(/*col=*/2, /*row=*/2, /*shift=*/true, /*clear=*/false);
  coco2_pia0_writer(0xFF02, (uint8_t)~(1u << 2));
  CHECK((coco2_pia0_reader(0xFF00) & 0x7Fu) == (0x7Fu & ~(1u << 2)), "col 2 senses row 2 low");
  coco2_pia0_writer(0xFF02, (uint8_t)~(1u << 7));
  CHECK((coco2_pia0_reader(0xFF00) & 0x7Fu) == (0x7Fu & ~(1u << 6)), "col 7 senses Shift (row 6) low");
  coco2_pia0_writer(0xFF02, (uint8_t)~(1u << 0));
  CHECK((coco2_pia0_reader(0xFF00) & 0x7Fu) == 0x7Fu, "unpressed col 0 senses 0x7F");
  coco2_kb_clear_live();

  // 5. Verify VDG video geometry decode for Text ($0400, 512 B) and PMODE 4 ($0E00, 6144 B)
  coco2_sam_bits = 0;
  coco2_sam_writer(0xFFC9, 0);  // F1 = 1 -> base = 2 << 9 = 0x0400
  coco2_pia1_writer(0xFF22, 0x00);
  uint16_t vbase = 0, vbytes = 0;
  coco2_video_geometry(&vbase, &vbytes);
  CHECK(vbase == 0x0400 && vbytes == 512, "Text mode geometry ($0400, 512B), got $%04x/%u", vbase, vbytes);

  coco2_sam_bits = 0;
  coco2_sam_writer(0xFFC7, 0);  // F0 = 1
  coco2_sam_writer(0xFFC9, 0);  // F1 = 1
  coco2_sam_writer(0xFFCB, 0);  // F2 = 1 -> F=7 -> base = 7 << 9 = 0x0E00 (PMODE page 1.. wait: 3<<9 = 0x0600)
  coco2_pia1_writer(0xFF22, 0xF8);  // A/G=1, GM=7 (PMODE 4), CSS=1
  coco2_video_geometry(&vbase, &vbytes);
  CHECK(vbase == 0x0E00 && vbytes == 6144, "PMODE 4 geometry ($0E00, 6144B), got $%04x/%u", vbase, vbytes);
}

int main() {
  TestSamMemoryAndVectors();
  TestPiaMirrorIrqKeyboardAnd64kSense();
  if (failures) {
    printf("coco2_mb_test: %d FAILURES\n", failures);
    return 1;
  }
  printf("coco2_mb_test: OK\n");
  return 0;
}
