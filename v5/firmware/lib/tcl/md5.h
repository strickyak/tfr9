/*
 * MD5 message-digest algorithm (RFC 1321).
 *
 * This implementation is based on the open source public domain MD5
 * implementation by Alexander Peslyak (Solar Designer), courtesy of Openwall:
 *   http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5
 *
 * License: Public Domain / Permissive Fallback (BSD/MIT equivalent)
 * ----------------------------------------------------------------------------
 * This software was written by Alexander Peslyak in 2001. No copyright is
 * claimed, and the software is hereby placed in the public domain.
 * In case this attempt to disclaim copyright and place the software in the
 * public domain is deemed not legally effective, this software is Copyright
 * (c) 2001 by Alexander Peslyak and permission is hereby granted to use,
 * copy, modify, and distribute this software with or without modifications
 * for any purpose.
 * ----------------------------------------------------------------------------
 */

#ifndef FIRMWARE_MD5_H_
#define FIRMWARE_MD5_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  uint32_t lo, hi;
  uint32_t a, b, c, d;
  uint8_t buffer[64];
  uint32_t block[16];
} MD5_CTX;

#define _MD5_F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define _MD5_G(x, y, z) ((y) ^ ((z) & ((x) ^ (y))))
#define _MD5_H(x, y, z) (((x) ^ (y)) ^ (z))
#define _MD5_H2(x, y, z) ((x) ^ ((y) ^ (z)))
#define _MD5_I(x, y, z) ((y) ^ ((x) | ~(z)))

#define _MD5_STEP(f, a, b, c, d, x, t, s)                       \
  (a) += f((b), (c), (d)) + (x) + (t);                          \
  (a) = (((a) << (s)) | (((a) & 0xffffffff) >> (32 - (s))));    \
  (a) += (b);

#define _MD5_SET(n)                                              \
  (ctx->block[(n)] =                                             \
       (uint32_t)ptr[(n) * 4] |                                  \
       ((uint32_t)ptr[(n) * 4 + 1] << 8) |                       \
       ((uint32_t)ptr[(n) * 4 + 2] << 16) |                      \
       ((uint32_t)ptr[(n) * 4 + 3] << 24))
#define _MD5_GET(n) (ctx->block[(n)])

static inline const void* _md5_body(MD5_CTX* ctx, const void* data, size_t size) {
  const uint8_t* ptr = (const uint8_t*)data;
  uint32_t a = ctx->a;
  uint32_t b = ctx->b;
  uint32_t c = ctx->c;
  uint32_t d = ctx->d;

  do {
    uint32_t saved_a = a;
    uint32_t saved_b = b;
    uint32_t saved_c = c;
    uint32_t saved_d = d;

    /* Round 1 */
    _MD5_STEP(_MD5_F, a, b, c, d, _MD5_SET(0), 0xd76aa478, 7)
    _MD5_STEP(_MD5_F, d, a, b, c, _MD5_SET(1), 0xe8c7b756, 12)
    _MD5_STEP(_MD5_F, c, d, a, b, _MD5_SET(2), 0x242070db, 17)
    _MD5_STEP(_MD5_F, b, c, d, a, _MD5_SET(3), 0xc1bdceee, 22)
    _MD5_STEP(_MD5_F, a, b, c, d, _MD5_SET(4), 0xf57c0faf, 7)
    _MD5_STEP(_MD5_F, d, a, b, c, _MD5_SET(5), 0x4787c62a, 12)
    _MD5_STEP(_MD5_F, c, d, a, b, _MD5_SET(6), 0xa8304613, 17)
    _MD5_STEP(_MD5_F, b, c, d, a, _MD5_SET(7), 0xfd469501, 22)
    _MD5_STEP(_MD5_F, a, b, c, d, _MD5_SET(8), 0x698098d8, 7)
    _MD5_STEP(_MD5_F, d, a, b, c, _MD5_SET(9), 0x8b44f7af, 12)
    _MD5_STEP(_MD5_F, c, d, a, b, _MD5_SET(10), 0xffff5bb1, 17)
    _MD5_STEP(_MD5_F, b, c, d, a, _MD5_SET(11), 0x895cd7be, 22)
    _MD5_STEP(_MD5_F, a, b, c, d, _MD5_SET(12), 0x6b901122, 7)
    _MD5_STEP(_MD5_F, d, a, b, c, _MD5_SET(13), 0xfd987193, 12)
    _MD5_STEP(_MD5_F, c, d, a, b, _MD5_SET(14), 0xa679438e, 17)
    _MD5_STEP(_MD5_F, b, c, d, a, _MD5_SET(15), 0x49b40821, 22)

    /* Round 2 */
    _MD5_STEP(_MD5_G, a, b, c, d, _MD5_GET(1), 0xf61e2562, 5)
    _MD5_STEP(_MD5_G, d, a, b, c, _MD5_GET(6), 0xc040b340, 9)
    _MD5_STEP(_MD5_G, c, d, a, b, _MD5_GET(11), 0x265e5a51, 14)
    _MD5_STEP(_MD5_G, b, c, d, a, _MD5_GET(0), 0xe9b6c7aa, 20)
    _MD5_STEP(_MD5_G, a, b, c, d, _MD5_GET(5), 0xd62f105d, 5)
    _MD5_STEP(_MD5_G, d, a, b, c, _MD5_GET(10), 0x02441453, 9)
    _MD5_STEP(_MD5_G, c, d, a, b, _MD5_GET(15), 0xd8a1e681, 14)
    _MD5_STEP(_MD5_G, b, c, d, a, _MD5_GET(4), 0xe7d3fbc8, 20)
    _MD5_STEP(_MD5_G, a, b, c, d, _MD5_GET(9), 0x21e1cde6, 5)
    _MD5_STEP(_MD5_G, d, a, b, c, _MD5_GET(14), 0xc33707d6, 9)
    _MD5_STEP(_MD5_G, c, d, a, b, _MD5_GET(3), 0xf4d50d87, 14)
    _MD5_STEP(_MD5_G, b, c, d, a, _MD5_GET(8), 0x455a14ed, 20)
    _MD5_STEP(_MD5_G, a, b, c, d, _MD5_GET(13), 0xa9e3e905, 5)
    _MD5_STEP(_MD5_G, d, a, b, c, _MD5_GET(2), 0xfcefa3f8, 9)
    _MD5_STEP(_MD5_G, c, d, a, b, _MD5_GET(7), 0x676f02d9, 14)
    _MD5_STEP(_MD5_G, b, c, d, a, _MD5_GET(12), 0x8d2a4c8a, 20)

    /* Round 3 */
    _MD5_STEP(_MD5_H, a, b, c, d, _MD5_GET(5), 0xfffa3942, 4)
    _MD5_STEP(_MD5_H2, d, a, b, c, _MD5_GET(8), 0x8771f681, 11)
    _MD5_STEP(_MD5_H, c, d, a, b, _MD5_GET(11), 0x6d9d6122, 16)
    _MD5_STEP(_MD5_H2, b, c, d, a, _MD5_GET(14), 0xfde5380c, 23)
    _MD5_STEP(_MD5_H, a, b, c, d, _MD5_GET(1), 0xa4beea44, 4)
    _MD5_STEP(_MD5_H2, d, a, b, c, _MD5_GET(4), 0x4bdecfa9, 11)
    _MD5_STEP(_MD5_H, c, d, a, b, _MD5_GET(7), 0xf6bb4b60, 16)
    _MD5_STEP(_MD5_H2, b, c, d, a, _MD5_GET(10), 0xbebfbc70, 23)
    _MD5_STEP(_MD5_H, a, b, c, d, _MD5_GET(13), 0x289b7ec6, 4)
    _MD5_STEP(_MD5_H2, d, a, b, c, _MD5_GET(0), 0xeaa127fa, 11)
    _MD5_STEP(_MD5_H, c, d, a, b, _MD5_GET(3), 0xd4ef3085, 16)
    _MD5_STEP(_MD5_H2, b, c, d, a, _MD5_GET(6), 0x04881d05, 23)
    _MD5_STEP(_MD5_H, a, b, c, d, _MD5_GET(9), 0xd9d4d039, 4)
    _MD5_STEP(_MD5_H2, d, a, b, c, _MD5_GET(12), 0xe6db99e5, 11)
    _MD5_STEP(_MD5_H, c, d, a, b, _MD5_GET(15), 0x1fa27cf8, 16)
    _MD5_STEP(_MD5_H2, b, c, d, a, _MD5_GET(2), 0xc4ac5665, 23)

    /* Round 4 */
    _MD5_STEP(_MD5_I, a, b, c, d, _MD5_GET(0), 0xf4292244, 6)
    _MD5_STEP(_MD5_I, d, a, b, c, _MD5_GET(7), 0x432aff97, 10)
    _MD5_STEP(_MD5_I, c, d, a, b, _MD5_GET(14), 0xab9423a7, 15)
    _MD5_STEP(_MD5_I, b, c, d, a, _MD5_GET(5), 0xfc93a039, 21)
    _MD5_STEP(_MD5_I, a, b, c, d, _MD5_GET(12), 0x655b59c3, 6)
    _MD5_STEP(_MD5_I, d, a, b, c, _MD5_GET(3), 0x8f0ccc92, 10)
    _MD5_STEP(_MD5_I, c, d, a, b, _MD5_GET(10), 0xffeff47d, 15)
    _MD5_STEP(_MD5_I, b, c, d, a, _MD5_GET(1), 0x85845dd1, 21)
    _MD5_STEP(_MD5_I, a, b, c, d, _MD5_GET(8), 0x6fa87e4f, 6)
    _MD5_STEP(_MD5_I, d, a, b, c, _MD5_GET(15), 0xfe2ce6e0, 10)
    _MD5_STEP(_MD5_I, c, d, a, b, _MD5_GET(6), 0xa3014314, 15)
    _MD5_STEP(_MD5_I, b, c, d, a, _MD5_GET(13), 0x4e0811a1, 21)
    _MD5_STEP(_MD5_I, a, b, c, d, _MD5_GET(4), 0xf7537e82, 6)
    _MD5_STEP(_MD5_I, d, a, b, c, _MD5_GET(11), 0xbd3af235, 10)
    _MD5_STEP(_MD5_I, c, d, a, b, _MD5_GET(2), 0x2ad7d2bb, 15)
    _MD5_STEP(_MD5_I, b, c, d, a, _MD5_GET(9), 0xeb86d391, 21)

    a += saved_a;
    b += saved_b;
    c += saved_c;
    d += saved_d;

    ptr += 64;
  } while (size -= 64);

  ctx->a = a;
  ctx->b = b;
  ctx->c = c;
  ctx->d = d;

  return ptr;
}

static inline void MD5_Init(MD5_CTX* ctx) {
  ctx->a = 0x67452301;
  ctx->b = 0xefcdab89;
  ctx->c = 0x98badcfe;
  ctx->d = 0x10325476;

  ctx->lo = 0;
  ctx->hi = 0;
}

static inline void MD5_Update(MD5_CTX* ctx, const void* data, size_t size) {
  uint32_t saved_lo = ctx->lo;
  if ((ctx->lo = (saved_lo + size) & 0x1fffffff) < saved_lo) ctx->hi++;
  ctx->hi += (uint32_t)(size >> 29);

  size_t used = saved_lo & 0x3f;

  if (used) {
    size_t available = 64 - used;

    if (size < available) {
      memcpy(&ctx->buffer[used], data, size);
      return;
    }

    memcpy(&ctx->buffer[used], data, available);
    data = (const uint8_t*)data + available;
    size -= available;
    _md5_body(ctx, ctx->buffer, 64);
  }

  if (size >= 64) {
    data = _md5_body(ctx, data, size & ~(size_t)0x3f);
    size &= 0x3f;
  }

  memcpy(ctx->buffer, data, size);
}

static inline void MD5_Final(unsigned char* result, MD5_CTX* ctx) {
  size_t used = ctx->lo & 0x3f;

  ctx->buffer[used++] = 0x80;

  size_t available = 64 - used;

  if (available < 8) {
    memset(&ctx->buffer[used], 0, available);
    _md5_body(ctx, ctx->buffer, 64);
    used = 0;
    available = 64;
  }

  memset(&ctx->buffer[used], 0, available - 8);

  ctx->lo <<= 3;
  ctx->buffer[56] = (uint8_t)(ctx->lo);
  ctx->buffer[57] = (uint8_t)(ctx->lo >> 8);
  ctx->buffer[58] = (uint8_t)(ctx->lo >> 16);
  ctx->buffer[59] = (uint8_t)(ctx->lo >> 24);
  ctx->buffer[60] = (uint8_t)(ctx->hi);
  ctx->buffer[61] = (uint8_t)(ctx->hi >> 8);
  ctx->buffer[62] = (uint8_t)(ctx->hi >> 16);
  ctx->buffer[63] = (uint8_t)(ctx->hi >> 24);

  _md5_body(ctx, ctx->buffer, 64);

  result[0] = (uint8_t)(ctx->a);
  result[1] = (uint8_t)(ctx->a >> 8);
  result[2] = (uint8_t)(ctx->a >> 16);
  result[3] = (uint8_t)(ctx->a >> 24);
  result[4] = (uint8_t)(ctx->b);
  result[5] = (uint8_t)(ctx->b >> 8);
  result[6] = (uint8_t)(ctx->b >> 16);
  result[7] = (uint8_t)(ctx->b >> 24);
  result[8] = (uint8_t)(ctx->c);
  result[9] = (uint8_t)(ctx->c >> 8);
  result[10] = (uint8_t)(ctx->c >> 16);
  result[11] = (uint8_t)(ctx->c >> 24);
  result[12] = (uint8_t)(ctx->d);
  result[13] = (uint8_t)(ctx->d >> 8);
  result[14] = (uint8_t)(ctx->d >> 16);
  result[15] = (uint8_t)(ctx->d >> 24);

  memset(ctx, 0, sizeof(*ctx));
}

#undef _MD5_F
#undef _MD5_G
#undef _MD5_H
#undef _MD5_H2
#undef _MD5_I
#undef _MD5_STEP
#undef _MD5_SET
#undef _MD5_GET

#endif  // FIRMWARE_MD5_H_
