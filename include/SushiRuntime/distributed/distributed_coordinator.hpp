/**************************************************************************/
/* distributed_coordinator.hpp                                            */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#pragma once

#include <memory>
#include <functional>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/i_offload_policy.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/buffer_registry.hpp>

namespace SushiRuntime
{
    namespace Graph { struct Node; }

    namespace Distributed
    {
        /**
         * @brief Master-side brain: the only distributed object the scheduler touches.
         *
         * The scheduler's dispatch loop calls try_offload() for the handful of
         * tasks that compile() flagged offloadable. Everything else — node
         * registry, buffer directory, the pull-based coherence protocol — lives
         * behind the Pimpl and never appears on the local hot path.
         *
         * Coherence model (kept deliberately simple for v1):
         *   - master is authoritative for every registered buffer;
         *   - workers cache by {id, version} and pull on miss;
         *   - a local write bumps the version (BufferRegistry::bump_version),
         *     which transparently invalidates stale worker caches.
         *
         * Result handling: when a worker reports TASK_RESULT, the coordinator
         * resolves the task_id back to its Node and invokes the completion hook
         * the scheduler installed — the network equivalent of EventPolling
         * firing a SYCL event and releasing successors.
         */
        class SUSHIRUNTIME_API DistributedCoordinator
        {
            public:
                /** @brief Invoked (off the hot path) when an offloaded node finishes. */
                using CompletionHook = std::function<void(Graph::Node* node, bool success)>;

                DistributedCoordinator(sushi_ptr<KernelRegistry> kernels,
                                       BufferRegistry&           buffers,
                                       std::shared_ptr<ITransport>     transport,
                                       std::shared_ptr<IOffloadPolicy> policy);
                ~DistributedCoordinator();

                DistributedCoordinator(const DistributedCoordinator&) = delete;
                DistributedCoordinator& operator=(const DistributedCoordinator&) = delete;

                /** @brief Starts transport, waits for workers to register (HELLO). */
                void start();

                /** @brief Drains inflight remote work and stops the transport. */
                void stop();

                /**
                 * @brief True if at least one live worker can run this OpID.
                 *
                 * Queried by TaskGraph::compile() to decide offloadability. An op
                 * absent from every worker's registry keeps its tasks local.
                 */
                bool supports(Graph::OpID op) const;

                /**
                 * @brief Installs the hook used to release a node's successors.
                 *
                 * The scheduler passes a closure that performs the same
                 * successor-decrement / ready-enqueue logic EventPolling uses.
                 */
                void set_completion_hook(CompletionHook hook);

                /**
                 * @brief Attempts to offload one ready, offloadable node.
                 *
                 * @return true  -> the node was shipped to a worker; the caller
                 *                  must NOT run it locally.
                 *         false -> no worker chosen (policy declined / none ready);
                 *                  the caller runs it locally (transparent fallback).
                 *
                 * Called from the scheduler's dispatch loop behind a single
                 * [[unlikely]] offloadable check, so non-offloaded tasks never
                 * reach here.
                 */
                bool try_offload(Graph::Node* node);

            private:
                struct Impl;
                sushi_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
