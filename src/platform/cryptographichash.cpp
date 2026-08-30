#include "cryptographichash.h"

#include <cstring>

using namespace muse;

namespace sve {
namespace {
// MD4, RFC 1320. Straight from the spec; the only liberty is that the message
// is padded into one buffer instead of being fed block by block, which the
// sizes here (a picture out of an .mscz) permit.
uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }
uint32_t f(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
uint32_t g(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (x & z) | (y & z); }
uint32_t h(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }

void round1(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, int s) { a = rol(a + f(b, c, d) + x, s); }
void round2(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, int s) { a = rol(a + g(b, c, d) + x + 0x5A827999u, s); }
void round3(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, int s) { a = rol(a + h(b, c, d) + x + 0x6ED9EBA1u, s); }

ByteArray md4(const ByteArray& data)
{
    const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8;

    // 0x80, then zeros up to 56 bytes into the last block, then the bit
    // length little-endian.
    std::vector<uint8_t> msg(data.constData(), data.constData() + data.size());
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) {
        msg.push_back(0);
    }
    for (int i = 0; i < 8; ++i) {
        msg.push_back(static_cast<uint8_t>(bitLength >> (8 * i)));
    }

    uint32_t state[4] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u };

    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t x[16];
        for (int i = 0; i < 16; ++i) {
            const uint8_t* p = msg.data() + offset + i * 4;
            x[i] = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];

        for (int i = 0; i < 16; i += 4) {
            round1(a, b, c, d, x[i + 0], 3);
            round1(d, a, b, c, x[i + 1], 7);
            round1(c, d, a, b, x[i + 2], 11);
            round1(b, c, d, a, x[i + 3], 19);
        }
        for (int i = 0; i < 4; ++i) {
            round2(a, b, c, d, x[i + 0], 3);
            round2(d, a, b, c, x[i + 4], 5);
            round2(c, d, a, b, x[i + 8], 9);
            round2(b, c, d, a, x[i + 12], 13);
        }
        for (int i : { 0, 2, 1, 3 }) {
            round3(a, b, c, d, x[i + 0], 3);
            round3(d, a, b, c, x[i + 8], 9);
            round3(c, d, a, b, x[i + 4], 11);
            round3(b, c, d, a, x[i + 12], 15);
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
    }

    ByteArray digest(16);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            digest[i * 4 + j] = static_cast<uint8_t>(state[i] >> (8 * j));
        }
    }
    return digest;
}
}

ByteArray CryptographicHash::hash(const ByteArray& data, Algorithm alg) const
{
    switch (alg) {
    case Algorithm::Md4: return md4(data);
    }
    return ByteArray();
}
}
