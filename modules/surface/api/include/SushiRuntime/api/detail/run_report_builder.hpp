/**************************************************************************/
/* run_report_builder.hpp                                                 */
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
 * @file run_report_builder.hpp
 * @brief Defines fill_run_report, which assembles a RunReport from a run's measurements.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <SushiRuntime/observability/run_report.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Returns how many dispatches the offload coordinator has accepted so far.
         * @return Zero when the context has no coordinator.
         */
        [[nodiscard]] inline std::uint64_t offloaded_dispatches(
            const Internal::Execution::RuntimeContext& context) noexcept
        {
            const auto* offload = context.offload();
            return offload != nullptr ? offload->statistics().offloaded : 0;
        }

        /**
         * @brief Assembles a RunReport from a finished run's raw measurements.
         * @param report The report to overwrite; reset before it is filled.
         * @param offloaded_before What offloaded_dispatches() returned before the run.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        inline void fill_run_report(
            Internal::Observability::RunReport&                   report,
            Internal::Execution::RuntimeContext&         context,
            const Internal::Execution::ExecutionMetrics& before,
            const Internal::Execution::ExecutionMetrics& after,
            std::uint64_t                      offloaded_before,
            std::size_t                        total_tasks,
            double                             compile_ms,
            double                             total_ms)
        {
            report.reset();
            report.is_successful = true;
            report.total_tasks_executed = total_tasks;
            report.graph_analysis_duration_ms = compile_ms;
            report.total_duration_ms = total_ms;

            // The device class comes from the runtime's own inventory, which does not allocate.
            Internal::Observability::NUMANodeStatistics node;
            const Internal::Device::DeviceQueueSet& devices = context.devices();
            if (devices.count() > 0 && devices.is_gpu(0))
                node.gpu_tasks_executed = total_tasks;
            else
                node.cpu_tasks_executed = total_tasks;

            // Steal counts are the scheduler's after-minus-before counters over the whole run.
            const std::size_t stolen_intra = after.local_steals  - before.local_steals;
            const std::size_t stolen_inter = after.remote_steals - before.remote_steals;
            const std::size_t completed    = after.tasks_completed - before.tasks_completed;
            node.tasks_stolen_intra_numa = stolen_intra;
            node.tasks_stolen_inter_numa = stolen_inter;
            const std::size_t stolen_total = stolen_intra + stolen_inter;
            node.tasks_popped_local = (completed > stolen_total) ? (completed - stolen_total) : 0;
            report.numa_statistics.push_back(node);

            // report.reset() zeroed the offload figures; they stay zero with no coordinator.
            if (const auto* offload = context.offload())
            {
                const auto offload_statistics = offload->statistics();
                const std::uint64_t offloaded_now = offload_statistics.offloaded;
                auto& distributed = report.distributed_statistics;
                // A coordinator installed during the run starts below the earlier reading.
                distributed.tasks_offloaded_to_network = static_cast<std::size_t>(
                    offloaded_now >= offloaded_before ? offloaded_now - offloaded_before
                                                      : offloaded_now);
                distributed.remote_nodes_involved = offload_statistics.alive_workers;
            }
        }

    } // namespace Detail
} // namespace SushiRuntime
