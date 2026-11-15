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
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <SushiRuntime/coherence/list_scheduling_partitioner.hpp>
#include <SushiRuntime/coherence/partition_session.hpp>

#include "partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Returns the parts of an active session over host_key(1) and host_key(2), of speeds 1 and 2. */
    PartitionSessionParts two_device_parts()
    {
        PartitionProblem devices;
        add_devices(devices, 2);
        PartitionSessionParts parts;
        parts.model = speed_model({1.0, 2.0});
        parts.partitioner = std::make_unique<ListSchedulingPartitioner>(parts.model);
        parts.devices = devices.devices;
        parts.copies = std::make_shared<TransferPathTally>();
        parts.device_of = [](MemorySpaceID space) -> std::optional<DeviceKey>
        {
            if (space == MemorySpaceID(2))
                return host_key(2);
            return std::nullopt;
        };
        return parts;
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
} // namespace
