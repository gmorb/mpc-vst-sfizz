// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#pragma once

// [sfizz for MPC OS] NEON versions of the element-wise helpers. Upstream's NEON table was empty, so 32-bit ARM ran the
// scalar loops; the compiler's own vectorization of those bails out at run time for in-place calls (input == output),
// which is how sfizz calls most of them. Each element gets exactly the scalar version's operations (a multiply, an
// add or a subtract, in the same order, never fused), so the results are the same.
void gainNEON(const float* gain, const float* input, float* output, unsigned size) noexcept;
void gain1NEON(float gain, const float* input, float* output, unsigned size) noexcept;
void multiplyAddNEON(const float* gain, const float* input, float* output, unsigned size) noexcept;
void multiplyAdd1NEON(float gain, const float* input, float* output, unsigned size) noexcept;
void multiplyMulNEON(const float* gain, const float* input, float* output, unsigned size) noexcept;
void multiplyMul1NEON(float gain, const float* input, float* output, unsigned size) noexcept;
void addNEON(const float* input, float* output, unsigned size) noexcept;
void add1NEON(float value, float* output, unsigned size) noexcept;
void subtractNEON(const float* input, float* output, unsigned size) noexcept;
void subtract1NEON(float value, float* output, unsigned size) noexcept;
