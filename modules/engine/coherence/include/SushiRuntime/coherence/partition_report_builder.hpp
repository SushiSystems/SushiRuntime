/**************************************************************************/
/* partition_report_builder.hpp                                           */
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
 * @file partition_report_builder.hpp
 * @brief Declares make_partition_report(), which turns a partitioner's answer into a PartitionReport.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/coherence/cost_model.hpp>
#include <SushiRuntime/coherence/partition_assignment.hpp>
#include <SushiRuntime/coherence/partition_measurement.hpp>
#include <SushiRuntime/coherence/partition_problem.hpp>
#include <SushiRuntime/coherence/partition_refusal.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/report/partition_report.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Returns the report of what a partitioner answered for problem, one row per device.
         *
         * A row counts the nodes put on the device, the bytes of the buffers homed on it, the
         * model's node times of those nodes, and what measured gives the device. The report holds
         * the floor's modelled step and devices. A refusal gives rows of no nodes with each pool's
         * shortfall. The result is a pure function of its arguments.
         */
        [[nodiscard]] PartitionReport make_partition_report(const PartitionProblem& problem,
                                                            const CostModel& model,
                                                            const Result<PartitionAssignment, PartitionRefusal>& answer,
                                                            const PartitionMeasurement& measured = PartitionMeasurement{});
    } // namespace Internal::Coherence
} // namespace SushiRuntime
