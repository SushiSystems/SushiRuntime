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
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>

namespace SushiRuntime
{
    namespace Execution { class RuntimeContext; }

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
         * @brief Holds a worker's counters since start().
         */
        struct WorkerStatistics
        {
            std::uint64_t tasks_run = 0;                      /**< Kernels that ran to the end. */
            std::uint64_t tasks_failed_kernel = 0;            /**< Kernels that threw. */
            std::uint64_t tasks_failed_output_allocation = 0; /**< Outputs that did not fit. */
            std::uint64_t tasks_failed_input_unavailable = 0; /**< Inputs that never arrived. */
            std::uint64_t tasks_failed_unknown_operation = 0; /**< Operations with no kernel. */
            std::uint64_t tasks_failed_shutting_down = 0;     /**< Tasks failed by stop(). */
            std::uint64_t cache_bytes = 0;                    /**< Bytes the cache holds. */
            std::uint64_t cache_evictions = 0;                /**< Buffers the cache dropped. */
            std::uint64_t reconnects = 0;                     /**< Registrations after the first. */
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
                 * @brief Builds an agent; nothing runs until start().
                 * @param context Supplies the queue kernels run on and the cache's memory;
                 *        outlives the agent.
                 * @param transport A worker-side transport that was never started; not null.
                 * @param kernels Not null; read from the exec thread.
                 */
                WorkerAgent(Execution::RuntimeContext& context,
                            std::shared_ptr<ITransport> transport,
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
