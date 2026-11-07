/**************************************************************************/
/* dispatch_executor.cpp                                                  */
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
 * @file dispatch_executor.cpp
 * @brief Implements DispatchExecutor: the job queue, the exec thread and the pins of a batch.
 * @author Mustafa Garip
 * @see modules/extension/dispatch_executor/README.md
 */

#include <SushiRuntime/dispatch_executor/dispatch_executor.hpp>

#include <algorithm>
#include <cstddef>
#include <deque>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/dispatch_executor/dispatch_executor_log_category.hpp>
#include <SushiRuntime/frame_transport/frame_sending.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Returns the words a log line uses for a cache answer. */
            const char* cache_status_name(CacheStatus status) noexcept
            {
                switch (status)
                {
                    case CacheStatus::Ready:
                        return "ready";
                    case CacheStatus::NeedsPull:
                        return "needs a pull";
                    case CacheStatus::AllocationFailed:
                        return "no room in the cache";
                    case CacheStatus::TimedOut:
                        return "no chunk within the pull idle timeout";
                    case CacheStatus::Aborted:
                        return "transfer aborted";
                    case CacheStatus::Cancelled:
                        return "cache cleared for a new master session";
                }
                return "unknown";
            }

            /**
             * @brief Unpins the buffers it was given unless they are handed on.
             */
            class PinGuard final
            {
                public:
                    /** @brief Builds a guard with no pins. */
                    explicit PinGuard(WorkerBufferCache& cache) noexcept : cache_(cache)
                    {

                    }

                    /** @brief Drops every pin still guarded. */
                    ~PinGuard()
                    {
                        for (const GlobalBufferID id : ids_)
                            cache_.unpin(id);
                    }

                    PinGuard(const PinGuard&) = delete;
                    PinGuard& operator=(const PinGuard&) = delete;
                    PinGuard(PinGuard&&) = delete;
                    PinGuard& operator=(PinGuard&&) = delete;

                    /** @brief Makes room for a number of pins, so add() cannot fail. */
                    void reserve(std::size_t count)
                    {
                        ids_.reserve(count);
                    }

                    /** @brief Guards one pin; reserve() covered it. */
                    void add(GlobalBufferID id) noexcept
                    {
                        ids_.push_back(id);
                    }

                    /**
                     * @brief Stops guarding: another owner unpins from here on.
                     * @return The ids of the pins handed on.
                     */
                    [[nodiscard]] std::vector<GlobalBufferID> hand_on() noexcept
                    {
                        return std::move(ids_);
                    }

                private:
                    /// @brief The cache the pins belong to.
                    WorkerBufferCache& cache_;

                    /// @brief One id per pin.
                    std::vector<GlobalBufferID> ids_;
            };
        } // namespace

        /**
         * @brief Holds the jobs of one run and drops their pins when it goes.
         *
         * Its entries never move, because the recorded tasks refer to them.
         */
        class DispatchExecutor::Batch final
        {
            public:
                /** @brief Builds an empty batch over the cache that holds its pins. */
                explicit Batch(WorkerBufferCache& cache) noexcept : cache_(cache)
                {

                }

                /** @brief Drops every pin still held. */
                ~Batch()
                {
                    for (DispatchEntry& entry : entries_)
                        for (const GlobalBufferID id : entry.pins)
                            cache_.unpin(id);
                }

                Batch(const Batch&) = delete;
                Batch& operator=(const Batch&) = delete;
                Batch(Batch&&) = delete;
                Batch& operator=(Batch&&) = delete;

                /** @brief Adds a job whose buffers are pinned; the caller hands its pins on. */
                [[nodiscard]] DispatchEntry& add(DispatchJob job,
                                                 const KernelRegistry::Kernel& kernel,
                                                 std::vector<BufferView> inputs,
                                                 std::vector<BufferView> outputs)
                {
                    for (const BufferHandle& output : job.dispatch.outputs)
                        written_.push_back(output.id);
                    return entries_.emplace_back(std::move(job), kernel, std::move(inputs),
                                                 std::move(outputs));
                }

                /** @brief Returns the ids the jobs of the batch write. */
                [[nodiscard]] SushiRuntime::span<const GlobalBufferID> written() const noexcept
                {
                    return SushiRuntime::span<const GlobalBufferID>(written_.data(),
                                                                    written_.size());
                }

                /** @brief Tells whether the batch holds no job. */
                [[nodiscard]] bool empty() const noexcept
                {
                    return entries_.empty();
                }

                /** @brief Returns the jobs, in the order they were added. */
                [[nodiscard]] std::deque<DispatchEntry>& entries() noexcept
                {
                    return entries_;
                }

            private:
                /// @brief The cache the pins belong to.
                WorkerBufferCache& cache_;

                /// @brief One entry per job; a deque, so an entry stays where it was built.
                std::deque<DispatchEntry> entries_;

                /// @brief The ids the jobs of the batch write.
                std::vector<GlobalBufferID> written_;
        };

        DispatchExecutor::DispatchExecutor(sushi_ptr<KernelRegistry> kernels,
                                           WorkerBufferCache& cache, Transport& transport,
                                           Internal::Device::DeviceQueueSet& devices,
                                           Graph graph, Interop interop,
                                           const DispatchExecutorOptions& options)
            : kernels_(std::move(kernels)), cache_(cache), transport_(transport), devices_(devices),
              options_(options), held_(std::make_shared<HeldTasks>()),
              recorder_(std::move(graph), std::move(interop)),
              settler_(cache, transport, held_)
        {

        }

        DispatchExecutor::~DispatchExecutor()
        {
            stop();
        }

        void DispatchExecutor::start()
        {
            thread_ = std::thread(&DispatchExecutor::run, this);
        }

        EnqueueStatus DispatchExecutor::enqueue(PeerID from, Wire::TaskDispatchMessage dispatch,
                                                std::uint32_t chunk_bytes)
        {
            const std::uint64_t task_id = dispatch.task_id;
            bool stopping = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                const EnqueueStatus admitted =
                    admit_dispatch(jobs_.size(), options_.max_jobs, stopping_);
                if (admitted == EnqueueStatus::QueueFull)
                    return admitted;

                stopping = admitted == EnqueueStatus::Stopping;
                if (!stopping)
                {
                    DispatchJob job;
                    job.from = from;
                    job.dispatch = std::move(dispatch);
                    job.chunk_bytes = usable_chunk_bytes(chunk_bytes);
                    job.generation = held_->hold(task_id);
                    jobs_.push_back(std::move(job));
                    queue_depth_.fetch_add(1, std::memory_order_relaxed);
                }
            }

            if (stopping)
            {
                settler_.fail(from, task_id, held_->hold(task_id),
                              Wire::TaskResultStatus::ShuttingDown);
                return EnqueueStatus::Stopping;
            }

            ready_.notify_one();

            Wire::TaskAcceptedMessage accepted;
            accepted.task_id = task_id;
            const SendStatus sent = send_message(transport_, from, accepted);
            if (sent != SendStatus::Queued)
                SR_LOG_DEBUG(K_DISPATCH_EXECUTOR_LOG_CATEGORY, "task {}: TASK_ACCEPTED to peer {} was not queued (status {})",
                             task_id, from.value, static_cast<int>(sent));
            return EnqueueStatus::Queued;
        }

        void DispatchExecutor::discard_queued() noexcept
        {
            std::optional<BufferHandle> pulling;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                jobs_.clear();
                queue_depth_.store(0, std::memory_order_relaxed);
                pulling = pulling_;
            }
            held_->reset();

            // No chunk can arrive any more; the abort ends the exec thread's wait at once.
            if (pulling.has_value())
                cache_.abort_transfer(pulling->id, pulling->version);
        }

        void DispatchExecutor::stop() noexcept
        {
            std::deque<DispatchJob> queued;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
                queued.swap(jobs_);
                queue_depth_.store(0, std::memory_order_relaxed);
            }
            ready_.notify_all();

            try
            {
                for (const DispatchJob& job : queued)
                    settler_.fail(job.from, job.dispatch.task_id, job.generation,
                                  Wire::TaskResultStatus::ShuttingDown);
                if (!queued.empty())
                    SR_LOG_INFO(K_DISPATCH_EXECUTOR_LOG_CATEGORY, "worker stopping: {} queued tasks reported as shutting down",
                                queued.size());
            }
            catch (const std::exception& error)
            {
                SR_LOG_WARN(K_DISPATCH_EXECUTOR_LOG_CATEGORY, "worker stopping: not every queued task was reported: {}; the "
                            "master takes the rest back when the connection closes",
                            error.what());
            }
            catch (...)
            {
                SR_LOG_WARN(K_DISPATCH_EXECUTOR_LOG_CATEGORY, "worker stopping: not every queued task was reported: unknown "
                            "exception; the master takes the rest back when the connection "
                            "closes");
            }

            if (thread_.joinable())
                thread_.join();
        }

        std::uint32_t DispatchExecutor::queue_depth() const noexcept
        {
            return queue_depth_.load(std::memory_order_relaxed);
        }

        std::uint64_t DispatchExecutor::oldest_held_task_id() const
        {
            return held_->oldest();
        }

        WorkerStatistics DispatchExecutor::statistics() const noexcept
        {
            return settler_.statistics();
        }

        void DispatchExecutor::run() noexcept
        {
            for (;;)
            {
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock, [this]
                                {
                                    return stopping_ || !jobs_.empty();
                                });
                    if (stopping_)
                        return;
                }

                run_one_batch();
            }
        }

        bool DispatchExecutor::step()
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_ || jobs_.empty())
                    return false;
            }

            run_one_batch();
            return true;
        }

        void DispatchExecutor::run_one_batch()
        {
            Batch batch(cache_);
            // The last batch's tasks hold &entry of a destroyed deque; this drops them unrun.
            recorder_.clear();
            while (std::optional<DispatchJob> job = next_job(batch))
            {
                try
                {
                    prepare(*job, batch);
                }
                catch (const std::exception& error)
                {
                    settler_.abandon(*job, error.what());
                }
                catch (...)
                {
                    settler_.abandon(*job, "unknown exception");
                }
            }

            if (!batch.empty())
                run_batch(batch);
        }

        std::optional<DispatchJob> DispatchExecutor::next_job(const Batch& batch)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || jobs_.empty())
                return std::nullopt;

            // An input a job of this batch writes is in no cache yet; the next batch reads it.
            const std::vector<BufferHandle>& inputs = jobs_.front().dispatch.inputs;
            if (reads_a_written_buffer(
                    SushiRuntime::span<const BufferHandle>(inputs.data(), inputs.size()),
                    batch.written()))
                return std::nullopt;

            std::optional<DispatchJob> job(std::move(jobs_.front()));
            jobs_.pop_front();
            queue_depth_.fetch_sub(1, std::memory_order_relaxed);
            return job;
        }

        CacheStatus DispatchExecutor::acquire_input(const DispatchJob& job,
                                                    const BufferHandle& handle, BufferView& view)
        {
            const CacheStatus status = cache_.begin_input(handle, view);
            if (status != CacheStatus::NeedsPull)
                return status;

            {
                const std::lock_guard<std::mutex> lock(mutex_);
                pulling_ = handle;
            }

            // A peer lost before this send refuses it; one lost after it reaches pulling_.
            Wire::BufferRequestMessage request;
            request.id = handle.id;
            request.version = handle.version;
            if (send_message(transport_, job.from, request) != SendStatus::Queued)
                cache_.abort_transfer(handle.id, handle.version);
            const CacheStatus pulled =
                cache_.wait_input(handle, options_.pull_idle_timeout, view);

            const std::lock_guard<std::mutex> lock(mutex_);
            pulling_.reset();
            return pulled;
        }

        void DispatchExecutor::log_no_room(std::uint64_t task_id, const BufferHandle& handle,
                                           const char* consequence) const
        {
            SR_LOG_ERROR(K_DISPATCH_EXECUTOR_LOG_CATEGORY, "task {}: cannot allocate {} bytes for buffer {} (budget {}, in use "
                         "{}); reported as {}",
                         task_id, handle.size_bytes, handle.id.value, cache_.budget(),
                         cache_.bytes(), consequence);
        }

        JobVerdict DispatchExecutor::verdict_of(const DispatchJob& job, bool kernel_known)
        {
            const std::uint32_t device = job.dispatch.device_index;
            const bool shares_context =
                device < devices_.count() &&
                devices_.queue(device).get_context() ==
                    devices_.queue(options_.cache_device).get_context();
            return judge_job(kernel_known, device, devices_.count(), shares_context);
        }

        void DispatchExecutor::prepare(DispatchJob& job, Batch& batch)
        {
            const Wire::TaskDispatchMessage& dispatch = job.dispatch;
            const std::uint64_t task_id = dispatch.task_id;

            const KernelRegistry::Kernel* kernel = kernels_->find(dispatch.operation);
            const JobVerdict verdict = verdict_of(job, kernel != nullptr);
            if (verdict == JobVerdict::UnknownOperation)
            {
                SR_LOG_WARN(K_DISPATCH_EXECUTOR_LOG_CATEGORY, "task {}: this worker has no kernel for operation {}; reported as "
                            "unknown operation",
                            task_id, dispatch.operation.value);
                settler_.fail(job.from, task_id, job.generation,
                              Wire::TaskResultStatus::UnknownOperation);
                return;
            }

            if (verdict == JobVerdict::DeviceMissing)
            {
                SR_LOG_WARN(K_DISPATCH_EXECUTOR_LOG_CATEGORY,
                            "task {}: the dispatch names device {}, and this worker has {}; "
                            "reported as kernel failed",
                            task_id, dispatch.device_index, devices_.count());
                settler_.fail(job.from, task_id, job.generation,
                              Wire::TaskResultStatus::KernelFailed);
                return;
            }

            if (verdict == JobVerdict::DeviceOutsideCacheContext)
            {
                SR_LOG_WARN(K_DISPATCH_EXECUTOR_LOG_CATEGORY,
                            "task {}: device {} does not share the context of device {}, where the "
                            "worker's buffers live; reported as kernel failed",
                            task_id, dispatch.device_index, options_.cache_device);
                settler_.fail(job.from, task_id, job.generation,
                              Wire::TaskResultStatus::KernelFailed);
                return;
            }

            PinGuard pins(cache_);
            std::vector<BufferView> outputs;
            std::vector<BufferView> inputs;
            pins.reserve(dispatch.outputs.size() + dispatch.inputs.size());
            outputs.reserve(dispatch.outputs.size());
            inputs.reserve(dispatch.inputs.size());

            for (const BufferHandle& handle : dispatch.outputs)
            {
                BufferView view;
                if (cache_.pin_output(handle, view) != CacheStatus::Ready)
                {
                    log_no_room(task_id, handle, "output allocation failed");
                    settler_.fail(job.from, task_id, job.generation,
                                  Wire::TaskResultStatus::OutputAllocationFailed);
                    return;
                }
                pins.add(handle.id);
                outputs.push_back(view);
            }

            for (const BufferHandle& handle : dispatch.inputs)
            {
                BufferView view;
                const CacheStatus status = acquire_input(job, handle, view);
                if (status != CacheStatus::Ready)
                {
                    if (status == CacheStatus::AllocationFailed)
                        log_no_room(task_id, handle, "input unavailable");
                    else
                        SR_LOG_WARN(K_DISPATCH_EXECUTOR_LOG_CATEGORY, "task {}: input buffer {} version {} did not arrive from "
                                    "peer {}: {}; reported as input unavailable",
                                    task_id, handle.id.value, handle.version, job.from.value,
                                    cache_status_name(status));
                    settler_.fail(job.from, task_id, job.generation,
                                  Wire::TaskResultStatus::InputUnavailable);
                    return;
                }
                pins.add(handle.id);
                inputs.push_back(view);
            }

            DispatchEntry& entry = batch.add(std::move(job), *kernel, std::move(inputs),
                                        std::move(outputs));
            entry.pins = pins.hand_on();
            recorder_.record(entry);
        }

        void DispatchExecutor::run_batch(Batch& batch)
        {
            std::string run_failure;
            try
            {
                run_failure = recorder_.run();
            }
            catch (const std::exception& error)
            {
                run_failure = error.what();
            }
            catch (...)
            {
                run_failure = "unknown exception";
            }

            for (DispatchEntry& entry : batch.entries())
            {
                try
                {
                    settler_.settle(entry, run_failure);
                }
                catch (const std::exception& error)
                {
                    settler_.abandon(entry.job, error.what());
                }
                catch (...)
                {
                    settler_.abandon(entry.job, "unknown exception");
                }
            }
        }

    } // namespace Distributed
} // namespace SushiRuntime
