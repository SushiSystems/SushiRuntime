/****************************************************************/
/* worker_agent.hpp                                             */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file worker_agent.hpp
 * @brief Declares WorkerAgent, which executes distributed tasks on a worker machine.
 * @author Mustafa Garip
 */

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
         * @brief Serves as a daemon that executes distributed tasks on a worker machine.
         *
         * Bridges the network transport and the local runtime context, fetching required remote data and executing incoming dispatches.
         */
        class SUSHIRUNTIME_API WorkerAgent
        {
            public:
                /**
                 * @brief Constructs a worker agent bound to a local runtime context and attaches
                 *        the transport.
                 */
                WorkerAgent(Execution::RuntimeContext& ctx,
                            std::shared_ptr<ITransport> transport,
                            sushi_ptr<KernelRegistry>   kernels);

                /**
                 * @brief Cleans up the worker agent resources.
                 *
                 * Automatically stops the internal event loops and releases memory before destruction.
                 */
                ~WorkerAgent();

                WorkerAgent(const WorkerAgent&) = delete;
                WorkerAgent& operator=(const WorkerAgent&) = delete;

                /**
                 * @brief Configures the time interval between network liveness pings.
                 *
                 * Schedules periodic heartbeat messages to notify the master that the worker is still active.
                 *
                 * @param interval The duration between consecutive heartbeat transmissions.
                 * @warning Must be called prior to invoking start().
                 */
                void set_heartbeat_interval(std::chrono::milliseconds interval);

                /**
                 * @brief Defines the maximum duration to wait for a requested buffer transfer.
                 *
                 * Forces tasks to fail gracefully instead of hanging indefinitely if a remote data fetch times out.
                 *
                 * @param timeout The maximum allowed wait time for a buffer transfer, or zero/negative to wait indefinitely.
                 * @warning Must be called prior to invoking start().
                 */
                void set_pull_timeout(std::chrono::milliseconds timeout);

                /**
                 * @brief Limits the maximum number of queued pending jobs to prevent memory exhaustion.
                 *
                 * Rejects incoming dispatches immediately if the internal queue exceeds this limit.
                 *
                 * @param max_jobs The strict upper bound on the number of pending tasks, or 0 for an unbounded queue.
                 */
                void set_max_jobs(size_t max_jobs);

                /**
                 * @brief Activates network communication and starts the background message processing loop.
                 *
                 * Transitions the agent into a running state where it can accept and process network payloads.
                 */
                void start();

                /**
                 * @brief Suspends the calling thread until the agent is explicitly stopped.
                 *
                 * Blocks execution synchronously by waiting on an internal synchronization primitive.
                 */
                void run();

                /**
                 * @brief Triggers a graceful shutdown of the agent's background loops.
                 *
                 * Signals internal threads to exit and cleanly interrupts the blocking run method.
                 */
                void stop();

            private:
                struct Impl;
                sushi_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
