/**************************************************************************/
/* dispatch_executor.hpp                                                  */
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
 * @file dispatch_executor.hpp
 * @brief Declares DispatchExecutor, the worker's job queue and the thread that runs kernels.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>

#include "wire_messages.hpp"
#include "worker_buffer_cache.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Holds the executor's bounds.
         */
        struct DispatchExecutorOptions
        {
            /** @brief Most jobs waiting to run; enqueue() then returns QueueFull. */
            std::size_t max_jobs = 1024;

            /** @brief Fails a task when no chunk of a requested input arrives for this long. */
            std::chrono::milliseconds pull_idle_timeout{5000};
        };

        /**
         * @brief Names what enqueue() did with a dispatch.
         */
        enum class EnqueueStatus : std::uint8_t
        {
            Queued,    /**< The job waits for the exec thread; send TASK_ACCEPTED. */
            QueueFull, /**< The queue is at max_jobs; send nothing, the master takes it back. */
            Stopping,  /**< stop() has begun; the executor queued a ShuttingDown result itself. */
        };

        /**
         * @brief Runs dispatched kernels one at a time and returns their outputs and results.
         *
         * A job: pin each output, get each input from the cache or pull it, call the kernel
         * inside one try and wait for its event, then send one stream of output chunks that
         * ends with TASK_RESULT. A failed task sends TASK_RESULT alone, as a control frame.
         * The executor's one mutex guards the queue and is released before any call out.
         */
        class SUSHIRUNTIME_API DispatchExecutor final
        {
            public:
                /**
                 * @brief Builds an executor; no thread runs until start().
                 * @param kernels Not null; only read.
                 * @param cache Outlives the executor and every stream it sent.
                 * @param queue The queue kernels submit to; outlives the executor.
                 */
                DispatchExecutor(sushi_ptr<KernelRegistry> kernels, WorkerBufferCache& cache,
                                 ITransport& transport, sycl::queue& queue,
                                 const DispatchExecutorOptions& options);

                /** @brief Calls stop(). */
                ~DispatchExecutor();

                DispatchExecutor(const DispatchExecutor&) = delete;
                DispatchExecutor& operator=(const DispatchExecutor&) = delete;
                DispatchExecutor(DispatchExecutor&&) = delete;
                DispatchExecutor& operator=(DispatchExecutor&&) = delete;

                /** @brief Starts the exec thread; called once. */
                void start();

                /**
                 * @brief Queues a dispatch the deduplicator has passed; called on the reader.
                 * @param from The connection the dispatch came on; every reply goes to it.
                 * @param chunk_bytes Data bytes per output chunk: MasterLink::chunk_bytes().
                 */
                [[nodiscard]] EnqueueStatus enqueue(PeerId from, Wire::TaskDispatchMessage dispatch,
                                                    std::uint32_t chunk_bytes);

                /** @brief Drops every queued job without a result; the master is gone. */
                void discard_queued() noexcept;

                /**
                 * @brief Refuses later jobs, sends a ShuttingDown result for each queued one,
                 *        lets the running kernel finish and joins the thread.
                 */
                void stop() noexcept;

                /** @brief Returns the number of jobs waiting, for HEARTBEAT. */
                [[nodiscard]] std::uint32_t queue_depth() const noexcept;

                /**
                 * @brief Returns HEARTBEAT's oldest_held_task_id.
                 *
                 * A task is held from enqueue() until its TASK_RESULT frame has been handed to
                 * the transport's writer: pulled from its stream, or queued as a control frame.
                 * @return The smallest held id; with none held, the largest id whose result
                 *         was handed over plus one, or 0 before the first.
                 */
                [[nodiscard]] std::uint64_t oldest_held_task_id() const;

                /** @brief Returns the task counters; cache and reconnect fields stay 0. */
                [[nodiscard]] WorkerStatistics statistics() const noexcept;

            private:
                /**
                 * @brief Keeps the task ids the worker holds for one connection, for HEARTBEAT.
                 *
                 * An output stream may outlive the executor, so the two share this object. Its
                 * mutex is a leaf: nothing is called while it is held.
                 */
                class HeldTasks final
                {
                    public:
                        /**
                         * @brief Starts holding a task.
                         * @return The generation let_go() must be given for the same task.
                         */
                        [[nodiscard]] std::uint64_t hold(std::uint64_t task_id);

                        /** @brief Stops holding a task whose result left or cannot leave. */
                        void let_go(std::uint64_t task_id, std::uint64_t generation) noexcept;

                        /** @brief Forgets every task and the idle value; the connection is gone. */
                        void reset() noexcept;

                        /** @brief Returns the smallest held id, or the idle value when none is. */
                        [[nodiscard]] std::uint64_t oldest() const;

                    private:
                        /// @brief Guards every member below.
                        mutable std::mutex mutex_;

                        /// @brief The ids held, smallest first.
                        std::set<std::uint64_t> held_;

                        /// @brief The largest id let go plus one; 0 before the first.
                        std::uint64_t idle_value_ = 0;

                        /// @brief Counts the resets, so a task of an earlier connection is ignored.
                        std::uint64_t generation_ = 0;
                };

                /** @brief Produces the output chunks of one finished task, then its TASK_RESULT. */
                class OutputStream;

                /** @brief Holds one dispatch that waits for the exec thread. */
                struct Job
                {
                    /** @brief The connection every reply goes to. */
                    PeerId from;

                    /** @brief The task. */
                    Wire::TaskDispatchMessage dispatch;

                    /** @brief Data bytes per output chunk. */
                    std::uint32_t chunk_bytes = 0;

                    /** @brief What HeldTasks::hold() returned for the task. */
                    std::uint64_t generation = 0;
                };

                /** @brief Runs jobs until stop(). */
                void run() noexcept;

                /** @brief Counts one failed task under its status. */
                void count_failure(Wire::TaskResultStatus status) noexcept;

                /** @brief Sends TASK_RESULT alone with a failure status and lets the task go. */
                void fail(PeerId from, std::uint64_t task_id, std::uint64_t generation,
                          Wire::TaskResultStatus status);

                /** @brief Lets a task go after execute() failed, with a result if one fits. */
                void abandon(const Job& job, const char* cause) noexcept;

                /** @brief Gets one input from the cache, pulling it from the master on a miss. */
                [[nodiscard]] CacheStatus acquire_input(const Job& job, const BufferHandle& handle,
                                                        BufferView& view);

                /** @brief Logs that the cache has no room for a buffer of a task. */
                void log_no_room(std::uint64_t task_id, const BufferHandle& handle,
                                 const char* consequence) const;

                /** @brief Runs one job from its pins to its result. */
                void execute(const Job& job);

                /// @brief Guards jobs_ and stopping_.
                mutable std::mutex mutex_;

                /// @brief Wakes the exec thread on a job and on stop().
                std::condition_variable ready_;

                /// @brief The kernels this worker can run.
                sushi_ptr<KernelRegistry> kernels_;

                /// @brief Supplies and pins the buffers of each job.
                WorkerBufferCache& cache_;

                /// @brief Carries requests, output streams and results.
                ITransport& transport_;

                /// @brief The queue kernels submit to.
                sycl::queue& queue_;

                /// @brief The queue bound and the pull timeout.
                DispatchExecutorOptions options_;

                /// @brief The exec thread.
                std::thread thread_;

                /// @brief Set by stop().
                bool stopping_ = false;

                /// @brief Jobs waiting, for queue_depth().
                std::atomic<std::uint32_t> queue_depth_{0};

                /// @brief Jobs waiting for the exec thread, oldest first.
                std::deque<Job> jobs_;

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
