/**************************************************************************/
/* run_report_builder.hpp                                                 */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

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
             *
             * Maps scheduler counters and device placement onto the public RunReport
             * structure. Writes into a caller-owned report so a steady-state tick can
             * hand back the same object every run: @p report is reset first, which
             * keeps the vector storage the previous run allocated.
             *
             * @param report      The report to overwrite; reset before it is filled.
             * @param ctx         The execution context providing the device inventory and distributed telemetry.
             * @param before      The scheduler execution metrics snapshotted before the run started.
             * @param after       The scheduler execution metrics snapshotted after the run completed.
             * @param total_tasks The total number of tasks executed across all steps.
             * @param compile_ms  The time spent compiling the task graph in milliseconds.
             * @param total_ms    The total wall-clock execution time in milliseconds.
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

                // Attribute the tasks to the device class the default queue targets.
                // Per-task placement and the reserved steal/byte counters are filled
                // in as the engine exposes them.
                //
                // Asked of the runtime's own inventory rather than of SYCL. This runs
                // on every tick, including the steady-state loop that run(RunReport&)
                // promises will not allocate, and the previous form could not keep
                // that promise portably: it called queue::get_device(), which returns
                // a device by value, and the cost of that construction belongs to the
                // SYCL runtime. On oneAPI's libsycl (the Linux CI toolchain) it
                // allocated once per call, which is why the steady-state test measured
                // 65 allocations over 64 ticks there and 0 on Windows, where the
                // intel/llvm nightly does not allocate. The device class of a queue is
                // fixed for the life of the context, so the answer was never worth
                // re-deriving per tick on any platform.
                Core::NumaNodeStatistics node;
                if (ctx.device_count() > 0 && ctx.device_is_gpu(0))
                    node.gpu_tasks_executed = total_tasks;
                else
                    node.cpu_tasks_executed = total_tasks;

                // Work-stealing breakdown over the whole run, from the scheduler's
                // before/after counters.
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
