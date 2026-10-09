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
 * @see include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/core/run_report.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Assembles a RunReport from a finished run's raw measurements.
             * @param report The report to overwrite; reset before it is filled.
             * @see include/SushiRuntime/api/detail/README.md
             */
            inline void fill_run_report(
                Core::RunReport&                   report,
                Execution::RuntimeContext&         ctx,
                const Execution::ExecutionMetrics& before,
                const Execution::ExecutionMetrics& after,
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
                Core::NumaNodeStatistics node;
                if (ctx.device_count() > 0 && ctx.device_is_gpu(0))
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
                if (const auto* offload = ctx.offload())
                {
                    const auto offload_statistics = offload->statistics();
                    auto& distributed = report.distributed_statistics;
                    distributed.tasks_offloaded_to_network =
                        static_cast<std::size_t>(offload_statistics.offloaded);
                    distributed.remote_nodes_involved = offload_statistics.alive_workers;
                }
            }

        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
