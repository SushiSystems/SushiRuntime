/**************************************************************************/
/* task_submission.cpp                                                    */
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
 * @file task_submission.cpp
 * @brief Submits one task to a queue behind its event dependencies, with or without an event.
 * @author Mustafa Garip
 */

#include <SushiRuntime/task/task_submission.hpp>

#include <utility>
#include <vector>
#include <sycl/sycl.hpp>
#if __has_include(<sycl/ext/oneapi/experimental/enqueue_functions.hpp>)
#include <sycl/ext/oneapi/experimental/enqueue_functions.hpp>
#define SR_HAS_ENQUEUE_FUNCTIONS 1
#endif
#include <SushiRuntime/task/dispatch_events.hpp>
#include <SushiRuntime/task/small_event_vector.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        namespace
        {
            /**
             * @brief Copies @p dependencies into the vector execute_host() takes.
             * @param dependencies The events, or null for none.
             * @return The events in order.
             */
            std::vector<sycl::event> host_dependencies(const SmallEventVector* dependencies)
            {
                std::vector<sycl::event> copied;
                if (dependencies == nullptr)
                    return copied;
                copied.reserve(dependencies->size());
                for (const sycl::event& event : *dependencies)
                    copied.push_back(event);
                return copied;
            }

            /**
             * @brief Builds the command group that runs @p task after @p dependencies.
             *
             * One depends_on() per event builds no heap vector.
             * @param task The kernel task.
             * @param dependencies The events, or null for none.
             * @return The command group.
             */
            auto kernel_group(Scheduler::Task& task, const SmallEventVector* dependencies)
            {
                return [&task, dependencies](sycl::handler& h)
                {
                    if (dependencies != nullptr)
                        for (const sycl::event& event : *dependencies)
                            h.depends_on(event);
                    task.execute(h);
                };
            }
        } // namespace

        /**
         * @brief Submits @p task to @p queue after @p dependencies and returns its event.
         */
        sycl::event submit_task(sycl::queue& queue, Scheduler::Task& task,
                                const SmallEventVector* dependencies,
                                std::vector<sycl::event>* handed_over)
        {
            if (task.execution_mode() != Scheduler::Task::ExecutionMode::Host)
                return queue.submit(kernel_group(task, dependencies));

            const std::vector<sycl::event> copied = host_dependencies(dependencies);
            if (handed_over == nullptr)
                return task.execute_host(queue, copied);
            // A body that submits several kernels hands each event over (task/dispatch_events.hpp).
            DispatchEvents::DispatchEventScope handing(*handed_over);
            return task.execute_host(queue, copied);
        }

        /**
         * @brief Submits @p task to @p queue after @p dependencies and keeps no event for it.
         */
        void submit_task_discarding(sycl::queue& queue, Scheduler::Task& task,
                                    const SmallEventVector* dependencies)
        {
            if (task.execution_mode() == Scheduler::Task::ExecutionMode::Host)
            {
                static_cast<void>(task.execute_host(queue, host_dependencies(dependencies)));
                return;
            }
#if defined(SR_HAS_ENQUEUE_FUNCTIONS)
            sycl::ext::oneapi::experimental::submit(queue, kernel_group(task, dependencies));
#else
            static_cast<void>(queue.submit(kernel_group(task, dependencies)));
#endif
        }

    } // namespace Internal::Graph
} // namespace SushiRuntime
