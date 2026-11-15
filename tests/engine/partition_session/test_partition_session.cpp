/**************************************************************************/
/* test_partition_session.cpp                                             */
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

/**
 * @file test_partition_session.cpp
 * @brief Tests that PartitionSession keeps the last plan, refuses an unfitting job and reports what its runs measured.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <SushiRuntime/partition_session/partition_session.hpp>

#include "support/partition_problem_builder.hpp"
#include "rebalancing_parts.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Returns the parts of an active session over host_key(1) and host_key(2), of speeds 1 and 2. */
    PartitionSessionParts two_device_parts()
    {
        return session_parts({1.0, 2.0});
    }

    /** @brief Returns a session of two equal devices with two independent nodes partitioned one per device. */
    PartitionSession split_session()
    {
        PartitionSession session(session_parts({1.0, 1.0}));
        EXPECT_TRUE(session.partition(independent_nodes(2, 2)).has_value());
        return session;
    }

    /** @brief Returns a problem whose one node writes 2000 bytes, more than any pool of 900 usable bytes holds. */
    PartitionProblem unfitting_job()
    {
        PartitionProblem problem;
        add_devices(problem, 2);
        PartitionNode node;
        node.writes.push_back(add_buffer(problem, 2000));
        problem.nodes.push_back(node);
        return problem;
    }

    TEST(Small_PartitionSession, ASessionWithNoPartitionerIsInactiveAndReportsNothing)
    {
        PartitionSessionParts parts;
        parts.copies = std::make_shared<TransferPathTally>();
        const PartitionSession session(std::move(parts));

        EXPECT_FALSE(session.is_active());
        EXPECT_TRUE(session.report().rows.empty());
    }

    TEST(Small_PartitionSession, AnUnfittingJobIsRefusedWithTheShortfallOfEachPool)
    {
        PartitionSession session(two_device_parts());

        const Result<PartitionAssignment, PartitionRefusal>& answer = session.partition(unfitting_job());

        ASSERT_FALSE(answer.has_value());
        const Error error = answer.error().to_error();
        EXPECT_EQ(error.code, ErrorCode::AllocationFailed);
        EXPECT_NE(error.message.find("pool 1 needs 2000 bytes of 900 usable, 1100 missing"), std::string::npos);
        const PartitionReport report = session.report();
        EXPECT_TRUE(report.refused);
        ASSERT_EQ(report.rows.size(), 2u);
        EXPECT_EQ(report.rows[0].shortfall_bytes, 1100u);
    }

    TEST(Small_PartitionSession, TheReportOfTheLastPlanCarriesWhatItsRunsMeasured)
    {
        PartitionSession session(two_device_parts());
        ASSERT_TRUE(session.partition(chain(3, 100, 2)).has_value());
        session.ledger().add_steps(2, 1.0);
        session.ledger().add_dispatches({DispatchObservation{host_key(2), NodeCost{}, 0.5}});

        const PartitionReport report = session.report();

        ASSERT_EQ(report.rows.size(), 2u);
        EXPECT_EQ(report.rows[0].nodes + report.rows[1].nodes, 3u);
        EXPECT_EQ(report.measured_step_seconds, 0.5);
        EXPECT_EQ(report.rows[1].measured_seconds, 0.25);
        EXPECT_GT(report.floor_step_seconds, 0.0);
    }

    TEST(Small_PartitionSession, ANewPlanStartsAnEmptyLedger)
    {
        PartitionSession session(two_device_parts());
        ASSERT_TRUE(session.partition(chain(3, 100, 2)).has_value());
        session.ledger().add_steps(2, 1.0);

        ASSERT_TRUE(session.partition(chain(2, 100, 2)).has_value());

        EXPECT_EQ(session.report().measured_step_seconds, 0.0);
    }

    TEST(Small_PartitionSession, AThrottledDeviceLosesItsNodeAtTheFirstHorizon)
    {
        PartitionSession session = split_session();
        ASSERT_NE(session.answer().value().node_devices[0], session.answer().value().node_devices[1]);

        // The model gives each node 0.5 s; host_key(2) takes 2.5 s, five times its model.
        measure_step(session, 2.5, {{host_key(1), 0.5}, {host_key(2), 2.5}});
        const RebalanceAction action = session.rebalance();

        EXPECT_EQ(action, RebalanceAction::Replan);
        EXPECT_EQ(session.answer().value().node_devices, (std::vector<DeviceKey>{host_key(1), host_key(1)}));
    }

    TEST(Small_PartitionSession, AHorizonFoldsTheMeasuredThroughputIntoTheProfilesInMemory)
    {
        PartitionSession session = split_session();

        measure_step(session, 2.5, {{host_key(1), 0.5}, {host_key(2), 2.5}});
        static_cast<void>(session.rebalance());

        // 1000 operations in 2.5 s with no bytes is 400 operations a second.
        EXPECT_EQ(session.profiles()->find(host_key(2))->base_throughput, 400.0);
    }

    TEST(Small_PartitionSession, ADoctoredProfileReturnsToTheFloorWithinTwoHorizons)
    {
        PartitionSession session = split_session();

        // The devices run at their model, but the step takes 3 s, twice the floor's modelled 1 s and more.
        measure_step(session, 3.0, {{host_key(1), 0.5}, {host_key(2), 0.5}});
        EXPECT_EQ(session.rebalance(), RebalanceAction::TryFloor);
        measure_step(session, 1.0, {{host_key(1), 0.5}, {host_key(1), 0.5}});
        EXPECT_EQ(session.rebalance(), RebalanceAction::KeepFloor);

        EXPECT_TRUE(session.answer().value().floor_taken);
    }

    TEST(Small_PartitionSession, AFloorThatMeasuresSlowerGivesThePlanBackAndIsNotTriedAgain)
    {
        PartitionSession session = split_session();
        const std::vector<DeviceKey> scheduled = session.answer().value().node_devices;

        measure_step(session, 3.0, {{host_key(1), 0.5}, {host_key(2), 0.5}});
        ASSERT_EQ(session.rebalance(), RebalanceAction::TryFloor);
        measure_step(session, 3.5, {{host_key(1), 0.5}, {host_key(1), 0.5}});
        ASSERT_EQ(session.rebalance(), RebalanceAction::LeaveFloor);
        measure_step(session, 3.0, {{host_key(1), 0.5}, {host_key(2), 0.5}});

        EXPECT_EQ(session.rebalance(), RebalanceAction::Keep);
        EXPECT_EQ(session.answer().value().node_devices, scheduled);
    }

    TEST(Small_PartitionSession, EveryPlanPutInForceIsJournalledAndRaisesTheGeneration)
    {
        PartitionSession session = split_session();
        measure_step(session, 2.5, {{host_key(1), 0.5}, {host_key(2), 2.5}});
        static_cast<void>(session.rebalance());

        ASSERT_EQ(session.journal().entries().size(), 2u);
        EXPECT_EQ(session.journal().entries()[0].reason, PlacementReason::First);
        EXPECT_EQ(session.journal().entries()[1].reason, PlacementReason::Replan);
        EXPECT_EQ(session.journal().entries()[1].measured_device_seconds.at(host_key(2)), 2.5);
        EXPECT_EQ(session.generation(), 2u);
    }

    TEST(Small_PartitionSession, AReplayingSessionPutsTheJournalledPlansInForceAtTheSameHorizons)
    {
        PartitionSession recorded = split_session();
        measure_step(recorded, 2.5, {{host_key(1), 0.5}, {host_key(2), 2.5}});
        ASSERT_EQ(recorded.rebalance(), RebalanceAction::Replan);

        PartitionSession replayed(session_parts({1.0, 1.0}));
        ASSERT_TRUE(replayed.replay(recorded.journal().entries()).has_value());
        ASSERT_TRUE(replayed.is_active());
        ASSERT_TRUE(replayed.partition(independent_nodes(2, 2)).has_value());
        // The replay ignores what the horizon measured: both devices ran at their model.
        measure_step(replayed, 1.0, {{host_key(1), 0.5}, {host_key(2), 0.5}});

        EXPECT_EQ(replayed.rebalance(), RebalanceAction::Replan);
        EXPECT_EQ(replayed.answer().value().node_devices, recorded.answer().value().node_devices);
        ASSERT_EQ(replayed.journal().entries().size(), 2u);
        for (std::size_t index = 0; index < 2; ++index)
        {
            EXPECT_EQ(replayed.journal().entries()[index].reason, recorded.journal().entries()[index].reason);
            EXPECT_EQ(replayed.journal().entries()[index].assignment_hash,
                      recorded.journal().entries()[index].assignment_hash);
        }
        measure_step(replayed, 2.5, {{host_key(1), 0.5}, {host_key(2), 2.5}});
        EXPECT_EQ(replayed.rebalance(), RebalanceAction::Keep);
    }

    TEST(Small_PartitionSession, AReplayPutsEachChangeInForceAtTheHorizonItWasMadeAt)
    {
        // The recorded run keeps its plan at horizon 1 and replans at horizon 2.
        PartitionSession recorded = split_session();
        measure_step(recorded, 1.0, {{host_key(1), 0.5}, {host_key(2), 0.5}});
        ASSERT_EQ(recorded.rebalance(), RebalanceAction::Keep);
        measure_step(recorded, 2.5, {{host_key(1), 0.5}, {host_key(2), 2.5}});
        ASSERT_EQ(recorded.rebalance(), RebalanceAction::Replan);
        ASSERT_EQ(recorded.journal().entries().size(), 2u);
        ASSERT_EQ(recorded.journal().entries()[1].horizon, std::optional<std::uint64_t>(2));

        PartitionSession replayed(session_parts({1.0, 1.0}));
        ASSERT_TRUE(replayed.replay(recorded.journal().entries()).has_value());
        ASSERT_TRUE(replayed.partition(independent_nodes(2, 2)).has_value());
        const std::vector<DeviceKey> first = replayed.answer().value().node_devices;

        // Horizon 1 keeps the plan, whatever it measured.
        measure_step(replayed, 2.5, {{host_key(1), 0.5}, {host_key(2), 2.5}});
        EXPECT_EQ(replayed.rebalance(), RebalanceAction::Keep);
        EXPECT_EQ(replayed.answer().value().node_devices, first);
        EXPECT_EQ(replayed.generation(), 1u);

        measure_step(replayed, 1.0, {{host_key(1), 0.5}, {host_key(2), 0.5}});
        EXPECT_EQ(replayed.rebalance(), RebalanceAction::Replan);
        EXPECT_EQ(replayed.answer().value().node_devices, recorded.answer().value().node_devices);
        ASSERT_EQ(replayed.journal().entries().size(), 2u);
        EXPECT_EQ(replayed.journal().entries()[1].horizon, recorded.journal().entries()[1].horizon);
    }

    TEST(Small_PartitionSession, AnEntryWithNoHorizonIsReplayedAtTheNextHorizon)
    {
        PartitionSession recorded = split_session();
        measure_step(recorded, 1.0, {{host_key(1), 0.5}, {host_key(2), 0.5}});
        ASSERT_EQ(recorded.rebalance(), RebalanceAction::Keep);
        measure_step(recorded, 2.5, {{host_key(1), 0.5}, {host_key(2), 2.5}});
        ASSERT_EQ(recorded.rebalance(), RebalanceAction::Replan);
        // A version 1 journal holds no horizon.
        std::vector<PlacementJournalEntry> entries = recorded.journal().entries();
        for (PlacementJournalEntry& entry : entries)
            entry.horizon.reset();

        PartitionSession replayed(session_parts({1.0, 1.0}));
        ASSERT_TRUE(replayed.replay(std::move(entries)).has_value());
        ASSERT_TRUE(replayed.partition(independent_nodes(2, 2)).has_value());
        measure_step(replayed, 1.0, {{host_key(1), 0.5}, {host_key(2), 0.5}});

        EXPECT_EQ(replayed.rebalance(), RebalanceAction::Replan);
        EXPECT_EQ(replayed.answer().value().node_devices, recorded.answer().value().node_devices);
    }

    TEST(Small_PartitionSession, AReplayOfAnotherProblemIsRefusedNamingTheEntry)
    {
        PartitionSession recorded = split_session();
        PartitionSession replayed(session_parts({1.0, 1.0}));
        ASSERT_TRUE(replayed.replay(recorded.journal().entries()).has_value());

        const Result<PartitionAssignment, PartitionRefusal>& answer = replayed.partition(independent_nodes(3, 2));

        ASSERT_FALSE(answer.has_value());
        EXPECT_EQ(answer.error().error.code, ErrorCode::InvalidArgument);
        EXPECT_NE(answer.error().error.message.find("entry 0"), std::string::npos) << answer.error().error.message;
        EXPECT_FALSE(replayed.has_plan());
    }

    TEST(Small_PartitionSession, AReplayAfterAPlanIsInForceIsRefused)
    {
        PartitionSession session = split_session();

        const Result<void> replay = session.replay(session.journal().entries());

        ASSERT_FALSE(replay.has_value());
        EXPECT_EQ(replay.error().code, ErrorCode::InvalidState);
    }
} // namespace
