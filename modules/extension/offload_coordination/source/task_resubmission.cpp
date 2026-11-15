/**************************************************************************/
/* task_resubmission.cpp                                                  */
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
 * @file task_resubmission.cpp
 * @brief Implements the functions that return nodes to the scheduler, producers of lost data first.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#include <SushiRuntime/offload_coordination/task_resubmission.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iterator>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>

#include <SushiRuntime/offload_coordination/offload_coordination_log_category.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /**
             * @brief Passes one node to the sink and reports whether the sink took it.
             * @return False when the sink threw; the caller still holds the node's reference.
             */
            bool hand_to_sink(Internal::Graph::OffloadSink& sink, Internal::Graph::Node& node,
                              std::uint64_t task_id) noexcept
            {
                try
                {
                    sink.resubmit(node);
                    return true;
                }
                catch (const std::exception& error)
                {
                    SR_LOG_ERROR(K_OFFLOAD_COORDINATION_LOG_CATEGORY,
                                 "task {} could not be resubmitted: {}; its node is released "
                                 "and will not run",
                                 task_id, error.what());
                }
                catch (...)
                {
                    SR_LOG_ERROR(K_OFFLOAD_COORDINATION_LOG_CATEGORY,
                                 "task {} could not be resubmitted: unknown exception; its node "
                                 "is released and will not run",
                                 task_id);
                }
                return false;
            }

            /**
             * @brief Forgets the first offer of a node about to be released.
             *
             * A plan rearmed later may reuse the node's address, which must not inherit the wait.
             */
            void forget_first_offer(HeldNodes& held, const Internal::Graph::Node& node) noexcept
            {
                try
                {
                    held.forget(node);
                }
                catch (const std::exception& error)
                {
                    SR_LOG_ERROR(K_OFFLOAD_COORDINATION_LOG_CATEGORY,
                                 "a released node kept its first offer: {}", error.what());
                }
                catch (...)
                {
                    SR_LOG_ERROR(K_OFFLOAD_COORDINATION_LOG_CATEGORY,
                                 "a released node kept its first offer: unknown exception");
                }
            }
        } // namespace

        std::size_t resubmit_tasks(SushiRuntime::span<PendingTask> tasks,
                                   SharedBuffers& buffers, BufferExchange& exchange,
                                   WorkerDirectory& directory, Internal::Graph::OffloadSink& sink) noexcept
        {
            for (PendingTask& task : tasks)
            {
                // Chunks of the task still in flight meet the bumped version and are refused.
                buffers.bump(
                    SushiRuntime::span<BufferHandle>(task.outputs.data(), task.outputs.size()));
                exchange.forget_task(task.task_id);
                directory.release_inflight(task.worker);

                Internal::Graph::Node* node = task.node;
                if (node != nullptr && !hand_to_sink(sink, *node, task.task_id))
                    node->release();
            }
            return tasks.size();
        }

        std::size_t order_for_rebuild(SushiRuntime::span<const LostInstance> lost,
                                      std::vector<PendingTask>& tasks)
        {
            std::size_t missing = 0;
            auto front = tasks.begin();
            for (const LostInstance& instance : lost)
            {
                const auto is_producer = [&instance](const PendingTask& task)
                {
                    return task.task_id == instance.producer.task_id;
                };
                if (std::find_if(tasks.begin(), front, is_producer) != front)
                    continue;

                const auto found = std::find_if(front, tasks.end(), is_producer);
                if (found == tasks.end())
                {
                    ++missing;
                    continue;
                }
                std::rotate(front, found, std::next(found));
                ++front;
            }
            return missing;
        }

        std::size_t reoffer_nodes(const std::vector<Internal::Graph::Node*>& nodes,
                                  HeldNodes& held, Internal::Graph::OffloadSink& sink) noexcept
        {
            // A held node has no task id; 0 names none in the log line.
            for (Internal::Graph::Node* node : nodes)
                if (node != nullptr && !hand_to_sink(sink, *node, 0))
                {
                    forget_first_offer(held, *node);
                    node->release();
                }
            return nodes.size();
        }

    } // namespace Distributed
} // namespace SushiRuntime
