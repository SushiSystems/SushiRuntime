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
 * @file task_resubmission.cpp
 * @brief Implements resubmit_tasks(), which returns taken tasks to the scheduler.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include "task_resubmission.hpp"

#include <exception>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/distributed/distributed_log_category.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>

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
                    SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY, "task {} could not be resubmitted: {}; its node is released "
                                 "and will not run",
                                 task_id, error.what());
                }
                catch (...)
                {
                    SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY, "task {} could not be resubmitted: unknown exception; its node "
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
                    SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY, "a released node kept its first offer: {}",
                                 error.what());
                }
                catch (...)
                {
                    SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY,
                                 "a released node kept its first offer: unknown exception");
                }
            }
        } // namespace

        std::size_t resubmit_tasks(SushiRuntime::span<PendingTask> tasks,
                                   BufferRegistry& registry, BufferExchange& exchange,
                                   WorkerDirectory& directory, Internal::Graph::OffloadSink& sink) noexcept
        {
            for (PendingTask& task : tasks)
            {
                // Chunks of the task still in flight meet the bumped version and are refused.
                registry.bump(
                    SushiRuntime::span<BufferHandle>(task.outputs.data(), task.outputs.size()));
                exchange.forget_task(task.task_id);
                directory.release_inflight(task.worker);

                Internal::Graph::Node* node = task.node;
                if (node != nullptr && !hand_to_sink(sink, *node, task.task_id))
                    node->release();
            }
            return tasks.size();
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
