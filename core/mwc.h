#pragma once
#include "core.h"
#include "simd.h"
#include "time.h"
#include "vector.h"

struct Random {
    v8ui state;

    Random(uint32 seed = readCycleCounter()) { for(size_t i: range(8)) seed = state[i] = 1566083941u * (seed ^ (seed >> 27)) + i; }
    generic T next();
};

typedef uint64 v4uq __attribute((ext_vector_type(4)));

template<> inline v8ui Random::next<v8ui>() {
    static const v8ui factors {4294963023, 0, 3947008974, 0, 4162943475, 0, 2654432763, 0};
    v4uq y = __builtin_ia32_pmuludq256(state, factors); // 32*32->64 bit unsigned multiply
    y += ((v4uq)state) >> 32u;                          // add old carry
    state = (v8ui)y;                                    // new x and carry
    y ^= y << 30;
    y ^= y >> 35;
    y ^= y << 13;
    return (v8ui)y;
}

template<> inline uint Random::next<uint>() { return next<v8ui>()[0]; }
template<> inline uint64 Random::next<uint64>() { return ((v4uq)next<v8ui>())[0]; }

template<> inline v8sf Random::next<v8sf>() {
    const v8ui one = 0x3F800000;       // Binary representation of 1.0f
    const v8ui randomBits = next<v8ui>();          // Get random bits
    const v8ui r1 = one - ((randomBits >> 8) & 1); // 1.0 if bit8 is 0, or 1.0-2^-24 = 0.99999994f if bit8 is 1
    const v8ui r2 = (randomBits >> 9) | one;       // bits 9 - 31 inserted as mantissa
    return (v8sf)r2 - (v8sf)r1;        // 0 <= x < 1
}
template<> inline float Random::next<float>() { return next<v8sf>()[0]; }
template<> inline vec2 Random::next<vec2>() { v8sf v = next<v8sf>(); return vec2(v[0], v[1]); }
