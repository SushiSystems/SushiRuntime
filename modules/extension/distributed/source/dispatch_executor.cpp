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
 * @file dispatch_executor.cpp
 * @brief Implements DispatchExecutor: the job queue, the exec thread and the output stream.
 * @author Mustafa Garip
 */

#include "dispatch_executor.hpp"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/logging/logger.hpp>

#include "frame_sending.hpp"
#include "wire_frame.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds the chunk size used when a dispatch arrives with none. */
            constexpr std::uint32_t K_DEFAULT_CHUNK_BYTES = 256u * 1024u;

            /** @brief Holds the most data bytes one BUFFER_CHUNK frame can carry. */
            constexpr std::uint32_t K_MAX_CHUNK_BYTES =
                Wire::K_MAX_FRAME_PAYLOAD -
                static_cast<std::uint32_t>(Wire::BufferChunkMessage::K_PREFIX_BYTES);

            /** @brief Returns a chunk size a frame can carry; 0 takes the default. */
            std::uint32_t usable_chunk_bytes(std::uint32_t chunk_bytes) noexcept
            {
                if (chunk_bytes == 0)
                    return K_DEFAULT_CHUNK_BYTES;
                return std::min(chunk_bytes, K_MAX_CHUNK_BYTES);
            }

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

                    /** @brief Stops guarding: another owner unpins from here on. */
                    void hand_on() noexcept
                    {
                        ids_.clear();
                    }

                private:
                    /// @brief The cache the pins belong to.
                    WorkerBufferCache& cache_;

                    /// @brief One id per pin.
                    std::vector<GlobalBufferID> ids_;
            };
        } // namespace

        /**
         * @brief Produces the output chunks of one finished task, then its TASK_RESULT.
         *
         * It owns one cache pin per output and drops them in its destructor, on whichever
         * transport thread destroys it.
         */
        class DispatchExecutor::OutputStream final : public FrameSource
        {
            public:
                /**
                 * @brief Builds the stream of a task whose kernel succeeded.
                 * @param cache Outlives the stream.
                 * @param views One per handle, each pinned once for this stream.
                 */
                OutputStream(WorkerBufferCache& cache, std::shared_ptr<HeldTasks> held,
                             std::uint64_t task_id, std::uint64_t generation,
                             std::uint32_t chunk_bytes, std::vector<BufferHandle> handles,
                             std::vector<BufferView> views) noexcept
                    : cache_(cache), held_(std::move(held)), task_id_(task_id),
                      generation_(generation), chunk_bytes_(chunk_bytes),
                      handles_(std::move(handles)), views_(std::move(views))
                {

                }

                /** @brief Unpins the outputs and lets the task go if its result never left. */
                ~OutputStream() override
                {
                    for (const BufferHandle& handle : handles_)
                        cache_.unpin(handle.id);
                    if (!result_written_)
                        held_->let_go(task_id_, generation_);
                }

                OutputStream(const OutputStream&) = delete;
                OutputStream& operator=(const OutputStream&) = delete;
                OutputStream(OutputStream&&) = delete;
                OutputStream& operator=(OutputStream&&) = delete;

                [[nodiscard]] FrameSourceStatus next(OutboundFrame& frame) noexcept override
                {
                    if (finished_)
                        return FrameSourceStatus::Finished;

                    const char* failure = nullptr;
                    try
                    {
                        frame.payload.clear();
                        const Wire::CodecStatus status = output_ < handles_.size()
                                                             ? write_chunk(frame)
                                                             : write_result(frame);
                        if (status == Wire::CodecStatus::Ok)
                            return finished_ ? FrameSourceStatus::FinalFrame
                                             : FrameSourceStatus::Frame;
                        failure = "a frame did not encode";
                    }
                    catch (const std::exception&)
                    {
                        failure = "an exception while encoding a frame";
                    }
                    catch (...)
                    {
                        failure = "an unknown exception while encoding a frame";
                    }

                    finished_ = true;
                    frame.payload.clear();
                    SR_LOG_ERROR("task {}: the output stream ended before its result: {}; the "
                                 "master takes the task back as a lost result",
                                 task_id_, failure);
                    return FrameSourceStatus::Finished;
                }

            private:
                /** @brief Writes the next chunk of the current output and moves past it. */
                [[nodiscard]] Wire::CodecStatus write_chunk(OutboundFrame& frame)
                {
                    const BufferHandle& handle = handles_[output_];
                    const std::uint64_t length =
                        std::min<std::uint64_t>(handle.size_bytes - offset_, chunk_bytes_);

                    Wire::BufferChunkMessage chunk;
                    chunk.purpose = Wire::ChunkPurpose::TaskOutput;
                    chunk.task_id = task_id_;
                    chunk.handle = handle;
                    chunk.offset = offset_;
                    if (length > 0)
                        chunk.data = SushiRuntime::span<const std::byte>(
                            static_cast<const std::byte*>(views_[output_].data) + offset_,
                            static_cast<std::size_t>(length));

                    frame.type = MessageType::BufferChunk;
                    frame.flags = 0;
                    offset_ += length;
                    if (offset_ == handle.size_bytes)
                    {
                        frame.flags = Wire::K_FRAME_FLAG_LAST_CHUNK;
                        ++output_;
                        offset_ = 0;
                    }
                    return chunk.encode(frame.payload);
                }

                /** @brief Writes TASK_RESULT and lets the task go. */
                [[nodiscard]] Wire::CodecStatus write_result(OutboundFrame& frame)
                {
                    Wire::TaskResultMessage result;
                    result.task_id = task_id_;
                    result.status = Wire::TaskResultStatus::Succeeded;
                    result.outputs = handles_;

                    frame.type = MessageType::TaskResult;
                    frame.flags = 0;
                    const Wire::CodecStatus status = result.encode(frame.payload);
                    if (status != Wire::CodecStatus::Ok)
                        return status;

                    finished_ = true;
                    result_written_ = true;
                    held_->let_go(task_id_, generation_);
                    return status;
                }

                /// @brief The cache whose pins the stream holds.
                WorkerBufferCache& cache_;

                /// @brief Told when the result leaves.
                std::shared_ptr<HeldTasks> held_;

                /// @brief The task the outputs belong to.
                std::uint64_t task_id_;

                /// @brief The generation the task was held under.
                std::uint64_t generation_;

                /// @brief Data bytes per chunk.
                std::uint32_t chunk_bytes_;

                /// @brief The outputs, at the versions the dispatch named.
                std::vector<BufferHandle> handles_;

                /// @brief The memory of each output.
                std::vector<BufferView> views_;

                /// @brief The output being sent.
                std::size_t output_ = 0;

                /// @brief The next byte of that output.
                std::uint64_t offset_ = 0;

                /// @brief Whether next() has nothing more to give.
                bool finished_ = false;

                /// @brief Whether TASK_RESULT was written into a frame.
                bool result_written_ = false;
        };

        std::uint64_t DispatchExecutor::HeldTasks::hold(std::uint64_t task_id)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            held_.insert(task_id);
            return generation_;
        }

        void DispatchExecutor::HeldTasks::let_go(std::uint64_t task_id,
                                                 std::uint64_t generation) noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (generation != generation_)
                return;

            held_.erase(task_id);
            idle_value_ = std::max(idle_value_, task_id + 1);
        }

        void DispatchExecutor::HeldTasks::reset() noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            held_.clear();
            idle_value_ = 0;
            ++generation_;
        }

        std::uint64_t DispatchExecutor::HeldTasks::oldest() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return held_.empty() ? idle_value_ : *held_.begin();
        }

        DispatchExecutor::DispatchExecutor(sushi_ptr<KernelRegistry> kernels,
                                           WorkerBufferCache& cache, ITransport& transport,
                                           sycl::queue& queue,
                                           const DispatchExecutorOptions& options)
            : kernels_(std::move(kernels)), cache_(cache), transport_(transport), queue_(queue),
              options_(options), held_(std::make_shared<HeldTasks>())
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

        EnqueueStatus DispatchExecutor::enqueue(PeerId from, Wire::TaskDispatchMessage dispatch,
                                                std::uint32_t chunk_bytes)
        {
            const std::uint64_t task_id = dispatch.task_id;
            bool stopping = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                stopping = stopping_;
                if (!stopping)
                {
                    if (jobs_.size() >= options_.max_jobs)
                        return EnqueueStatus::QueueFull;

                    Job job;
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
                fail(from, task_id, held_->hold(task_id), Wire::TaskResultStatus::ShuttingDown);
                return EnqueueStatus::Stopping;
            }

            ready_.notify_one();

            Wire::TaskAcceptedMessage accepted;
            accepted.task_id = task_id;
            const SendStatus sent = send_message(transport_, from, accepted);
            if (sent != SendStatus::Queued)
                SR_LOG_DEBUG("task {}: TASK_ACCEPTED to peer {} was not queued (status {})",
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
            std::deque<Job> queued;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
                queued.swap(jobs_);
                queue_depth_.store(0, std::memory_order_relaxed);
            }
            ready_.notify_all();

            try
            {
                for (const Job& job : queued)
                    fail(job.from, job.dispatch.task_id, job.generation,
                         Wire::TaskResultStatus::ShuttingDown);
                if (!queued.empty())
                    SR_LOG_INFO("worker stopping: {} queued tasks reported as shutting down",
                                queued.size());
            }
            catch (const std::exception& error)
            {
                SR_LOG_WARN("worker stopping: not every queued task was reported: {}; the "
                            "master takes the rest back when the connection closes",
                            error.what());
            }
            catch (...)
            {
                SR_LOG_WARN("worker stopping: not every queued task was reported: unknown "
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
            WorkerStatistics statistics;
            statistics.tasks_run = tasks_run_.load(std::memory_order_relaxed);
            statistics.tasks_failed_kernel = failed_kernel_.load(std::memory_order_relaxed);
            statistics.tasks_failed_output_allocation =
                failed_output_allocation_.load(std::memory_order_relaxed);
            statistics.tasks_failed_input_unavailable =
                failed_input_unavailable_.load(std::memory_order_relaxed);
            statistics.tasks_failed_unknown_operation =
                failed_unknown_operation_.load(std::memory_order_relaxed);
            statistics.tasks_failed_shutting_down =
                failed_shutting_down_.load(std::memory_order_relaxed);
            return statistics;
        }

        void DispatchExecutor::run() noexcept
        {
            for (;;)
            {
                Job job;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock, [this]
                                {
                                    return stopping_ || !jobs_.empty();
                                });
                    if (stopping_)
                        return;

                    job = std::move(jobs_.front());
                    jobs_.pop_front();
                    queue_depth_.fetch_sub(1, std::memory_order_relaxed);
                }

                try
                {
                    execute(job);
                }
                catch (const std::exception& error)
                {
                    abandon(job, error.what());
                }
                catch (...)
                {
                    abandon(job, "unknown exception");
                }
            }
        }

        void DispatchExecutor::count_failure(Wire::TaskResultStatus status) noexcept
        {
            switch (status)
            {
                case Wire::TaskResultStatus::Succeeded:
                    break;
                case Wire::TaskResultStatus::KernelFailed:
                    failed_kernel_.fetch_add(1, std::memory_order_relaxed);
                    break;
                case Wire::TaskResultStatus::OutputAllocationFailed:
                    failed_output_allocation_.fetch_add(1, std::memory_order_relaxed);
                    break;
                case Wire::TaskResultStatus::InputUnavailable:
                    failed_input_unavailable_.fetch_add(1, std::memory_order_relaxed);
                    break;
                case Wire::TaskResultStatus::UnknownOperation:
                    failed_unknown_operation_.fetch_add(1, std::memory_order_relaxed);
                    break;
                case Wire::TaskResultStatus::ShuttingDown:
                    failed_shutting_down_.fetch_add(1, std::memory_order_relaxed);
                    break;
            }
        }

        void DispatchExecutor::fail(PeerId from, std::uint64_t task_id, std::uint64_t generation,
                                    Wire::TaskResultStatus status)
        {
            count_failure(status);

            Wire::TaskResultMessage result;
            result.task_id = task_id;
            result.status = status;
            const SendStatus sent = send_message(transport_, from, result);
            held_->let_go(task_id, generation);
            if (sent != SendStatus::Queued)
                SR_LOG_DEBUG("task {}: TASK_RESULT to peer {} was not queued (status {})",
                             task_id, from.value, static_cast<int>(sent));
        }

        void DispatchExecutor::abandon(const Job& job, const char* cause) noexcept
        {
            try
            {
                SR_LOG_ERROR("task {}: the worker failed outside the kernel: {}; reported as "
                             "kernel failed",
                             job.dispatch.task_id, cause);
                fail(job.from, job.dispatch.task_id, job.generation,
                     Wire::TaskResultStatus::KernelFailed);
            }
            catch (...)
            {
                held_->let_go(job.dispatch.task_id, job.generation);
                SR_LOG_ERROR("the worker could not report a failed task; the master takes it "
                             "back as a lost result");
            }
        }

        CacheStatus DispatchExecutor::acquire_input(const Job& job, const BufferHandle& handle,
                                                    BufferView& view)
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
            SR_LOG_ERROR("task {}: cannot allocate {} bytes for buffer {} (budget {}, in use "
                         "{}); reported as {}",
                         task_id, handle.size_bytes, handle.id.value, cache_.budget(),
                         cache_.bytes(), consequence);
        }

        void DispatchExecutor::execute(const Job& job)
        {
            const Wire::TaskDispatchMessage& dispatch = job.dispatch;
            const std::uint64_t task_id = dispatch.task_id;

            const KernelRegistry::Kernel* kernel = kernels_->find(dispatch.metadata.op_id);
            if (kernel == nullptr)
            {
                SR_LOG_WARN("task {}: this worker has no kernel for operation {}; reported as "
                            "unknown operation",
                            task_id, dispatch.metadata.op_id.value);
                fail(job.from, task_id, job.generation,
                     Wire::TaskResultStatus::UnknownOperation);
                return;
            }

            PinGuard output_pins(cache_);
            PinGuard input_pins(cache_);
            std::vector<BufferView> outputs;
            std::vector<BufferView> inputs;
            output_pins.reserve(dispatch.outputs.size());
            input_pins.reserve(dispatch.inputs.size());
            outputs.reserve(dispatch.outputs.size());
            inputs.reserve(dispatch.inputs.size());

            for (const BufferHandle& handle : dispatch.outputs)
            {
                BufferView view;
                if (cache_.pin_output(handle, view) != CacheStatus::Ready)
                {
                    log_no_room(task_id, handle, "output allocation failed");
                    fail(job.from, task_id, job.generation,
                         Wire::TaskResultStatus::OutputAllocationFailed);
                    return;
                }
                output_pins.add(handle.id);
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
                        SR_LOG_WARN("task {}: input buffer {} version {} did not arrive from "
                                    "peer {}: {}; reported as input unavailable",
                                    task_id, handle.id.value, handle.version, job.from.value,
                                    cache_status_name(status));
                    fail(job.from, task_id, job.generation,
                         Wire::TaskResultStatus::InputUnavailable);
                    return;
                }
                input_pins.add(handle.id);
                inputs.push_back(view);
            }

            std::string failure;
            bool ran = false;
            try
            {
                sycl::event event = (*kernel)(
                    queue_, SushiRuntime::span<const BufferView>(inputs.data(), inputs.size()),
                    SushiRuntime::span<const BufferView>(outputs.data(), outputs.size()),
                    dispatch.metadata);
                event.wait_and_throw();
                ran = true;
            }
            catch (const std::exception& error)
            {
                failure = error.what();
            }
            catch (...)
            {
                failure = "unknown exception";
            }

            if (!ran)
            {
                // A kernel that threw may have written part of an output.
                for (const BufferHandle& handle : dispatch.outputs)
                    cache_.release(handle.id);
                SR_LOG_ERROR("task {}: the kernel of operation {} threw: {}; reported as "
                             "kernel failed",
                             task_id, dispatch.metadata.op_id.value, failure);
                fail(job.from, task_id, job.generation, Wire::TaskResultStatus::KernelFailed);
                return;
            }

            for (const BufferHandle& handle : dispatch.outputs)
                cache_.commit_output(handle);
            tasks_run_.fetch_add(1, std::memory_order_relaxed);

            std::unique_ptr<FrameSource> stream = std::make_unique<OutputStream>(
                cache_, held_, task_id, job.generation, job.chunk_bytes, dispatch.outputs,
                std::move(outputs));
            output_pins.hand_on();

            const SendStatus sent = transport_.send_stream(job.from, std::move(stream));
            if (sent != SendStatus::Queued)
                SR_LOG_DEBUG("task {}: the output stream to peer {} was not queued (status {})",
                             task_id, job.from.value, static_cast<int>(sent));
        }

    } // namespace Distributed
} // namespace SushiRuntime
