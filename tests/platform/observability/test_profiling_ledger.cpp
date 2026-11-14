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
using SushiRuntime::Internal::Observability::DispatchWindow;
using SushiRuntime::Internal::Observability::ProfilingLedger;

namespace
{
    /** @brief Expected dispatch count per node, used to size every row of a reserve() call. */
    constexpr std::uint32_t K_EXPECTED_INVOCATIONS = 3;

    /** @brief Devices a reserve() call prepares pairings for. */
    constexpr std::size_t K_DEVICE_COUNT = 2;

    /** @brief Event-clock reading the source ledger's pairing carries before it is moved: its dispatch's start. */
    constexpr std::uint64_t K_EVENT_NS = 900;

    /** @brief K_EVENT_NS on the steady clock: the midpoint of [5000 - 900, 5100 - 950] is 4125, and 900 + 4125. */
    constexpr std::uint64_t K_STEADY_NS = 5025;

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
        ledger.narrow_pairing(0, DispatchWindow{5000, K_EVENT_NS, 950, 5100});
    }

    /** @brief A fake device's event clock: ahead of the steady clock by a fixed amount, and drifting at a fixed rate. */
    struct FakeEventClock
    {
        std::uint64_t ahead_ns;           /**< How far it runs ahead of the steady clock at steady 0. */
        std::uint64_t drift_numerator;    /**< It gains drift_numerator ns ... */
        std::uint64_t drift_denominator;  /**< ... every drift_denominator steady ns. */

        /** @brief Returns this clock's reading at the steady instant @p steady_ns. */
        [[nodiscard]] std::uint64_t read(std::uint64_t steady_ns) const
        {
            return steady_ns + ahead_ns + steady_ns * drift_numerator / drift_denominator;
        }
    };

    /** @brief An event clock 5 ms ahead of the steady clock that keeps its pace. */
    constexpr FakeEventClock K_STEADY_DEVICE{5'000'000, 0, 1};

    /** @brief An event clock 5 ms ahead that gains 33.5 ppm, the drift an RTX 3080 Ti's CUDA events showed. */
    constexpr FakeEventClock K_DRIFTING_DEVICE{5'000'000, 67, 2'000'000};

    /** @brief One dispatch of the fake device on the steady clock: the truth the ledger never sees whole. */
    struct TrueDispatch
    {
        std::uint64_t submit_steady_ns; /**< When the host began to submit it. */
        std::uint64_t start_steady_ns;  /**< When it truly started. */
        std::uint64_t end_steady_ns;    /**< When it truly ended. */
        std::uint64_t reap_steady_ns;   /**< When the host saw it complete. */
    };

    /** @brief A cold first dispatch: its kernel starts 200 us after the submit began, and is reaped 40 us after its end. */
    constexpr TrueDispatch K_COLD{10'000, 210'000, 260'000, 300'000};

    /** @brief A warm dispatch whose start follows its submit by 30 ns. */
    constexpr TrueDispatch K_WARM{400'000, 400'030, 400'500, 400'520};

    /** @brief A warm dispatch reaped 50 ns after its end. */
    constexpr TrueDispatch K_REAPED_QUICKLY{500'000, 500'010, 501'000, 501'050};

    /** @brief Returns @p truth moved @p later_ns later on the steady clock. */
    TrueDispatch moved(const TrueDispatch& truth, std::uint64_t later_ns)
    {
        return TrueDispatch{truth.submit_steady_ns + later_ns, truth.start_steady_ns + later_ns,
                            truth.end_steady_ns + later_ns, truth.reap_steady_ns + later_ns};
    }

    /** @brief Returns what the host sees of @p truth: its start and end read on @p clock. */
    DispatchWindow seen_on_device(const TrueDispatch& truth, const FakeEventClock& clock = K_STEADY_DEVICE)
    {
        return DispatchWindow{truth.submit_steady_ns, clock.read(truth.start_steady_ns), clock.read(truth.end_steady_ns),
                              truth.reap_steady_ns};
    }

    /** @brief Returns @p event_ns on the steady clock through @p pairing, as ClockPairing documents it. */
    std::uint64_t converted(const ClockPairing& pairing, std::uint64_t event_ns)
    {
        return pairing.steady_ns + (event_ns - pairing.event_ns);
    }

    /** @brief Returns how far @p actual lies from @p expected, in either direction. */
    std::uint64_t distance(std::uint64_t actual, std::uint64_t expected)
    {
        return actual > expected ? actual - expected : expected - actual;
    }

    /**
     * @brief Expects the span of @p truth, read on @p clock and converted through @p pairing, inside its uncertainty.
     *
     * Each end lies within the uncertainty of its true steady time, and the span lies inside the host's
     * submit-to-reap window give or take the uncertainty.
     */
    void expect_converted_within_uncertainty(const ClockPairing& pairing, const TrueDispatch& truth,
                                             const FakeEventClock& clock = K_STEADY_DEVICE)
    {
        const DispatchWindow seen = seen_on_device(truth, clock);
        const std::uint64_t start = converted(pairing, seen.start_ns);
        const std::uint64_t end = converted(pairing, seen.end_ns);
        EXPECT_LE(distance(start, truth.start_steady_ns), pairing.uncertainty_ns);
        EXPECT_LE(distance(end, truth.end_steady_ns), pairing.uncertainty_ns);
        EXPECT_LE(truth.submit_steady_ns, start + pairing.uncertainty_ns);
        EXPECT_LE(end, truth.reap_steady_ns + pairing.uncertainty_ns);
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

TEST_P(Small_ProfilingLedgerMove, TheMovedFromLedgerTakesNothingBeforeReserve)
{
    ProfilingLedger source;
    source.reserve(K_FIRST_MULTIPLICITIES, K_EXPECTED_INVOCATIONS, K_DEVICE_COUNT);

    const ProfilingLedger target = move_out_of(source, GetParam());
    pair_device_zero(source);

    EXPECT_EQ(source.claim_slot(0), ProfilingLedger::npos);
    EXPECT_TRUE(pairings_of(source).empty());
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

/** @brief Tests that a device no dispatch narrowed has no pairing, and one that was narrowed has its own. */
TEST(Small_ProfilingLedger, OnlyANarrowedDeviceHasAPairing)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 3);

    ledger.narrow_pairing(1, seen_on_device(K_WARM));

    const std::vector<DeviceClock> clocks = pairings_of(ledger);
    ASSERT_EQ(clocks.size(), 1u);
    EXPECT_EQ(clocks[0].device_index, 1u);
}

/** @brief Tests that a device past the ledger is ignored. */
TEST(Small_ProfilingLedger, ADevicePastTheLedgerIsIgnored)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 3);

    ledger.narrow_pairing(3, seen_on_device(K_WARM));

    EXPECT_TRUE(pairings_of(ledger).empty());
}

/** @brief Tests that each device keeps the bounds of its own dispatches. */
TEST(Small_ProfilingLedger, EachDeviceKeepsItsOwnBounds)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 2);

    ledger.narrow_pairing(0, DispatchWindow{1000, 400, 700, 1500});
    ledger.narrow_pairing(1, DispatchWindow{1000, 9400, 9700, 1500});

    // Device 0 is bounded by [600, 800] and device 1 by [-8400, -8200]; each midpoint places its start at 1100.
    const std::vector<DeviceClock> clocks = pairings_of(ledger);
    ASSERT_EQ(clocks.size(), 2u);
    EXPECT_EQ(clocks[0].device_index, 0u);
    EXPECT_EQ(clocks[0].pairing.event_ns, 400u);
    EXPECT_EQ(clocks[0].pairing.steady_ns, 1100u);
    // Half the width is 100, and 500 ns of span at 100 ppm adds 1 ns of drift, rounded up.
    EXPECT_EQ(clocks[0].pairing.uncertainty_ns, 101u);
    EXPECT_EQ(clocks[1].device_index, 1u);
    EXPECT_EQ(clocks[1].pairing.event_ns, 9400u);
    EXPECT_EQ(clocks[1].pairing.steady_ns, 1100u);
}

/** @brief Tests that the bounds of several dispatches give the tightest pairing, and it holds the true offset. */
TEST(Small_ProfilingLedger, SeveralDispatchesGiveTheTightestPairingAroundTheTrueOffset)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);

    ledger.narrow_pairing(0, seen_on_device(K_COLD));
    ledger.narrow_pairing(0, seen_on_device(K_WARM));
    ledger.narrow_pairing(0, seen_on_device(K_REAPED_QUICKLY));

    const std::vector<DeviceClock> clocks = pairings_of(ledger);
    ASSERT_EQ(clocks.size(), 1u);
    // The reference is the earliest start, K_COLD's.
    EXPECT_EQ(clocks[0].pairing.event_ns, 5'210'000u);
    // Low is K_REAPED_QUICKLY's -5'000'010 and high K_WARM's -4'999'980, so the midpoint is -4'999'995.
    EXPECT_EQ(clocks[0].pairing.steady_ns, 210'005u);
    // Half the width is 15; the span from 10'000 to 501'050 at 100 ppm adds 49.105 ns, rounded up to 50.
    EXPECT_EQ(clocks[0].pairing.uncertainty_ns, 65u);
    expect_converted_within_uncertainty(clocks[0].pairing, K_COLD);
    expect_converted_within_uncertainty(clocks[0].pairing, K_WARM);
    expect_converted_within_uncertainty(clocks[0].pairing, K_REAPED_QUICKLY);
}

/** @brief Tests that a cold first dispatch alone gives a wide pairing, and its span still lies inside it. */
TEST(Small_ProfilingLedger, AColdFirstDispatchAloneStaysInsideItsStatedUncertainty)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);

    ledger.narrow_pairing(0, seen_on_device(K_COLD));

    const std::vector<DeviceClock> clocks = pairings_of(ledger);
    ASSERT_EQ(clocks.size(), 1u);
    // [-5'200'000, -4'960'000] is 240'000 wide, and 290'000 ns of span at 100 ppm adds 29.
    EXPECT_EQ(clocks[0].pairing.uncertainty_ns, 120'029u);
    expect_converted_within_uncertainty(clocks[0].pairing, K_COLD);
}

/** @brief Tests that the pairing does not depend on the order the dispatches are reaped in. */
TEST(Small_ProfilingLedger, ThePairingDoesNotDependOnTheReapOrder)
{
    ProfilingLedger forward;
    forward.reserve({1}, 1, 1);
    ProfilingLedger backward;
    backward.reserve({1}, 1, 1);

    forward.narrow_pairing(0, seen_on_device(K_COLD));
    forward.narrow_pairing(0, seen_on_device(K_WARM));
    forward.narrow_pairing(0, seen_on_device(K_REAPED_QUICKLY));
    backward.narrow_pairing(0, seen_on_device(K_REAPED_QUICKLY));
    backward.narrow_pairing(0, seen_on_device(K_WARM));
    backward.narrow_pairing(0, seen_on_device(K_COLD));

    const std::vector<DeviceClock> first = pairings_of(forward);
    const std::vector<DeviceClock> second = pairings_of(backward);
    ASSERT_EQ(first.size(), 1u);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(first[0].pairing.event_ns, second[0].pairing.event_ns);
    EXPECT_EQ(first[0].pairing.steady_ns, second[0].pairing.steady_ns);
    EXPECT_EQ(first[0].pairing.uncertainty_ns, second[0].pairing.uncertainty_ns);
}

/** @brief Tests that the next reserve() widens the bounds again, so a run keeps none of the last run's. */
TEST(Small_ProfilingLedger, AReserveClearsThePairings)
{
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);
    ledger.narrow_pairing(0, seen_on_device(K_COLD));

    ledger.reserve({1}, 1, 1);
    ledger.narrow_pairing(0, seen_on_device(K_WARM));

    // Only K_WARM's own bounds are left: [-5'000'030, -4'999'980], half width 25, and 1 ns of drift over its 520 ns.
    const std::vector<DeviceClock> clocks = pairings_of(ledger);
    ASSERT_EQ(clocks.size(), 1u);
    EXPECT_EQ(clocks[0].pairing.event_ns, 5'400'030u);
    EXPECT_EQ(clocks[0].pairing.uncertainty_ns, 26u);
}

/** @brief Tests that a clock drifting 33.5 ppm across a run of one minute keeps every span inside the stated uncertainty. */
TEST(Small_ProfilingLedger, AClockDriftingAcrossALongRunStaysInsideTheStatedUncertainty)
{
    constexpr std::uint64_t K_HALF_MINUTE_NS = 30'000'000'000ULL;
    // The run starts a minute after the steady origin, so no converted time falls before it.
    const TrueDispatch first = moved(K_WARM, 2 * K_HALF_MINUTE_NS);
    const TrueDispatch middle = moved(K_WARM, 3 * K_HALF_MINUTE_NS);
    const TrueDispatch last = moved(K_REAPED_QUICKLY, 4 * K_HALF_MINUTE_NS);
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);

    ledger.narrow_pairing(0, seen_on_device(first, K_DRIFTING_DEVICE));
    ledger.narrow_pairing(0, seen_on_device(middle, K_DRIFTING_DEVICE));
    ledger.narrow_pairing(0, seen_on_device(last, K_DRIFTING_DEVICE));

    // The offset moves 2 ms over the minute, so the dispatches' own bounds cross; the drift allowance covers it.
    const std::vector<DeviceClock> clocks = pairings_of(ledger);
    ASSERT_EQ(clocks.size(), 1u);
    expect_converted_within_uncertainty(clocks[0].pairing, first, K_DRIFTING_DEVICE);
    expect_converted_within_uncertainty(clocks[0].pairing, middle, K_DRIFTING_DEVICE);
    expect_converted_within_uncertainty(clocks[0].pairing, last, K_DRIFTING_DEVICE);
}

/** @brief Runs a drifting device made a number of minutes after its first run. */
class Small_ProfilingLedgerDrift : public ::testing::TestWithParam<std::uint64_t>
{
};

/**
 * @brief Tests that a run made minutes after another on a clock drifting 33.5 ppm stays inside its own window.
 *
 * The first run leaves bounds that the offset has drifted out of by the second; the reserve between them drops them.
 */
TEST_P(Small_ProfilingLedgerDrift, ALaterRunOfADriftingClockStaysInsideItsWindow)
{
    constexpr std::uint64_t K_MINUTE_NS = 60'000'000'000ULL;
    const std::uint64_t later_ns = GetParam() * K_MINUTE_NS;
    ProfilingLedger ledger;
    ledger.reserve({1}, 1, 1);
    ledger.narrow_pairing(0, seen_on_device(K_COLD, K_DRIFTING_DEVICE));
    ledger.narrow_pairing(0, seen_on_device(K_WARM, K_DRIFTING_DEVICE));
    ledger.narrow_pairing(0, seen_on_device(K_REAPED_QUICKLY, K_DRIFTING_DEVICE));
    static_cast<void>(pairings_of(ledger));

    ledger.reserve({1}, 1, 1);
    ledger.narrow_pairing(0, seen_on_device(moved(K_COLD, later_ns), K_DRIFTING_DEVICE));
    ledger.narrow_pairing(0, seen_on_device(moved(K_WARM, later_ns), K_DRIFTING_DEVICE));
    ledger.narrow_pairing(0, seen_on_device(moved(K_REAPED_QUICKLY, later_ns), K_DRIFTING_DEVICE));

    const std::vector<DeviceClock> clocks = pairings_of(ledger);
    ASSERT_EQ(clocks.size(), 1u);
    expect_converted_within_uncertainty(clocks[0].pairing, moved(K_COLD, later_ns), K_DRIFTING_DEVICE);
    expect_converted_within_uncertainty(clocks[0].pairing, moved(K_WARM, later_ns), K_DRIFTING_DEVICE);
    expect_converted_within_uncertainty(clocks[0].pairing, moved(K_REAPED_QUICKLY, later_ns), K_DRIFTING_DEVICE);
}

INSTANTIATE_TEST_SUITE_P(Minutes, Small_ProfilingLedgerDrift, ::testing::Values(1u, 5u, 10u),
                         [](const ::testing::TestParamInfo<std::uint64_t>& info)
                         { return "After" + std::to_string(info.param) + "Minutes"; });

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
