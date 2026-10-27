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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_pairing_claim.cpp
 * @brief Tests that a PairingClaim keeps, gives up or leaves a device's clock pairing.
 * @see tests/platform/observability/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/observability/pairing_claim.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>

using SushiRuntime::DeviceClock;
using SushiRuntime::Internal::Observability::PairingClaim;
using SushiRuntime::Internal::Observability::ProfilingLedger;
using SushiRuntime::Internal::Observability::steady_ns;

namespace
{
    /** @brief Devices a reserve() call prepares pairings for. */
    constexpr std::size_t K_DEVICE_COUNT = 2;

    /** @brief Event-clock reading a kept pairing carries. */
    constexpr std::uint64_t K_EVENT_NS = 900;
} // namespace

/** @brief Tests that a claim that submitted records a window, so the device's pairing is kept once its event is read. */
TEST(Small_PairingClaim, ASubmittedClaimKeepsThePairing)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, K_DEVICE_COUNT);
    int task = 0;

    PairingClaim claim(&ledger, 0, &task);

    EXPECT_FALSE(claim.settle(true));
    EXPECT_TRUE(ledger.is_pairing_owner(0, &task));
    ledger.set_pairing_event_ns(0, K_EVENT_NS);
    std::vector<DeviceClock> clocks;
    ledger.collect_pairings(clocks);
    ASSERT_EQ(clocks.size(), 1u);
    EXPECT_EQ(clocks[0].pairing.event_ns, K_EVENT_NS);
    EXPECT_GT(clocks[0].pairing.steady_ns, 0u) << "the window starts at a steady reading";
}

/** @brief Tests that a claim that submitted nothing is given up, so the next dispatch claims the device. */
TEST(Small_PairingClaim, AClaimThatSubmittedNothingIsGivenUp)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, K_DEVICE_COUNT);
    int empty_body = 0;
    int next = 0;

    PairingClaim claim(&ledger, 0, &empty_body);

    EXPECT_TRUE(claim.settle(false));
    EXPECT_FALSE(ledger.is_pairing_owner(0, &empty_body));
    EXPECT_TRUE(ledger.claim_pairing_owner(0, &next));
}

/** @brief Tests that a claim with no ledger or on a held device leaves the ledger as it was. */
TEST(Small_PairingClaim, ALosingClaimChangesNothing)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, K_DEVICE_COUNT);
    int owner = 0;
    int other = 0;
    ASSERT_TRUE(ledger.claim_pairing_owner(0, &owner));

    PairingClaim held(&ledger, 0, &other);
    PairingClaim unset(nullptr, 1, &other);

    EXPECT_FALSE(held.settle(false));
    EXPECT_FALSE(unset.settle(false));
    EXPECT_TRUE(ledger.is_pairing_owner(0, &owner)) << "a losing claim never releases the owner's";
    EXPECT_TRUE(ledger.claim_pairing_owner(1, &other)) << "a claim with no ledger claimed nothing";
}

/** @brief Tests that steady_ns() counts the nanoseconds of a time point since the steady clock's epoch. */
TEST(Small_PairingClaim, SteadyNsCountsNanosecondsSinceTheEpoch)
{
    const std::chrono::steady_clock::time_point point{std::chrono::microseconds(3)};

    EXPECT_EQ(steady_ns(point), 3000u);
}
