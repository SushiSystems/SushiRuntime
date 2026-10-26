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
 * @brief Defines fill_run_report, which assembles a RunReport from a run's counts.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <SushiRuntime/report/run_report.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Assembles a RunReport from a finished run's task and steal counts.
         * @param report The report to overwrite; reset before it is filled.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        inline void fill_run_report(
            RunReport&                   report,
            Internal::Execution::RuntimeContext&         context,
            const Internal::Execution::ExecutionMetrics& before,
            const Internal::Execution::ExecutionMetrics& after,
            std::size_t                        total_tasks)
        {
            report.reset();
            report.is_successful = true;
            report.total_tasks_executed = total_tasks;

            // The device class comes from the runtime's own inventory, which does not allocate.
            NUMANodeStatistics node;
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

            report.distributed_statistics.tasks_offloaded_to_network =
                after.tasks_offloaded - before.tasks_offloaded;
        }

    } // namespace Detail
} // namespace SushiRuntime
