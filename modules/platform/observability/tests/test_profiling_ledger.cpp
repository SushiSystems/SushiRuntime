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
 * @see modules/platform/observability/tests/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>
#include <SushiRuntime/observability/profiling_ledger.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using SushiRuntime::NodeEventRecord;
using SushiRuntime::Internal::Observability::ProfilingLedger;
using SushiRuntime::RunReport;

namespace
{
    /** @brief Expected dispatch count per node, used to size every row of a reserve() call. */
    constexpr std::uint32_t K_EXPECTED_INVOCATIONS = 3;

    /** @brief Steady-clock reading the source ledger carries before it is moved. */
    constexpr std::uint64_t K_RUN_START_NS = 900;

    /** @brief Multiplicities of the first run, before the move. */
    const std::vector<std::uint32_t> K_FIRST_MULTIPLICITIES{2, 1};

    /** @brief Multiplicities of the run after the move; a different row count from the first. */
    const std::vector<std::uint32_t> K_SECOND_MULTIPLICITIES{1, 3, 2};

    /** @brief Dispatch attempts per node in the run after the move; each stays within its row. */
    const std::vector<std::uint32_t> K_SECOND_ATTEMPTS{2, 5, 4};

    /**
     * @brief Claims and writes one record per attempt, then collects the run into a report.
     * @param ledger The ledger the dispatches are recorded in.
     * @param attempts Dispatch count per node; each count stays within the node's row capacity.
     * @return The report that collect() filled.
     */
    RunReport run_dispatches(ProfilingLedger& ledger, const std::vector<std::uint32_t>& attempts)
    {
        for (std::uint32_t node = 0; node < attempts.size(); ++node)
        {
            for (std::uint32_t invocation = 0; invocation < attempts[node]; ++invocation)
            {
                const std::uint32_t slot = ledger.claim_slot(node);
                if (slot == ProfilingLedger::npos)
                    continue;

                NodeEventRecord record;
                record.node_index = node;
                record.invocation = invocation;
                ledger.write(node, slot, record);
            }
        }

        RunReport report;
        ledger.collect(report);
        return report;
    }

    /**
     * @brief Asserts that two collected runs hold the same records in the same order.
     * @param actual The report produced by the moved-from ledger.
     * @param expected The report produced by a default-constructed ledger.
     */
    void expect_same_run(const RunReport& actual, const RunReport& expected)
    {
        ASSERT_EQ(actual.node_events.size(), expected.node_events.size());
        for (std::size_t i = 0; i < expected.node_events.size(); ++i)
        {
            EXPECT_EQ(actual.node_events[i].node_index, expected.node_events[i].node_index);
            EXPECT_EQ(actual.node_events[i].invocation, expected.node_events[i].invocation);
        }
    }
} // namespace

TEST(Unit_ProfilingLedger, MovedFromLedgerClaimsNothingBeforeReserve)
{
    ProfilingLedger source;
    source.reserve(K_FIRST_MULTIPLICITIES, K_EXPECTED_INVOCATIONS);

    ProfilingLedger target(std::move(source));

    EXPECT_EQ(source.claim_slot(0), ProfilingLedger::npos);
    EXPECT_EQ(target.claim_slot(0), 0u);
}

TEST(Unit_ProfilingLedger, MoveConstructedSourceReservesLikeFresh)
{
    ProfilingLedger source;
    source.reserve(K_FIRST_MULTIPLICITIES, K_EXPECTED_INVOCATIONS);
    source.set_run_start(K_RUN_START_NS);

    ProfilingLedger moved(std::move(source));

    // The moved-to ledger keeps the clock reading; the moved-from one reports zero.
    EXPECT_EQ(moved.pairing().event_ns, K_RUN_START_NS);
    EXPECT_EQ(source.pairing().event_ns, 0u);

    ProfilingLedger fresh;
    fresh.reserve(K_SECOND_MULTIPLICITIES, K_EXPECTED_INVOCATIONS);
    source.reserve(K_SECOND_MULTIPLICITIES, K_EXPECTED_INVOCATIONS);

    expect_same_run(run_dispatches(source, K_SECOND_ATTEMPTS),
                    run_dispatches(fresh, K_SECOND_ATTEMPTS));
}

TEST(Unit_ProfilingLedger, MoveAssignedSourceReservesLikeFresh)
{
    ProfilingLedger source;
    source.reserve(K_FIRST_MULTIPLICITIES, K_EXPECTED_INVOCATIONS);
    source.set_run_start(K_RUN_START_NS);

    ProfilingLedger target;
    target.reserve({5}, K_EXPECTED_INVOCATIONS);
    target = std::move(source);

    EXPECT_EQ(target.pairing().event_ns, K_RUN_START_NS);
    EXPECT_EQ(source.pairing().event_ns, 0u);
    EXPECT_EQ(source.claim_slot(0), ProfilingLedger::npos);

    ProfilingLedger fresh;
    fresh.reserve(K_SECOND_MULTIPLICITIES, K_EXPECTED_INVOCATIONS);
    source.reserve(K_SECOND_MULTIPLICITIES, K_EXPECTED_INVOCATIONS);

    expect_same_run(run_dispatches(source, K_SECOND_ATTEMPTS),
                    run_dispatches(fresh, K_SECOND_ATTEMPTS));
}
