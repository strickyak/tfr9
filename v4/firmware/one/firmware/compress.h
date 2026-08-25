#ifndef _FIRMWARE_PIO_COMPRESS_H_
#define _FIRMWARE_PIO_COMPRESS_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct CycleCompressState {
  addr16 prev;
  addr16 zone16[16];
  addr16 page256[256];
};

CycleCompressState old_read_cs;
CycleCompressState new_read_cs;
CycleCompressState write_cs;

// pred(abus) returns true if the read at this address is an "old" (ROM/known)
// read.
typedef bool (*ReadIsOldPredicate)(addr16 abus);

// ============================================================
// BitWriter: packs bits MSB-first into an output byte buffer.
// ============================================================

typedef struct {
  uint8_t* buf;
  uint byte_pos;  // index of byte currently being filled
  int bit_pos;    // next bit slot within current byte (7=MSB .. 0=LSB)
} BitWriter;

inline void IN_RAM bw_init(BitWriter* bw, uint8_t* buf) {
  bw->buf = buf;
  bw->byte_pos = 0;
  bw->bit_pos = 7;
  bw->buf[0] = 0;
}

// Write the low-order n bits of 'value', MSB first.
inline void IN_RAM bw_write(BitWriter* bw, uint32_t value, int n) {
  for (int i = n - 1; i >= 0; i--) {
    if ((value >> i) & 1u) {
      bw->buf[bw->byte_pos] |= (uint8_t)(1u << bw->bit_pos);
    }
    bw->bit_pos--;
    if (bw->bit_pos < 0) {
      bw->bit_pos = 7;
      bw->byte_pos++;
      bw->buf[bw->byte_pos] = 0;
    }
  }
}

// Flush: write end-of-buffer padding and return total byte count.
// Padding sequences (all invalid for old-read+incr sentinel reasons):
//   0 free bits: nothing
//   2 free bits: 00
//   4 free bits: 0010
//   6 free bits: 001000
inline uint IN_RAM bw_flush(BitWriter* bw) {
  if (bw->bit_pos == 7) {
    // Exactly on a byte boundary; no partial byte in progress.
    return bw->byte_pos;
  }
  int free_bits = bw->bit_pos + 1;  // bits remaining in buf[byte_pos]
  switch (free_bits) {
    case 2:
      bw_write(bw, 0b000000, 2);
      break;  // 00
    case 4:
      bw_write(bw, 0b0010, 4);
      break;  // 0010
    case 6:
      bw_write(bw, 0b001000, 6);
      break;  // 001000
    // free_bits==1,3,5,7 should not occur (all units are 2-bit aligned),
    // but if they do we leave the remaining bits as zero.
    default:
      break;
  }
  // After padding, bit_pos wrapped and byte_pos incremented.
  return bw->byte_pos;
}

// ============================================================
// Sign extension helpers.
// ============================================================

// Sign-extend a 3-bit two's-complement value to int. Range: -4..+3.
inline int se3(int v) { return (v >= 4) ? (v - 8) : v; }

// Sign-extend a 6-bit two's-complement value to int. Range: -32..+31.
inline int se6(int v) { return (v >= 32) ? (v - 64) : v; }

// ============================================================
// Address encoding.
// ============================================================

// Encode the zone+page delta portion of an address (called when prev-delta
// does not fit in -1/0/+1).  Writes zone bits then the appropriate delta.
// Also updates cs->zone16[zone] and cs->page256[page] as needed.
inline void IN_RAM encode_zone(BitWriter* bw, CycleCompressState* cs,
                               addr16 abus) {
  int zone = abus >> 12;
  bw_write(bw, (uint32_t)zone, 4);

  int zone_delta = (int)abus - (int)cs->zone16[zone];
  if (zone_delta >= -4 && zone_delta <= 3) {
    // 1ddd  (4 bits: 1 prefix + 3-bit signed delta)
    bw_write(bw, 1u, 1);
    bw_write(bw, (uint32_t)(zone_delta & 7), 3);
  } else if (zone_delta >= -32 && zone_delta <= 31) {
    // 01dddddd  (8 bits: 01 prefix + 6-bit signed delta)
    bw_write(bw, 1u, 2);
    bw_write(bw, (uint32_t)(zone_delta & 63), 6);
  } else {
    // 00pppp + page encoding
    int pppp = (abus >> 8) & 0xF;
    bw_write(bw, 0u, 2);
    bw_write(bw, (uint32_t)pppp, 4);

    int page = abus >> 8;
    int page_delta = (int)abus - (int)cs->page256[page];
    if (page_delta >= -4 && page_delta <= 3) {
      // 1ddd
      bw_write(bw, 1u, 1);
      bw_write(bw, (uint32_t)(page_delta & 7), 3);
    } else if (page_delta >= -32 && page_delta <= 31) {
      // 01dddddd
      bw_write(bw, 1u, 2);
      bw_write(bw, (uint32_t)(page_delta & 63), 6);
    } else {
      // 00aaaaaaaa  (10 bits: 00 prefix + raw low 8 bits)
      bw_write(bw, 0u, 2);
      bw_write(bw, (uint32_t)(abus & 0xFF), 8);
    }
    cs->page256[page] = abus;
  }
  cs->zone16[zone] = abus;
}

// Encode address delta relative to cs->prev, then update cs->prev.
inline void IN_RAM encode_abus(BitWriter* bw, CycleCompressState* cs,
                               addr16 abus) {
  int delta = (int)abus - (int)cs->prev;
  if (delta == -1) {
    bw_write(bw, 0u, 2);  // 00 = decr
  } else if (delta == 0) {
    bw_write(bw, 1u, 2);  // 01 = same
  } else if (delta == 1) {
    bw_write(bw, 2u, 2);  // 10 = incr
  } else {
    bw_write(bw, 3u, 2);  // 11 = zone encoding follows
    encode_zone(bw, cs, abus);
  }
  cs->prev = abus;
}

// ResetCompressCycles resets all three cycle states to zero.
// Call this at the start of a new session; omit between blocks to
// preserve state for better inter-block compression.
void IN_FLASH ResetCompressCycles(void) {
  memset(&old_read_cs, 0, sizeof(old_read_cs));
  memset(&new_read_cs, 0, sizeof(new_read_cs));
  memset(&write_cs, 0, sizeof(write_cs));
}

// CompressCycles — main entry point.
// Compresses an array of 32-bit bus cycle words into output_buffer.
// Returns the number of bytes written.
// Does NOT reset state — call ResetCompressCycles() to start fresh.
//
// Each input chore is: [fifo_verb(8) | abus(16) | dbus(8)]
// pred(abus) returns true for "old" reads whose dbus is known by the receiver.
//
// Output format: 2-bit aligned, packed MSB-first. See #if 0 spec below.

uint IN_RAM CompressCycles(uint8_t* output_buffer, uint32_t* input,
                           uint input_len, ReadIsOldPredicate pred) {
  BitWriter bw;
  bw_init(&bw, output_buffer);

  for (uint i = 0; i < input_len; i++) {
    uint32_t chore = input[i];
    addr16 abus = (chore >> 8) & 0xFFFF;
    uint8_t dbus = chore & 0xFF;
    byte fifo_verb = 0xFF & (chore >> 24);

    bool is_write = (fifo_verb == FG2BG_WRITE);
    bool is_old_read = !is_write && pred(abus);

    // Special case: old read with abus == old_read_cs.prev + 1.
    // Encoded as just "11" (2 bits).  This case must be used here
    // because "00 10" (old-read + incr) is reserved as the end sentinel.
    if (is_old_read && abus == (addr16)((int)old_read_cs.prev + 1)) {
      bw_write(&bw, 3u, 2);  // 11
      old_read_cs.prev = abus;
      continue;
    }

    // Cycle-type bits: 00=old-read, 01=new-read, 10=write
    uint32_t type_bits = is_write ? 2u : (is_old_read ? 0u : 1u);
    bw_write(&bw, type_bits, 2);

    // Address
    CycleCompressState* cs = is_write      ? &write_cs
                             : is_old_read ? &old_read_cs
                                           : &new_read_cs;
    encode_abus(&bw, cs, abus);

    // dbus: only for new-read and write (old-read dbus is implicit)
    if (!is_old_read) {
      bw_write(&bw, (uint32_t)dbus, 8);
    }
  }

  return bw_flush(&bw);
}

#if 0  // protocol spec

The input to CompressCycles are 32-bit words.
Each word has 3 parts:   The highest byte comes from FG2BG_READ
or FG2BG_WRITE.  The lowest byte is called dbus.
The middle 16 bits are abus.

If (word >> 24) == FG2BG_WRITE, it is a write cycle.

If (word >> 24) == FG2BG_READ, you must call pred to find
out if it is an Old Read Cycle or a New Read Cycle.

The difference is that an Old Read has a well-known dbus value
that does not need to be encoded in the compression.

Also, the three cycle types have their own independent memory
old_read_cs, new_read_cs, and write_cs, which is key
to compression for those cycles.

The last address encoded is saved in the .prev field.
The last address used with the high four bits B4 is
saved in .zone16[B4].
The last address used with the high eight bits B8 is
saved in .page256[B8].

The compressor produces output data with granularity of 2 bits.
These bits pack into the output from high 2 bits down to low 2 bits.
Bits pack tightly, leaving no padding.

1.  The first 2 bits tell the cycle type:
*   00: read old
*   01: read new
*   10: write
*   11: special: old read "next" -- abus is old_read_cs.prev+1, no further bits.

2.  If the above 2 bits are not 11, they are followed by 2 more bits
that tell if the current abus is very close to the previous abus
of this cycle type:

*   00: decr i.e. .prev-1
*   01: same i.e. .prev
*   10: incr i.e. .prev+1
*   11: something else, 4 zone bits follow.

NOTE: "00 10" (old-read + incr) is NEVER emitted because that case is
always encoded as "11" above.  This makes "00 10" usable as an
end-of-buffer sentinel.

If the above 2 bits are 11, let the "zone" be the
top 4 bits of the address.  These 4 bits are output,
and then zone indexes into zone16, and one of the
following follow:

*   1ddd:      3-bit delta: .zone16[zone] + signed(ddd)
*   01dddddd:  6-bit delta: .zone16[zone] + signed(dddddd)
*   00pppp:    four low-order page bits "p"

If the above zone discriminator is 00pppp, let the "page" be the
top 8 bits of the address (zone<<4)|pppp.  Then one of:

*   1ddd:        3-bit delta: .page256[page] + signed(ddd)
*   01dddddd:    6-bit delta: .page256[page] + signed(dddddd)
*   00aaaaaaaa:  the low 8 bits of the abus

If the compression is complete, and the gap not populated
in the final output byte is this many bits, output the
following bit sequences, which are not any valid compression:

0 free bits: none
2 free bits: 00
4 free bits: 0010
6 free bits: 001000

#endif  // protocol spec

#endif  // _FIRMWARE_PIO_COMPRESS_H_
