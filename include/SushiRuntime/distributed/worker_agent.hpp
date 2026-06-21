/**************************************************************************/
/* worker_agent.hpp                                                       */
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

#include <chrono>
#include <memory>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>

namespace SushiRuntime
{
    namespace Execution { class RuntimeContext; }

    namespace Distributed
    {
        /**
         * @brief Stateless executor daemon running on each worker machine.
         *
         * The agent is a thin network shell around a normal, local RuntimeContext.
         * Inside the worker, the existing NUMA-aware hierarchical stealing and the
         * intra-NUMA manager run unchanged — the agent merely feeds them work that
         * arrived over the wire.
         *
         * Cycle:
         *   1. HELLO the master with this binary's registered OpID set.
         *   2. On TASK_DISPATCH: resolve each input BufferHandle against the local
         *      cache (pull-on-miss via BUFFER_REQUEST), run the OpID's kernel on
         *      the local context, cache outputs under their new versions.
         *   3. Reply TASK_RESULT (lazy — output bytes travel only when the master
         *      later requests them as another task's input).
         *
         * The buffer cache is keyed by {id, version}: a dispatch referencing a
         * version the worker lacks triggers a pull, which is exactly how a
         * master-side write (version bump) invalidates a stale cache entry.
         */
        class SUSHIRUNTIME_API WorkerAgent
        {
            public:
                WorkerAgent(Execution::RuntimeContext& ctx,
                            std::shared_ptr<ITransport> transport,
                            sushi_ptr<KernelRegistry>   kernels);
                ~WorkerAgent();

                WorkerAgent(const WorkerAgent&) = delete;
                WorkerAgent& operator=(const WorkerAgent&) = delete;

                /**
                 * @brief Sets the liveness heartbeat period (default 1s).
                 *
                 * The agent sends a HEARTBEAT to the master every interval so a
                 * crash or hang is detectable as missed beats. A zero duration
                 * disables heartbeats (used by tests to simulate a dead worker).
                 * Must be called before start().
                 */
                void set_heartbeat_interval(std::chrono::milliseconds interval);

                /**
                 * @brief Sets how long an input buffer pull may wait (default 5s).
                 *
                 * resolve_input() blocks until the master pushes the requested
                 * {id, version}. Without a bound, a lost BUFFER_PUSH would hang the
                 * task forever — and because heartbeats run on a separate thread, the
                 * master would still see the worker as alive and never re-dispatch,
                 * stalling the whole graph. On timeout the task fails cleanly
                 * (TASK_RESULT success=false) so the master can recover. A zero or
                 * negative duration means wait forever (legacy behaviour).
                 * Must be called before start().
                 */
                void set_pull_timeout(std::chrono::milliseconds timeout);

                /**
                 * @brief Caps the pending-job queue (default 1024); 0 means unbounded.
                 *
                 * A dispatch arriving when the queue is full is fast-failed so the
                 * master re-dispatches it, bounding this worker's memory under a
                 * flood. May be called at any time.
                 */
                void set_max_jobs(size_t max_jobs);

                /** @brief Starts the transport and the message-processing loop. */
                void start();

                /** @brief Blocks until stop() is called from another thread. */
                void run();

                /** @brief Signals the agent to drain and stop. Safe from any thread. */
                void stop();

            private:
                struct Impl;
                sushi_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
