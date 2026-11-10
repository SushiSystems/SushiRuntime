/**************************************************************************/
/* test_pairing_claim.cpp                                                 */
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
 * @file test_pairing_claim.cpp
 * @brief Tests that a PairingClaim keeps, gives up or leaves a device's clock pairing.
 * @see tests/platform/observability/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/observability/pairing_claim.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>

using SushiRuntime::DeviceClock;
using SushiRuntime::Internal::Observability::PairingClaim;
using SushiRuntime::Internal::Observability::PairingKey;
using SushiRuntime::Internal::Observability::ProfilingLedger;
using SushiRuntime::Internal::Observability::steady_ns;

namespace
{
    /** @brief Devices a reserve() call prepares pairings for. */
    constexpr std::size_t K_DEVICE_COUNT = 2;

    /** @brief Event-clock reading a kept pairing carries. */
    constexpr std::uint64_t K_EVENT_NS = 900;
} // namespace

/** @brief Tests that a claim that submitted keeps the device's pairing, so collect returns the event reading stored. */
TEST(Small_PairingClaim, ASubmittedClaimKeepsThePairing)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, K_DEVICE_COUNT);
    int task = 0;
    const PairingKey key{0, &task, 0};
    PairingClaim claim(&ledger, key);

    EXPECT_FALSE(claim.settle(true));

    EXPECT_TRUE(ledger.is_pairing_owner(key));
    ledger.set_pairing_event_ns(key, K_EVENT_NS);
    std::vector<DeviceClock> clocks;
    ledger.collect_pairings(clocks);
    ASSERT_EQ(clocks.size(), 1u);
    EXPECT_EQ(clocks[0].pairing.event_ns, K_EVENT_NS);
}

/** @brief Tests that the window of a claim starts at a steady reading. */
TEST(Small_PairingClaim, TheWindowOfAClaimStartsAtASteadyReading)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, K_DEVICE_COUNT);
    int task = 0;
    const PairingKey key{0, &task, 0};
    PairingClaim claim(&ledger, key);
    static_cast<void>(claim.settle(true));
    ledger.set_pairing_event_ns(key, K_EVENT_NS);

    std::vector<DeviceClock> clocks;
    ledger.collect_pairings(clocks);

    ASSERT_EQ(clocks.size(), 1u);
    EXPECT_GT(clocks[0].pairing.steady_ns, 0u);
}

/** @brief Tests that a claim that submitted nothing is given up, so the next dispatch claims the device. */
TEST(Small_PairingClaim, AClaimThatSubmittedNothingIsGivenUp)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, K_DEVICE_COUNT);
    int empty_body = 0;
    int next = 0;
    const PairingKey empty_key{0, &empty_body, 0};

    PairingClaim claim(&ledger, empty_key);

    EXPECT_TRUE(claim.settle(false));
    EXPECT_FALSE(ledger.is_pairing_owner(empty_key));
    EXPECT_TRUE(ledger.claim_pairing_owner(PairingKey{0, &next, 0}));
}

/** @brief Tests that a claim with no ledger, or on a device whose candidates are all held, leaves the ledger as it was. */
TEST(Small_PairingClaim, ALosingClaimChangesNothing)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, K_DEVICE_COUNT);
    std::array<int, ProfilingLedger::K_PAIRING_CANDIDATES> owners{};
    for (const int& owner : owners)
        ASSERT_TRUE(ledger.claim_pairing_owner(PairingKey{0, &owner, 0}));
    int other = 0;

    PairingClaim held(&ledger, PairingKey{0, &other, 0});
    PairingClaim unset(nullptr, PairingKey{1, &other, 0});

    EXPECT_FALSE(held.settle(false));
    EXPECT_FALSE(unset.settle(false));
    EXPECT_TRUE(ledger.is_pairing_owner(PairingKey{0, &owners[0], 0})) << "a losing claim never releases the owner's";
    EXPECT_TRUE(ledger.claim_pairing_owner(PairingKey{1, &other, 0})) << "a claim with no ledger claimed nothing";
}

/** @brief Tests that the first dispatches of a device each keep a candidate, so a cold first submit is not the only one. */
TEST(Small_PairingClaim, TheFirstDispatchesOfADeviceEachKeepACandidate)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, K_DEVICE_COUNT);
    std::array<int, 2> tasks{};
    PairingClaim first(&ledger, PairingKey{0, &tasks[0], 0});
    PairingClaim second(&ledger, PairingKey{0, &tasks[1], 0});

    EXPECT_FALSE(first.settle(true));
    EXPECT_FALSE(second.settle(true));

    EXPECT_TRUE(ledger.is_pairing_owner(PairingKey{0, &tasks[0], 0}));
    EXPECT_TRUE(ledger.is_pairing_owner(PairingKey{0, &tasks[1], 0}));
}

/** @brief Tests that steady_ns() counts the nanoseconds of a time point since the steady clock's epoch. */
TEST(Small_PairingClaim, SteadyNsCountsNanosecondsSinceTheEpoch)
{
    const std::chrono::steady_clock::time_point point{std::chrono::microseconds(3)};

    EXPECT_EQ(steady_ns(point), 3000u);
}
