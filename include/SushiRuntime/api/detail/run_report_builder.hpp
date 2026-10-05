/****************************************************************/
/* run_report_builder.hpp                                       */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file run_report_builder.hpp
 * @brief Defines fill_run_report, which assembles a RunReport from a run's measurements.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/core/run_report.hpp>
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

#ifdef SR_DISTRIBUTED
                auto& d = report.distributed_statistics;
                d.tasks_offloaded_to_network = ctx.distributed_offloaded_count();
                d.remote_nodes_involved =
                    static_cast<std::size_t>(ctx.distributed_alive_workers());
#endif
            }

        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
