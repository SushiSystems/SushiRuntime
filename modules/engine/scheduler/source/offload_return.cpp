/**************************************************************************/
/* offload_return.cpp                                                     */
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
 * @file offload_return.cpp
 * @brief Takes back a node the offload coordinator accepted and has finished with.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#include <SushiRuntime/scheduler/task_scheduler.hpp>

#include <atomic>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/scheduler/scheduler_log_category.hpp>
#include <SushiRuntime/task/node.hpp>

#include "task_scheduler_parts.hpp"

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Queues a node no worker finished and drops the coordinator's reference.
         * @param node Accepted by the coordinator's try_offload().
         */
        void TaskScheduler::resubmit(Internal::Graph::Node& node)
        {
            // submit() takes its own reference before the coordinator's is dropped.
            submit(&node);
            node.release();
        }

        /**
         * @brief Releases a remotely finished node's successors, then drops the coordinator's
         *        reference.
         * @param node Accepted by the coordinator's try_offload().
         */
        void TaskScheduler::complete_remote(Internal::Graph::Node& node)
        {
            // Ready successors go through submit() on this coordinator thread.
            node.for_each_successor([this](Internal::Graph::Node* successor)
            {
                if (successor->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    submit(successor);
            });

            node.release();
        }

        void TaskScheduler::fail(Internal::Graph::Node& node, Error error)
        {
            SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY,
                         "{}; the run is aborted and its remaining tasks are skipped.",
                         error.message);

            // The failure is recorded before a successor can run, so each one is skipped.
            parts_->run.outcome.record(std::move(error));
            complete_remote(node);
        }

    } // namespace Scheduler
} // namespace SushiRuntime
