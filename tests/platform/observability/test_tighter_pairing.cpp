/**************************************************************************/
/* test_tighter_pairing.cpp                                               */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_tighter_pairing.cpp
 * @brief Tests tighter_pairing(), the choice between two clock pairings of one device.
 * @see tests/platform/observability/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <initializer_list>

#include <gtest/gtest.h>

#include <SushiRuntime/observability/tighter_pairing.hpp>

using SushiRuntime::ClockPairing;
using SushiRuntime::Internal::Observability::tighter_pairing;

namespace
{
    /** @brief A cold first submit: a window of 192200 ns, the figure a GPU gave right after it idled. */
    constexpr ClockPairing K_COLD{100, 1000, 192200};

    /** @brief A warm submit of the same run. */
    constexpr ClockPairing K_WARM{1100, 2000, 35};

    /** @brief A warmer one, with a shorter window still. */
    constexpr ClockPairing K_WARMER{2100, 3000, 20};

    /** @brief Folds @p candidates, the first the one kept, the way the ledger folds a device's candidates. */
    ClockPairing fold(std::initializer_list<ClockPairing> candidates) noexcept
    {
        ClockPairing kept = *candidates.begin();
        for (const ClockPairing& candidate : candidates)
            kept = tighter_pairing(kept, candidate);
        return kept;
    }
} // namespace

/** @brief Tests that a warm pairing replaces a cold one with a longer window. */
TEST(Small_TighterPairing, AShorterWindowReplacesALongerOne)
{
    const ClockPairing chosen = tighter_pairing(K_COLD, K_WARM);

    EXPECT_EQ(chosen.uncertainty_ns, K_WARM.uncertainty_ns);
    EXPECT_EQ(chosen.event_ns, K_WARM.event_ns);
    EXPECT_EQ(chosen.steady_ns, K_WARM.steady_ns);
}

/** @brief Tests that a cold candidate arriving after a warm one does not replace it. */
TEST(Small_TighterPairing, ALongerWindowKeepsTheKeptPairing)
{
    const ClockPairing chosen = tighter_pairing(K_WARM, K_COLD);

    EXPECT_EQ(chosen.uncertainty_ns, K_WARM.uncertainty_ns);
    EXPECT_EQ(chosen.event_ns, K_WARM.event_ns);
}

/** @brief Tests that two equal windows keep the pairing already kept. */
TEST(Small_TighterPairing, EqualWindowsKeepTheKeptPairing)
{
    constexpr ClockPairing K_SAME_WINDOW{9999, 8888, K_WARM.uncertainty_ns};

    const ClockPairing chosen = tighter_pairing(K_WARM, K_SAME_WINDOW);

    EXPECT_EQ(chosen.event_ns, K_WARM.event_ns);
    EXPECT_EQ(chosen.steady_ns, K_WARM.steady_ns);
}

/** @brief Tests that a window of zero beats every other and the pairing travels whole, not field by field. */
TEST(Small_TighterPairing, APairingIsChosenWholeNotFieldByField)
{
    constexpr ClockPairing K_EXACT{7, 6, 0};

    const ClockPairing chosen = tighter_pairing(K_WARM, K_EXACT);

    EXPECT_EQ(chosen.event_ns, 7u);
    EXPECT_EQ(chosen.steady_ns, 6u);
    EXPECT_EQ(chosen.uncertainty_ns, 0u);
}

/** @brief Tests that the tightest of a run's candidates is chosen whatever order they come in. */
TEST(Small_TighterPairing, TheTightestOfSeveralIsChosenInAnyOrder)
{
    EXPECT_EQ(fold({K_COLD, K_WARM, K_WARMER}).uncertainty_ns, K_WARMER.uncertainty_ns);
    EXPECT_EQ(fold({K_WARMER, K_WARM, K_COLD}).uncertainty_ns, K_WARMER.uncertainty_ns);
    EXPECT_EQ(fold({K_WARM, K_COLD, K_WARMER}).uncertainty_ns, K_WARMER.uncertainty_ns);
    EXPECT_EQ(fold({K_WARM, K_COLD, K_WARMER}).event_ns, K_WARMER.event_ns);
}

/** @brief Tests that the chosen window is never longer than any candidate's, so a cold first submit does not inflate it. */
TEST(Small_TighterPairing, TheChosenWindowIsNoLongerThanAnyCandidates)
{
    const ClockPairing chosen = fold({K_COLD, K_WARM, K_WARMER});

    for (const ClockPairing& candidate : {K_COLD, K_WARM, K_WARMER})
        EXPECT_LE(chosen.uncertainty_ns, candidate.uncertainty_ns);
}
