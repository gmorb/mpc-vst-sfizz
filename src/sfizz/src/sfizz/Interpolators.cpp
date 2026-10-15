// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#include "Interpolators.h"

namespace sfz {

void initializeInterpolators()
{
    SincInterpolatorTraits<8>::initialize();
    SincInterpolatorTraits<12>::initialize();
    SincInterpolatorTraits<16>::initialize();
    SincInterpolatorTraits<24>::initialize();
    SincInterpolatorTraits<36>::initialize();
    SincInterpolatorTraits<48>::initialize();
    SincInterpolatorTraits<60>::initialize();
    SincInterpolatorTraits<72>::initialize();
    sinc8PolyphaseTable();   // [sfizz for MPC OS] built here, never on the audio thread
}

const float* sinc8PolyphaseTable() noexcept
{
    struct Table {
        alignas(64) float rows[(kSinc8Phases + 2) * 8];
        Table()
        {
            SincInterpolatorTraits<8>::initialize();
            const float* t = SincInterpolatorTraits<8>::windowedSinc->getTablePointer();
            for (int k = 0; k <= kSinc8Phases + 1; ++k)
                for (int tap = 0; tap < 8; ++tap) {
                    const int index = (tap + 1) * kSinc8Phases - k;   // upstream: (tap + 1 - coeff) * 8191
                    rows[k * 8 + tap] = index >= 0 ? t[index] : 0.0f;
                }
        }
    };
    static const Table table;
    return table.rows;
}

} // namespace sfz
