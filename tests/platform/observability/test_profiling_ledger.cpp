/**************************************************************************/
/* test_profiling_ledger.cpp                                              */
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
 * @file test_profiling_ledger.cpp
 * @brief Tests that a moved-from ProfilingLedger behaves as a default-constructed one.
 * @see tests/platform/observability/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/observability/profiling_ledger.hpp>

using SushiRuntime::ClockPairing;
using SushiRuntime::DeviceClock;
using SushiRuntime::NodeEventRecord;
using SushiRuntime::Internal::Observability::DispatchFacts;
using SushiRuntime::Internal::Observability::DispatchTimes;
using SushiRuntime::Internal::Observability::ProfilingLedger;

namespace
{
    /** @brief Expected dispatch count per node, used to size every row of a reserve() call. */
    constexpr std::uint32_t K_EXPECTED_INVOCATIONS = 3;

    /** @brief Devices a reserve() call prepares pairings for. */
    constexpr std::size_t K_DEVICE_COUNT = 2;

    /** @brief Event-clock reading the source ledger's pairing carries before it is moved. */
    constexpr std::uint64_t K_EVENT_NS = 900;

    /** @brief Steady-clock reading taken beside K_EVENT_NS. */
    constexpr std::uint64_t K_STEADY_NS = 5000;

    /** @brief Multiplicities of the first run, before the move. */
    const std::vector<std::uint32_t> K_FIRST_MULTIPLICITIES{2, 1};

    /** @brief Multiplicities of the run after the move; a different row count from the first. */
    const std::vector<std::uint32_t> K_SECOND_MULTIPLICITIES{1, 3, 2};

    /** @brief Dispatch attempts per node in the run after the move; each stays within its row. */
    const std::vector<std::uint32_t> K_SECOND_ATTEMPTS{2, 5, 4};

    /**
     * @brief Records one dispatch per attempt, then collects the run.
     * @param ledger The ledger the dispatches are recorded in.
     * @param attempts Dispatch count per node; each count stays within the node's row capacity.
     * @return The records that collect() appended.
     */
    std::vector<NodeEventRecord> run_dispatches(ProfilingLedger& ledger,
                                                const std::vector<std::uint32_t>& attempts)
    {
        for (std::uint32_t node = 0; node < attempts.size(); ++node)
        {
            for (std::uint32_t invocation = 0; invocation < attempts[node]; ++invocation)
            {
                DispatchFacts facts;
                facts.node_index = node;
                facts.invocation = invocation;
                static_cast<void>(ledger.record(facts, DispatchTimes{}));
            }
        }

        std::vector<NodeEventRecord> events;
        ledger.collect(events);
        return events;
    }

    /**
     * @brief Asserts that two collected runs hold the same records in the same order.
     * @param actual The records produced by the moved-from ledger.
     * @param expected The records produced by a default-constructed ledger.
     */
    void expect_same_run(const std::vector<NodeEventRecord>& actual,
                         const std::vector<NodeEventRecord>& expected)
    {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(actual[i].node_index, expected[i].node_index);
            EXPECT_EQ(actual[i].invocation, expected[i].invocation);
        }
    }

    /**
     * @brief Gives device 0 of @p ledger the pairing {K_EVENT_NS, K_STEADY_NS}.
     * @pre The ledger is reserved for at least one device.
     */
    void pair_device_zero(ProfilingLedger& ledger)
    {
        const int owner = 0;
        ASSERT_TRUE(ledger.claim_pairing_owner(0, &owner));
        ledger.set_pairing_submit_window(0, K_STEADY_NS, 7);
        ledger.set_pairing_event_ns(0, K_EVENT_NS);
    }

    /** @brief Returns the pairings the ledger collects. */
    std::vector<DeviceClock> pairings_of(const ProfilingLedger& ledger)
    {
        std::vector<DeviceClock> clocks;
        ledger.collect_pairings(clocks);
        return clocks;
    }
} // namespace

TEST(Small_ProfilingLedger, MovedFromLedgerClaimsNothingBeforeReserve)
{
    ProfilingLedger source;
    source.reserve(K_FIRST_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);

    ProfilingLedger target(std::move(source));

    EXPECT_EQ(source.claim_slot(0), ProfilingLedger::npos);
    EXPECT_EQ(target.claim_slot(0), 0u);
}

TEST(Small_ProfilingLedger, MoveConstructedSourceReservesLikeFresh)
{
    ProfilingLedger source;
    source.reserve(K_FIRST_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);
    pair_device_zero(source);

    ProfilingLedger moved(std::move(source));

    // The moved-to ledger keeps the pairing; the moved-from one holds none.
    const std::vector<DeviceClock> kept = pairings_of(moved);
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(kept[0].device_index, 0u);
    EXPECT_EQ(kept[0].pairing.event_ns, K_EVENT_NS);
    EXPECT_EQ(kept[0].pairing.steady_ns, K_STEADY_NS);
    EXPECT_TRUE(pairings_of(source).empty());

    ProfilingLedger fresh;
    fresh.reserve(K_SECOND_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);
    source.reserve(K_SECOND_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);

    expect_same_run(run_dispatches(source, K_SECOND_ATTEMPTS),
                    run_dispatches(fresh, K_SECOND_ATTEMPTS));
}

TEST(Small_ProfilingLedger, MoveAssignedSourceReservesLikeFresh)
{
    ProfilingLedger source;
    source.reserve(K_FIRST_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);
    pair_device_zero(source);

    ProfilingLedger target;
    target.reserve({5}, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);
    target = std::move(source);

    const std::vector<DeviceClock> kept = pairings_of(target);
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(kept[0].pairing.event_ns, K_EVENT_NS);
    EXPECT_TRUE(pairings_of(source).empty());
    EXPECT_EQ(source.claim_slot(0), ProfilingLedger::npos);

    ProfilingLedger fresh;
    fresh.reserve(K_SECOND_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);
    source.reserve(K_SECOND_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);

    expect_same_run(run_dispatches(source, K_SECOND_ATTEMPTS),
                    run_dispatches(fresh, K_SECOND_ATTEMPTS));
}

using namespace SushiRuntime;
using SushiRuntime::Internal::Observability::DispatchFacts;
using SushiRuntime::Internal::Observability::DispatchTimes;
using SushiRuntime::Internal::Observability::ProfilingLedger;

namespace
{
    /** @brief Builds the facts of one dispatch of node @p node. */
    DispatchFacts facts_of(std::uint32_t node, std::uint32_t invocation)
    {
        DispatchFacts facts;
        facts.node_index = node;
        facts.invocation = invocation;
        return facts;
    }

    /** @brief Collects the records the ledger holds. */
    std::vector<NodeEventRecord> collected(ProfilingLedger& ledger)
    {
        std::vector<NodeEventRecord> records;
        ledger.collect(records);
        return records;
    }
} // namespace

/** @brief Tests that record() fills every field of the record it stores. */
TEST(Small_ProfilingLedger, RecordFillsEveryField)
{
    ProfilingLedger ledger;
    ledger.reserve({1, 1}, 2, 1);

    DispatchFacts facts;
    facts.node_index = 1;
    facts.invocation = 4;
    facts.launched_width = 64;
    facts.live_width = 48;
    facts.device_index = 0;
    facts.submit_thread = 7;
    ASSERT_TRUE(ledger.record(facts, DispatchTimes{100, 110, 150}));

    const std::vector<NodeEventRecord> records = collected(ledger);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].node_index, 1u);
    EXPECT_EQ(records[0].invocation, 4u);
    EXPECT_EQ(records[0].launched_width, 64u);
    EXPECT_EQ(records[0].live_width, 48u);
    EXPECT_EQ(records[0].device_index, 0u);
    EXPECT_EQ(records[0].submit_thread, 7u);
    EXPECT_EQ(records[0].submit_ns, 100u);
    EXPECT_EQ(records[0].start_ns, 110u);
    EXPECT_EQ(records[0].end_ns, 150u);
}

/** @brief Tests that a host record keeps the host device and unknown widths by default. */
TEST(Small_ProfilingLedger, ARecordWithNoDeviceNamesTheHost)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);

    ASSERT_TRUE(ledger.record(facts_of(0, 0), DispatchTimes{5, 5, 9}));

    const std::vector<NodeEventRecord> records = collected(ledger);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].device_index, K_HOST_DEVICE_INDEX);
    EXPECT_EQ(records[0].launched_width, K_WIDTH_UNKNOWN);
    EXPECT_EQ(records[0].live_width, K_WIDTH_UNKNOWN);
}

/** @brief Tests that each device claims its own pairing and only a device that read its event reports one. */
TEST(Small_ProfilingLedger, KeepsOnePairingPerDevice)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 3);
    int first_owner = 0;
    int second_owner = 0;
    int third_owner = 0;

    EXPECT_TRUE(ledger.claim_pairing_owner(0, &first_owner));
    EXPECT_FALSE(ledger.claim_pairing_owner(0, &second_owner)) << "a device has one owner per run";
    EXPECT_TRUE(ledger.claim_pairing_owner(1, &second_owner)) << "another device claims its own";
    EXPECT_TRUE(ledger.claim_pairing_owner(2, &third_owner));
    EXPECT_FALSE(ledger.claim_pairing_owner(3, &third_owner)) << "a device past the ledger claims nothing";
    EXPECT_TRUE(ledger.is_pairing_owner(0, &first_owner));
    EXPECT_FALSE(ledger.is_pairing_owner(1, &first_owner));
    EXPECT_FALSE(ledger.is_pairing_owner(3, &first_owner));

    ledger.set_pairing_submit_window(0, 1000, 20);
    ledger.set_pairing_event_ns(0, 500);
    ledger.set_pairing_submit_window(1, 2000, 30);
    ledger.set_pairing_event_ns(1, 9000);
    // Device 2 never read its owner's event, so it has no pairing to report.
    ledger.set_pairing_submit_window(2, 3000, 40);

    std::vector<DeviceClock> clocks;
    ledger.collect_pairings(clocks);

    ASSERT_EQ(clocks.size(), 2u);
    EXPECT_EQ(clocks[0].device_index, 0u);
    EXPECT_EQ(clocks[0].pairing.event_ns, 500u);
    EXPECT_EQ(clocks[0].pairing.steady_ns, 1000u);
    EXPECT_EQ(clocks[0].pairing.uncertainty_ns, 20u);
    EXPECT_EQ(clocks[1].device_index, 1u);
    EXPECT_EQ(clocks[1].pairing.event_ns, 9000u);
    EXPECT_EQ(clocks[1].pairing.steady_ns, 2000u);
}

/** @brief Tests that the next reserve() clears the claims, so a run starts with no pairing. */
TEST(Small_ProfilingLedger, AReserveClearsThePairings)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);
    int owner = 0;
    ASSERT_TRUE(ledger.claim_pairing_owner(0, &owner));
    ledger.set_pairing_event_ns(0, 1);

    ledger.reserve({1}, 1, 1);

    std::vector<DeviceClock> clocks;
    ledger.collect_pairings(clocks);
    EXPECT_TRUE(clocks.empty());
    EXPECT_TRUE(ledger.claim_pairing_owner(0, &owner)) << "the claim is free again";
}

/** @brief Tests that a dispatch that finds its row full is counted, not stored. */
TEST(Small_ProfilingLedger, CountsADispatchThatFoundItsRowFull)
{
    ProfilingLedger ledger;
    ledger.reserve({1, 1}, 1, 1);

    EXPECT_TRUE(ledger.record(facts_of(1, 0), DispatchTimes{1, 2, 3}));
    EXPECT_FALSE(ledger.record(facts_of(1, 1), DispatchTimes{4, 5, 6}));

    const DropCounts drops = ledger.drops();
    EXPECT_EQ(drops.of(DropCause::FullRow), 1u);
    EXPECT_EQ(drops.first_node, 1u);
    EXPECT_TRUE(drops.any());
    EXPECT_EQ(collected(ledger).size(), 1u) << "only the dispatch that found a slot is stored";
}

/** @brief Tests that a record for a node past the ledger is counted under its own cause. */
TEST(Small_ProfilingLedger, CountsARecordForANodePastTheRows)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);

    EXPECT_FALSE(ledger.record(facts_of(9, 0), DispatchTimes{}));

    EXPECT_EQ(ledger.drops().of(DropCause::UnknownNode), 1u);
    EXPECT_EQ(ledger.drops().of(DropCause::FullRow), 0u);
    EXPECT_EQ(ledger.drops().first_node, 9u);
}

/** @brief Tests that an overflow never aborts, so debug and release builds behave the same. */
TEST(Small_ProfilingLedger, OverflowBehavesTheSameInDebugAndRelease)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 2, 1);

    for (std::uint32_t invocation = 0; invocation < 5; ++invocation)
        static_cast<void>(ledger.record(facts_of(0, invocation), DispatchTimes{invocation, invocation, invocation}));

    // Collecting an overflowed row once asserted in debug builds and capped silently in release builds.
    const std::vector<NodeEventRecord> records = collected(ledger);
    EXPECT_EQ(records.size(), 2u) << "the row holds what its capacity was";
    EXPECT_EQ(ledger.drops().of(DropCause::FullRow), 3u);

    // The demand was five, so the next run sizes the row for it and loses nothing.
    ledger.reserve({1}, 2, 1);
    EXPECT_FALSE(ledger.drops().any()) << "a reserve starts the counts again";
    for (std::uint32_t invocation = 0; invocation < 5; ++invocation)
        EXPECT_TRUE(ledger.record(facts_of(0, invocation), DispatchTimes{})) << "invocation " << invocation;
    EXPECT_EQ(collected(ledger).size(), 5u);
}

/** @brief Tests that the other drop causes are counted by note_drop(), each under its own count. */
TEST(Small_ProfilingLedger, NoteDropCountsEachCauseApart)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);

    ledger.note_drop(DropCause::UnreadableEvent, 0);
    ledger.note_drop(DropCause::UnreadableEvent, 0);
    ledger.note_drop(DropCause::NoEvent, 0);
    ledger.note_drop(DropCause::NoQueue, DropCounts::K_NO_NODE);

    const DropCounts drops = ledger.drops();
    EXPECT_EQ(drops.of(DropCause::UnreadableEvent), 2u);
    EXPECT_EQ(drops.of(DropCause::NoEvent), 1u);
    EXPECT_EQ(drops.of(DropCause::NoQueue), 1u);
    EXPECT_EQ(drops.total(), 4u);
    EXPECT_EQ(drops.first_node, 0u) << "the first drop names its node";
}

/** @brief Tests that a ledger that recorded nothing reports no drop. */
TEST(Small_ProfilingLedger, ACleanRunLosesNothing)
{
    ProfilingLedger ledger;
    ledger.reserve({2}, 1, 1);

    EXPECT_TRUE(ledger.record(facts_of(0, 0), DispatchTimes{}));
    EXPECT_TRUE(ledger.record(facts_of(0, 0), DispatchTimes{}));

    EXPECT_FALSE(ledger.drops().any());
    EXPECT_EQ(ledger.drops().first_node, DropCounts::K_NO_NODE);
}

/** @brief Tests that a moved ledger keeps its pairings and a moved-from one starts with none. */
TEST(Small_ProfilingLedger, AMovedLedgerCarriesItsPairings)
{
    ProfilingLedger source;
    source.reserve({1}, 1, 2);
    int owner = 0;
    ASSERT_TRUE(source.claim_pairing_owner(1, &owner));
    source.set_pairing_event_ns(1, 77);

    ProfilingLedger target(std::move(source));

    std::vector<DeviceClock> clocks;
    target.collect_pairings(clocks);
    ASSERT_EQ(clocks.size(), 1u);
    EXPECT_EQ(clocks[0].device_index, 1u);
    EXPECT_FALSE(source.claim_pairing_owner(0, &owner)) << "a moved-from ledger claims nothing before a reserve";
}
