// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#include "HelpersNEON.h"
#include "../SIMDConfig.h"
#include "Common.h"

#if SFIZZ_HAVE_NEON
#include <arm_neon.h>
#endif

// [sfizz for MPC OS] see HelpersNEON.h. Unaligned loads and stores are fine on ARMv7 NEON; each 4-lane step reads
// before it writes, so in-place calls are safe.
#if SFIZZ_HAVE_NEON
#define LANES(body) for (; i + 4 <= size; i += 4) { body; }
#else
#define LANES(body)
#endif

void gainNEON(const float* gain, const float* input, float* output, unsigned size) noexcept
{
    unsigned i = 0;
    LANES(vst1q_f32(output + i, vmulq_f32(vld1q_f32(gain + i), vld1q_f32(input + i))))
    for (; i < size; ++i) output[i] = gain[i] * input[i];
}

void gain1NEON(float gain, const float* input, float* output, unsigned size) noexcept
{
    unsigned i = 0;
#if SFIZZ_HAVE_NEON
    const float32x4_t g = vdupq_n_f32(gain);
#endif
    LANES(vst1q_f32(output + i, vmulq_f32(g, vld1q_f32(input + i))))
    for (; i < size; ++i) output[i] = gain * input[i];
}

void multiplyAddNEON(const float* gain, const float* input, float* output, unsigned size) noexcept
{
    unsigned i = 0;
    LANES(vst1q_f32(output + i, vaddq_f32(vld1q_f32(output + i), vmulq_f32(vld1q_f32(gain + i), vld1q_f32(input + i)))))
    for (; i < size; ++i) output[i] += gain[i] * input[i];
}

void multiplyAdd1NEON(float gain, const float* input, float* output, unsigned size) noexcept
{
    unsigned i = 0;
#if SFIZZ_HAVE_NEON
    const float32x4_t g = vdupq_n_f32(gain);
#endif
    LANES(vst1q_f32(output + i, vaddq_f32(vld1q_f32(output + i), vmulq_f32(g, vld1q_f32(input + i)))))
    for (; i < size; ++i) output[i] += gain * input[i];
}

void multiplyMulNEON(const float* gain, const float* input, float* output, unsigned size) noexcept
{
    unsigned i = 0;
    LANES(vst1q_f32(output + i, vmulq_f32(vld1q_f32(output + i), vmulq_f32(vld1q_f32(gain + i), vld1q_f32(input + i)))))
    for (; i < size; ++i) output[i] *= gain[i] * input[i];
}

void multiplyMul1NEON(float gain, const float* input, float* output, unsigned size) noexcept
{
    unsigned i = 0;
#if SFIZZ_HAVE_NEON
    const float32x4_t g = vdupq_n_f32(gain);
#endif
    LANES(vst1q_f32(output + i, vmulq_f32(vld1q_f32(output + i), vmulq_f32(g, vld1q_f32(input + i)))))
    for (; i < size; ++i) output[i] *= gain * input[i];
}

void addNEON(const float* input, float* output, unsigned size) noexcept
{
    unsigned i = 0;
    LANES(vst1q_f32(output + i, vaddq_f32(vld1q_f32(output + i), vld1q_f32(input + i))))
    for (; i < size; ++i) output[i] += input[i];
}

void add1NEON(float value, float* output, unsigned size) noexcept
{
    unsigned i = 0;
#if SFIZZ_HAVE_NEON
    const float32x4_t v = vdupq_n_f32(value);
#endif
    LANES(vst1q_f32(output + i, vaddq_f32(vld1q_f32(output + i), v)))
    for (; i < size; ++i) output[i] += value;
}

void subtractNEON(const float* input, float* output, unsigned size) noexcept
{
    unsigned i = 0;
    LANES(vst1q_f32(output + i, vsubq_f32(vld1q_f32(output + i), vld1q_f32(input + i))))
    for (; i < size; ++i) output[i] -= input[i];
}

void subtract1NEON(float value, float* output, unsigned size) noexcept
{
    unsigned i = 0;
#if SFIZZ_HAVE_NEON
    const float32x4_t v = vdupq_n_f32(value);
#endif
    LANES(vst1q_f32(output + i, vsubq_f32(vld1q_f32(output + i), v)))
    for (; i < size; ++i) output[i] -= value;
}
