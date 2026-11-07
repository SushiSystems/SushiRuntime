/**************************************************************************/
/* worker_agent.hpp                                                       */
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
 * @file worker_agent.hpp
 * @brief Declares WorkerAgent, which runs a master's tasks inside the embedding application.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>
#include <SushiRuntime/distributed/worker_statistics.hpp>
#include <SushiRuntime/transport/transport.hpp>

namespace SushiRuntime
{
    class Runtime;

    namespace Distributed
    {
        /**
         * @brief Holds what a worker agent is built with; moved into its constructor.
         */
        struct WorkerAgentOptions
        {
            /** @brief Holds the cluster's key; empty joins only a master without one. */
            SharedSecret secret;

            /** @brief Holds the name shown in the master's log, at most 255 bytes. */
            std::string name;

            /** @brief Caps the tasks waiting to run; the master is told in PROOF. */
            std::size_t max_jobs = 1024;

            /** @brief Caps the bytes of cached buffers; 0 takes half of the device's memory. */
            std::uint64_t cache_bytes = 0;

            /** @brief Holds the interval between two HEARTBEAT frames. */
            std::chrono::milliseconds heartbeat_interval{1000};

            /** @brief Fails a task when no chunk of a requested buffer arrives for this long. */
            std::chrono::milliseconds pull_idle_timeout{5000};

            /**
             * @brief Tells whether start() runs the exec and heartbeat threads.
             *
             * False leaves them to the caller, who runs what they would run through step().
             */
            bool own_threads = true;
        };

        /**
         * @brief Names why a worker agent stopped serving, or that it still serves.
         */
        enum class WorkerEnd : std::uint8_t
        {
            Running,                /**< The agent serves, or is between two connections. */
            Stopped,                /**< stop() was called. */
            MasterUnreachable,      /**< The transport never reached the master and gave up. */
            RejectedAuthentication, /**< One side's proof failed: the secrets differ. */
            ProtocolMismatch,       /**< The peer is another protocol version, or no master. */
            MasterLost,             /**< The master went away and the transport does not retry. */
        };

        /**
         * @brief Registers with a master over a transport and runs the kernels it dispatches.
         *
         * The agent is the transport's only event receiver. It runs one exec thread, which
         * calls the kernels one at a time, and one heartbeat thread.
         * @see modules/extension/distributed/README.md
         */
        class SUSHIRUNTIME_API WorkerAgent final
        {
            public:
                /**
                 * @brief Builds an agent on a runtime; nothing runs until start().
                 * @param runtime Supplies the queue kernels run on and the cache's memory. The
                 *        agent shares its context, which therefore outlives the agent.
                 * @param transport A worker-side transport that was never started; not null.
                 * @param kernels Not null; read from the exec thread.
                 */
                WorkerAgent(Runtime& runtime, std::shared_ptr<Transport> transport,
                            sushi_ptr<KernelRegistry> kernels,
                            WorkerAgentOptions options = WorkerAgentOptions());

                /** @brief Stops the agent and frees its cache. */
                ~WorkerAgent();

                WorkerAgent(const WorkerAgent&) = delete;
                WorkerAgent& operator=(const WorkerAgent&) = delete;
                WorkerAgent(WorkerAgent&&) = delete;
                WorkerAgent& operator=(WorkerAgent&&) = delete;

                /**
                 * @brief Starts the exec and heartbeat threads, then the transport.
                 * @return The transport's result; on a failure no thread is left running.
                 */
                [[nodiscard]] TransportResult start();

                /**
                 * @brief Does what the agent's threads would, on the calling thread.
                 *
                 * The link's due heartbeat or watch, then one batch of the queued jobs. For an
                 * agent built with own_threads false; with threads running, they do it.
                 * @return True when a batch ran.
                 */
                bool step();

                /**
                 * @brief Blocks until the agent stops serving, or the timeout.
                 *
                 * An agent that ended for a reason other than stop() answers no connection
                 * and still needs stop().
                 * @return Running when the timeout passed first.
                 */
                [[nodiscard]] WorkerEnd wait_for_end(std::chrono::milliseconds timeout);

                /**
                 * @brief Ends the agent in order; blocks while the running kernel finishes.
                 *
                 * Takes no more tasks, fails the queued ones as shutting down, sends GOODBYE,
                 * stops the transport and joins the threads. A second call does nothing.
                 */
                void stop() noexcept;

                /** @brief Returns the counters as they stand; any thread may call. */
                [[nodiscard]] WorkerStatistics statistics() const noexcept;

            private:
                struct Impl;

                /** @brief Holds the four worker bricks and what composes them. */
                std::unique_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
