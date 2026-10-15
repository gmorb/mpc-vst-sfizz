// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz
#pragma once

#include <chrono>
#include <type_traits>

namespace sfz
{

using Duration = std::chrono::duration<double>;
using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

static TimePoint highResNow()
{
#ifdef EMSCRIPTEN
    return {};
#else
    return std::chrono::high_resolution_clock::now();
#endif
}

/**
 * @brief Creates an RAII logger which fills or adds to a duration on destruction
 *
 */
// [sfizz for MPC OS] the per-voice and per-callback timings only feed sfizz's statistics and log files, which the
// MPC never reads. Each cost two clock reads (about ten per voice per block), a measurable share of a voice on the
// MPC's CPU, so ScopedTiming is a no-op here. The figures it would fill stay 0.
struct ScopedTiming
{
    enum class Operation
    {
        addToDuration,
        replaceDuration
    };
    ScopedTiming() = delete;
    ScopedTiming(double&, Operation = Operation::replaceDuration) noexcept {}
};

}
