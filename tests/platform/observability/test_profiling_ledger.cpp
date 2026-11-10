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
 * @file test_profiling_ledger.cpp
 * @brief Tests ProfilingLedger: its records, drop counts, device pairings and its behaviour after a move.
 * @see tests/platform/observability/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/observability/profiling_ledger.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Observability::DispatchFacts;
using SushiRuntime::Internal::Observability::DispatchTimes;
using SushiRuntime::Internal::Observability::PairingKey;
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

    /** @brief The node and invocation a collected record names. */
    using Identity = std::pair<std::uint32_t, std::uint32_t>;

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

    /**
     * @brief Records @p count dispatches of node @p node, invocation 0 up.
     * @return How many of them the ledger stored.
     */
    std::size_t record_many(ProfilingLedger& ledger, std::uint32_t node, std::uint32_t count)
    {
        std::size_t stored = 0;
        for (std::uint32_t invocation = 0; invocation < count; ++invocation)
        {
            if (ledger.record(facts_of(node, invocation), DispatchTimes{}))
                ++stored;
        }
        return stored;
    }

    /**
     * @brief Records one dispatch per attempt, then collects the run.
     * @param ledger The ledger the dispatches are recorded in.
     * @param attempts Dispatch count per node; each count stays within the node's row capacity.
     * @return The node and invocation of each record that collect() appended, in order.
     */
    std::vector<Identity> run_dispatches(ProfilingLedger& ledger, const std::vector<std::uint32_t>& attempts)
    {
        for (std::uint32_t node = 0; node < attempts.size(); ++node)
            static_cast<void>(record_many(ledger, node, attempts[node]));

        std::vector<Identity> identities;
        for (const NodeEventRecord& record : collected(ledger))
            identities.emplace_back(record.node_index, record.invocation);
        return identities;
    }

    /**
     * @brief Gives device 0 of @p ledger the pairing {K_EVENT_NS, K_STEADY_NS}.
     * @pre The ledger is reserved for at least one device.
     */
    void pair_device_zero(ProfilingLedger& ledger)
    {
        const int owner = 0;
        const PairingKey key{0, &owner, 0};
        EXPECT_TRUE(ledger.claim_pairing_owner(key));
        ledger.set_pairing_submit_window(key, K_STEADY_NS, 7);
        ledger.set_pairing_event_ns(key, K_EVENT_NS);
    }

    /** @brief Returns the pairings the ledger collects. */
    std::vector<DeviceClock> pairings_of(const ProfilingLedger& ledger)
    {
        std::vector<DeviceClock> clocks;
        ledger.collect_pairings(clocks);
        return clocks;
    }

    /** @brief The two ways a ledger is moved. */
    enum class MoveKind : std::uint8_t
    {
        Construct, /**< The target is move-constructed from the source. */
        Assign     /**< The target holds other rows and is move-assigned from the source. */
    };

    /** @brief Moves @p source into a new ledger the way @p kind names, and returns the target. */
    ProfilingLedger move_out_of(ProfilingLedger& source, MoveKind kind)
    {
        if (kind == MoveKind::Construct)
            return ProfilingLedger(std::move(source));
        ProfilingLedger target;
        target.reserve({5}, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);
        target = std::move(source);
        return target;
    }

    /** @brief One run that overflows a row: its shape, the dispatches made, and what the ledger must say. */
    struct OverflowRow
    {
        /** @brief The row name gtest prints. */
        const char* name;
        /** @brief Multiplicities handed to reserve(). */
        std::vector<std::uint32_t> multiplicities;
        /** @brief Expected invocations per node handed to reserve(). */
        std::uint32_t expected_invocations;
        /** @brief The node the dispatches name. */
        std::uint32_t node;
        /** @brief How many dispatches are made. */
        std::uint32_t attempts;
        /** @brief How many of them the row keeps. */
        std::size_t stored;
        /** @brief How many of them are counted as FullRow drops. */
        std::uint64_t full_row_drops;
    };
} // namespace

class Small_ProfilingLedgerMove : public ::testing::TestWithParam<MoveKind>
{
};

TEST_P(Small_ProfilingLedgerMove, TheMovedToLedgerKeepsThePairing)
{
    ProfilingLedger source;
    source.reserve(K_FIRST_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);
    pair_device_zero(source);

    const ProfilingLedger target = move_out_of(source, GetParam());

    const std::vector<DeviceClock> kept = pairings_of(target);
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(kept[0].device_index, 0u);
    EXPECT_EQ(kept[0].pairing.event_ns, K_EVENT_NS);
    EXPECT_EQ(kept[0].pairing.steady_ns, K_STEADY_NS);
}

TEST_P(Small_ProfilingLedgerMove, TheMovedToLedgerKeepsTheRows)
{
    ProfilingLedger source;
    source.reserve(K_FIRST_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);

    ProfilingLedger target = move_out_of(source, GetParam());

    EXPECT_EQ(target.claim_slot(0), 0u);
}

TEST_P(Small_ProfilingLedgerMove, TheMovedFromLedgerHoldsNoPairing)
{
    ProfilingLedger source;
    source.reserve(K_FIRST_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);
    pair_device_zero(source);

    const ProfilingLedger target = move_out_of(source, GetParam());

    EXPECT_TRUE(pairings_of(source).empty());
}

TEST_P(Small_ProfilingLedgerMove, TheMovedFromLedgerClaimsNothingBeforeReserve)
{
    ProfilingLedger source;
    source.reserve(K_FIRST_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);
    int owner = 0;

    const ProfilingLedger target = move_out_of(source, GetParam());

    EXPECT_EQ(source.claim_slot(0), ProfilingLedger::npos);
    EXPECT_FALSE(source.claim_pairing_owner(PairingKey{0, &owner, 0}));
}

TEST_P(Small_ProfilingLedgerMove, TheMovedFromLedgerReservesLikeAFreshOne)
{
    ProfilingLedger source;
    source.reserve(K_FIRST_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);
    const ProfilingLedger target = move_out_of(source, GetParam());
    ProfilingLedger fresh;
    fresh.reserve(K_SECOND_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);
    source.reserve(K_SECOND_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);

    EXPECT_EQ(run_dispatches(source, K_SECOND_ATTEMPTS), run_dispatches(fresh, K_SECOND_ATTEMPTS));
}

INSTANTIATE_TEST_SUITE_P(Moves, Small_ProfilingLedgerMove,
                         ::testing::Values(MoveKind::Construct, MoveKind::Assign),
                         [](const ::testing::TestParamInfo<MoveKind>& info)
                         { return info.param == MoveKind::Construct ? "Constructed" : "Assigned"; });

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

/** @brief Tests that a record made on the host device and with unknown widths keeps them. */
TEST(Small_ProfilingLedger, ARecordOnTheHostDeviceKeepsTheHostIndexAndUnknownWidths)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);
    DispatchFacts facts = facts_of(0, 0);
    facts.device_index = K_HOST_DEVICE_INDEX;
    facts.launched_width = K_WIDTH_UNKNOWN;
    facts.live_width = K_WIDTH_UNKNOWN;

    ASSERT_TRUE(ledger.record(facts, DispatchTimes{5, 5, 9}));

    const std::vector<NodeEventRecord> records = collected(ledger);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].device_index, K_HOST_DEVICE_INDEX);
    EXPECT_EQ(records[0].launched_width, K_WIDTH_UNKNOWN);
    EXPECT_EQ(records[0].live_width, K_WIDTH_UNKNOWN);
}

/** @brief Tests that a device keeps K_PAIRING_CANDIDATES candidates per run and refuses the next claim. */
TEST(Small_ProfilingLedger, ADeviceKeepsAFewPairingCandidatesPerRun)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 3);
    std::array<int, ProfilingLedger::K_PAIRING_CANDIDATES + 1> tasks{};

    for (std::size_t i = 0; i < ProfilingLedger::K_PAIRING_CANDIDATES; ++i)
        EXPECT_TRUE(ledger.claim_pairing_owner(PairingKey{0, &tasks[i], 0})) << "candidate " << i;

    const PairingKey refused{0, &tasks[ProfilingLedger::K_PAIRING_CANDIDATES], 0};
    EXPECT_FALSE(ledger.claim_pairing_owner(refused));
    EXPECT_TRUE(ledger.is_pairing_owner(PairingKey{0, &tasks[0], 0}));
    EXPECT_FALSE(ledger.is_pairing_owner(refused));
}

/** @brief Tests that each device claims its own pairing independently. */
TEST(Small_ProfilingLedger, EachDeviceClaimsItsOwnPairing)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 3);
    int owner = 0;

    EXPECT_TRUE(ledger.claim_pairing_owner(PairingKey{0, &owner, 0}));
    EXPECT_TRUE(ledger.claim_pairing_owner(PairingKey{1, &owner, 0}));
    EXPECT_TRUE(ledger.claim_pairing_owner(PairingKey{2, &owner, 0}));
}

/** @brief Tests that a device past the ledger claims nothing and owns nothing. */
TEST(Small_ProfilingLedger, ADevicePastTheLedgerClaimsNothing)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 3);
    int owner = 0;

    EXPECT_FALSE(ledger.claim_pairing_owner(PairingKey{3, &owner, 0}));
    EXPECT_FALSE(ledger.is_pairing_owner(PairingKey{3, &owner, 0}));
}

/** @brief Tests that a released candidate is free again, so a full device claims once one is given back. */
TEST(Small_ProfilingLedger, AReleasedCandidateIsClaimedAgain)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);
    std::array<int, ProfilingLedger::K_PAIRING_CANDIDATES + 1> tasks{};
    for (std::size_t i = 0; i < ProfilingLedger::K_PAIRING_CANDIDATES; ++i)
        ASSERT_TRUE(ledger.claim_pairing_owner(PairingKey{0, &tasks[i], 0}));
    const PairingKey late{0, &tasks[ProfilingLedger::K_PAIRING_CANDIDATES], 0};
    ASSERT_FALSE(ledger.claim_pairing_owner(late));

    ledger.release_pairing_owner(PairingKey{0, &tasks[1], 0});

    EXPECT_FALSE(ledger.is_pairing_owner(PairingKey{0, &tasks[1], 0}));
    EXPECT_TRUE(ledger.claim_pairing_owner(late));
}

/** @brief Tests that one task claiming in two steps holds two candidates, and each step's event reaches its own. */
TEST(Small_ProfilingLedger, TheSameTaskInTwoStepsHoldsTwoCandidates)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);
    int task = 0;
    const PairingKey first{0, &task, 1};
    const PairingKey second{0, &task, 2};
    ASSERT_TRUE(ledger.claim_pairing_owner(first));
    ASSERT_TRUE(ledger.claim_pairing_owner(second));
    ledger.set_pairing_submit_window(first, 1000, 90);
    ledger.set_pairing_submit_window(second, 2000, 10);

    ledger.set_pairing_event_ns(second, 7000);
    ledger.set_pairing_event_ns(first, 6000);

    const std::vector<DeviceClock> clocks = pairings_of(ledger);
    ASSERT_EQ(clocks.size(), 1u);
    EXPECT_EQ(clocks[0].pairing.event_ns, 7000u) << "the event of step 2 is paired with the window of step 2";
    EXPECT_EQ(clocks[0].pairing.steady_ns, 2000u);
    EXPECT_EQ(clocks[0].pairing.uncertainty_ns, 10u);
}

/** @brief Tests that the first event reading of a candidate stays when a second dispatch of its key is reaped. */
TEST(Small_ProfilingLedger, TheFirstEventReadingOfACandidateStays)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);
    int task = 0;
    const PairingKey key{0, &task, 0};
    ASSERT_TRUE(ledger.claim_pairing_owner(key));
    ledger.set_pairing_submit_window(key, 1000, 20);

    ledger.set_pairing_event_ns(key, 500);
    ledger.set_pairing_event_ns(key, 9999);

    const std::vector<DeviceClock> clocks = pairings_of(ledger);
    ASSERT_EQ(clocks.size(), 1u);
    EXPECT_EQ(clocks[0].pairing.event_ns, 500u);
}

/** @brief Tests that a device whose owner never read its event has no pairing to report. */
TEST(Small_ProfilingLedger, ADeviceThatNeverReadItsEventHasNoPairing)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 3);
    int owner = 0;
    const PairingKey key{2, &owner, 0};
    ASSERT_TRUE(ledger.claim_pairing_owner(key));

    ledger.set_pairing_submit_window(key, 3000, 40);

    EXPECT_TRUE(pairings_of(ledger).empty());
}

/** @brief Tests that the pairings of the devices that read their events are reported with their values. */
TEST(Small_ProfilingLedger, TheStoredPairingsAreReportedPerDevice)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 3);
    int first_owner = 0;
    int second_owner = 0;
    const PairingKey first{0, &first_owner, 0};
    const PairingKey second{1, &second_owner, 0};
    ASSERT_TRUE(ledger.claim_pairing_owner(first));
    ASSERT_TRUE(ledger.claim_pairing_owner(second));
    ledger.set_pairing_submit_window(first, 1000, 20);
    ledger.set_pairing_event_ns(first, 500);
    ledger.set_pairing_submit_window(second, 2000, 30);
    ledger.set_pairing_event_ns(second, 9000);

    const std::vector<DeviceClock> clocks = pairings_of(ledger);

    ASSERT_EQ(clocks.size(), 2u);
    EXPECT_EQ(clocks[0].device_index, 0u);
    EXPECT_EQ(clocks[0].pairing.event_ns, 500u);
    EXPECT_EQ(clocks[0].pairing.steady_ns, 1000u);
    EXPECT_EQ(clocks[0].pairing.uncertainty_ns, 20u);
    EXPECT_EQ(clocks[1].device_index, 1u);
    EXPECT_EQ(clocks[1].pairing.event_ns, 9000u);
    EXPECT_EQ(clocks[1].pairing.steady_ns, 2000u);
}

/** @brief Tests that a cold first submit with a long window loses to a later, shorter one. */
TEST(Small_ProfilingLedger, TheTightestWindowIsTheDevicesPairing)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);
    int cold = 0;
    int warm = 0;
    int warmer = 0;
    const PairingKey keys[] = {{0, &cold, 0}, {0, &warm, 0}, {0, &warmer, 0}};
    constexpr std::uint64_t K_STEADY[] = {1000, 2000, 3000};
    constexpr std::uint64_t K_WINDOW[] = {192200, 35, 20};
    constexpr std::uint64_t K_EVENT[] = {100, 1100, 2100};
    for (std::size_t i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(ledger.claim_pairing_owner(keys[i]));
        ledger.set_pairing_submit_window(keys[i], K_STEADY[i], K_WINDOW[i]);
        ledger.set_pairing_event_ns(keys[i], K_EVENT[i]);
    }

    const std::vector<DeviceClock> clocks = pairings_of(ledger);

    ASSERT_EQ(clocks.size(), 1u);
    EXPECT_EQ(clocks[0].pairing.uncertainty_ns, 20u);
    EXPECT_EQ(clocks[0].pairing.steady_ns, 3000u);
    EXPECT_EQ(clocks[0].pairing.event_ns, 2100u);
}

/** @brief Tests that a candidate with no event reading never wins, however short its window. */
TEST(Small_ProfilingLedger, ACandidateWithNoEventIsNotChosen)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);
    int read = 0;
    int unread = 0;
    const PairingKey with_event{0, &read, 0};
    const PairingKey without_event{0, &unread, 0};
    ASSERT_TRUE(ledger.claim_pairing_owner(with_event));
    ASSERT_TRUE(ledger.claim_pairing_owner(without_event));
    ledger.set_pairing_submit_window(with_event, 1000, 500);
    ledger.set_pairing_event_ns(with_event, 10);
    ledger.set_pairing_submit_window(without_event, 2000, 1);

    const std::vector<DeviceClock> clocks = pairings_of(ledger);

    ASSERT_EQ(clocks.size(), 1u);
    EXPECT_EQ(clocks[0].pairing.uncertainty_ns, 500u);
}

/** @brief Tests that the next reserve() clears the claims, so a run starts with no pairing. */
TEST(Small_ProfilingLedger, AReserveClearsThePairings)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);
    int owner = 0;
    const PairingKey key{0, &owner, 0};
    ASSERT_TRUE(ledger.claim_pairing_owner(key));
    ledger.set_pairing_event_ns(key, 1);

    ledger.reserve({1}, 1, 1);

    std::vector<DeviceClock> clocks;
    ledger.collect_pairings(clocks);
    EXPECT_TRUE(clocks.empty());
    EXPECT_TRUE(ledger.claim_pairing_owner(key)) << "the claim is free again";
}

class Small_ProfilingLedgerOverflow : public ::testing::TestWithParam<OverflowRow>
{
};

/** @brief Tests that dispatches past the capacity of a row are counted as FullRow and not stored, and nothing aborts. */
TEST_P(Small_ProfilingLedgerOverflow, ARowKeepsItsCapacityAndCountsTheRest)
{
    const OverflowRow& row = GetParam();
    ProfilingLedger ledger;
    ledger.reserve(row.multiplicities, row.expected_invocations, 1);

    EXPECT_EQ(record_many(ledger, row.node, row.attempts), row.stored);

    const DropCounts drops = ledger.drops();
    EXPECT_EQ(drops.of(DropCause::FullRow), row.full_row_drops);
    EXPECT_EQ(drops.first_node, row.node);
    EXPECT_TRUE(drops.any());
    EXPECT_EQ(collected(ledger).size(), row.stored);
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_ProfilingLedgerOverflow,
                         ::testing::Values(OverflowRow{"OneDispatchPastOneSlot", {1, 1}, 1, 1, 2, 1, 1},
                                           OverflowRow{"ThreeDispatchesPastTwoSlots", {1}, 2, 0, 5, 2, 3}),
                         [](const ::testing::TestParamInfo<OverflowRow>& info) { return std::string(info.param.name); });

/** @brief Tests that the run after an overflow sizes the row to the demand, so it loses nothing. */
TEST(Small_ProfilingLedger, TheNextReserveSizesARowToTheDemand)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 2, 1);
    static_cast<void>(record_many(ledger, 0, 5));
    static_cast<void>(collected(ledger));

    ledger.reserve({1}, 2, 1);

    EXPECT_FALSE(ledger.drops().any()) << "a reserve starts the counts again";
    EXPECT_EQ(record_many(ledger, 0, 5), 5u);
    EXPECT_EQ(collected(ledger).size(), 5u);
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
}

/** @brief Tests that the first drop names its node. */
TEST(Small_ProfilingLedger, TheFirstDropNamesItsNode)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);

    ledger.note_drop(DropCause::NoEvent, 0);
    ledger.note_drop(DropCause::NoQueue, DropCounts::K_NO_NODE);

    EXPECT_EQ(ledger.drops().first_node, 0u);
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
