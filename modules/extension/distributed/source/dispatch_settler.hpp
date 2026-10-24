/**************************************************************************/
/* dispatch_settler.hpp                                                   */
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
 * @file dispatch_settler.hpp
 * @brief Declares DispatchSettler, which reports each job's outcome to the master.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <SushiRuntime/distributed/worker_agent.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

#include "dispatch_job.hpp"
#include "held_tasks.hpp"
#include "worker_buffer_cache.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Ends each job: commits and streams the outputs of one that ran, or sends a
         *        failed TASK_RESULT, and counts the outcome.
         *
         * Every way a task leaves goes through it, so each one lets its held id go once.
         * @see modules/extension/distributed/README.md
         */
        class DispatchSettler final
        {
            public:
                /**
                 * @brief Builds a settler.
                 * @param cache Outlives the settler and every stream it sent.
                 * @param held Shared with every output stream; not null.
                 */
                DispatchSettler(WorkerBufferCache& cache, Transport& transport,
                                std::shared_ptr<HeldTasks> held) noexcept;

                DispatchSettler(const DispatchSettler&) = delete;
                DispatchSettler& operator=(const DispatchSettler&) = delete;
                DispatchSettler(DispatchSettler&&) = delete;
                DispatchSettler& operator=(DispatchSettler&&) = delete;

                /**
                 * @brief Commits and streams the outputs of a job that ran, or fails it.
                 * @param run_failure Why the run failed, or empty.
                 */
                void settle(DispatchEntry& entry, const std::string& run_failure);

                /** @brief Sends TASK_RESULT alone with a failure status and lets the task go. */
                void fail(PeerID from, std::uint64_t task_id, std::uint64_t generation,
                          Wire::TaskResultStatus status);

                /** @brief Lets a task go after the worker failed outside the kernel. */
                void abandon(const DispatchJob& job, const char* cause) noexcept;

                /** @brief Returns the task counters; cache and reconnect fields stay 0. */
                [[nodiscard]] WorkerStatistics statistics() const noexcept;

            private:
                /** @brief Counts one failed task under its status. */
                void count_failure(Wire::TaskResultStatus status) noexcept;

                /// @brief Takes the committed outputs and drops a failed task's partial ones.
                WorkerBufferCache& cache_;

                /// @brief Carries output streams and results.
                Transport& transport_;

                /// @brief The tasks held, shared with every output stream.
                std::shared_ptr<HeldTasks> held_;

                /// @brief Kernels that ran to the end.
                std::atomic<std::uint64_t> tasks_run_{0};

                /// @brief Kernels that threw.
                std::atomic<std::uint64_t> failed_kernel_{0};

                /// @brief Tasks whose output did not fit.
                std::atomic<std::uint64_t> failed_output_allocation_{0};

                /// @brief Tasks whose input never arrived.
                std::atomic<std::uint64_t> failed_input_unavailable_{0};

                /// @brief Tasks whose operation has no kernel.
                std::atomic<std::uint64_t> failed_unknown_operation_{0};

                /// @brief Tasks failed by stop().
                std::atomic<std::uint64_t> failed_shutting_down_{0};
        };

    } // namespace Distributed
} // namespace SushiRuntime
