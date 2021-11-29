/*  Written in 2018 by David Blackman and Sebastiano Vigna (vigna@acm.org)
 *
 *  To the extent possible under law, the author has dedicated all copyright
 *  and related and neighboring rights to this software to the public domain
 *  worldwide. This software is distributed without any warranty.
 *
 *  See <http://creativecommons.org/publicdomain/zero/1.0/>. */

// Taken from https://prng.di.unimi.it/xoshiro256plus.c
// Added some glue code for C++ and integrated with the SGX SDK.

#pragma once

#include <array>
#include <algorithm>
#include <sgx_trts.h>

namespace eurostat {
namespace enclave {

/* This is xoshiro256+ 1.0, our best and fastest generator for floating-point
 numbers. We suggest to use its upper bits for floating-point
 generation, as it is slightly faster than xoshiro256**. It passes all
 tests we are aware of except for the lowest three bits, which might
 fail linearity tests (and just those), so if low linear complexity is
 not considered an issue (as it is usually the case) it can be used to
 generate 64-bit outputs, too.

 We suggest to use a sign test to extract a random Boolean value, and
 right shifts to extract subsets of bits.

 The state must be seeded so that it is not everywhere zero. If you have
 a 64-bit seed, we suggest to seed a splitmix64 generator and use its
 output to fill s. */

class Xoshiro256Plus
{
public:
    using result_type = uint64_t;

private:
    result_type state[4];

public:
    Xoshiro256Plus() noexcept
    {
        sgx_read_rand(reinterpret_cast<uint8_t *>(&state), sizeof(state));
    }

    result_type operator()() noexcept
    {
        auto rotl = [](uint64_t const x, int const k) -> result_type {
            return (x << k) | (x >> (64 - k));
        };

        uint64_t const result = state[0] + state[3];

        uint64_t const t = state[1] << 17;

        state[2] ^= state[0];
        state[3] ^= state[1];
        state[1] ^= state[2];
        state[0] ^= state[3];

        state[2] ^= t;

        state[3] = rotl(state[3], 45);

        return result;
    }
};

} // namespace enclave
} // namespace eurostat
