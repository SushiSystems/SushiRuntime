/**************************************************************************/
/* dispatch_settler.cpp                                                   */
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
 * @file dispatch_settler.cpp
 * @brief Implements DispatchSettler and the output stream of a finished task.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include "dispatch_settler.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <utility>
#include <vector>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/distributed/distributed_log_category.hpp>
#include <SushiRuntime/frame_transport/frame_sending.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /**
             * @brief Produces the output chunks of one finished task, then its TASK_RESULT.
             *
             * It sends the copy of each output its task took when its kernel ended, so a later
             * task that writes the same buffer cannot change what this one sends.
             */
            class OutputStream final : public FrameSource
            {
                public:
                    /**
                     * @brief Builds the stream of a task whose kernel succeeded.
                     * @param outputs One copy per handle, of its size_bytes.
                     */
                    OutputStream(std::shared_ptr<HeldTasks> held, std::uint64_t task_id,
                                 std::uint64_t generation, std::uint32_t chunk_bytes,
                                 std::vector<BufferHandle> handles,
                                 std::vector<std::vector<std::byte>> outputs) noexcept
                        : held_(std::move(held)), task_id_(task_id), generation_(generation),
                          chunk_bytes_(chunk_bytes), handles_(std::move(handles)),
                          outputs_(std::move(outputs))
                    {

                    }

                    /** @brief Lets the task go if its result never left. */
                    ~OutputStream() override
                    {
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
                        SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY,
                                     "task {}: the output stream ended before its result: {}; the "
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
                                outputs_[output_].data() + offset_,
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

                    /// @brief The bytes of each output, copied when its kernel ended.
                    std::vector<std::vector<std::byte>> outputs_;

                    /// @brief The output being sent.
                    std::size_t output_ = 0;

                    /// @brief The next byte of that output.
                    std::uint64_t offset_ = 0;

                    /// @brief Whether next() has nothing more to give.
                    bool finished_ = false;

                    /// @brief Whether TASK_RESULT was written into a frame.
                    bool result_written_ = false;
            };
        } // namespace

        DispatchSettler::DispatchSettler(WorkerBufferCache& cache, Transport& transport,
                                         std::shared_ptr<HeldTasks> held) noexcept
            : cache_(cache), transport_(transport), held_(std::move(held))
        {

        }

        void DispatchSettler::settle(DispatchEntry& entry, const std::string& run_failure)
        {
            const Wire::TaskDispatchMessage& dispatch = entry.job.dispatch;
            const std::uint64_t task_id = dispatch.task_id;

            if (!entry.ran)
            {
                // A kernel that threw may have written part of an output.
                for (const BufferHandle& handle : dispatch.outputs)
                    cache_.release(handle.id);
                const std::string& cause = !entry.failure.empty() ? entry.failure : run_failure;
                SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY,
                             "task {}: the kernel of operation {} did not finish: {}; reported "
                             "as kernel failed",
                             task_id, dispatch.operation.value,
                             cause.empty() ? std::string("the run did not start the task") : cause);
                fail(entry.job.from, task_id, entry.job.generation,
                     Wire::TaskResultStatus::KernelFailed);
                return;
            }

            for (const BufferHandle& handle : dispatch.outputs)
                cache_.commit_output(handle);
            tasks_run_.fetch_add(1, std::memory_order_relaxed);

            std::unique_ptr<FrameSource> stream = std::make_unique<OutputStream>(
                held_, task_id, entry.job.generation, entry.job.chunk_bytes, dispatch.outputs,
                std::move(entry.copies));

            const SendStatus sent = transport_.send_stream(entry.job.from, std::move(stream));
            if (sent != SendStatus::Queued)
                SR_LOG_DEBUG(K_DISTRIBUTED_LOG_CATEGORY,
                             "task {}: the output stream to peer {} was not queued (status {})",
                             task_id, entry.job.from.value, static_cast<int>(sent));
        }

        WorkerStatistics DispatchSettler::statistics() const noexcept
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

        void DispatchSettler::count_failure(Wire::TaskResultStatus status) noexcept
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

        void DispatchSettler::fail(PeerID from, std::uint64_t task_id, std::uint64_t generation,
                                    Wire::TaskResultStatus status)
        {
            count_failure(status);

            Wire::TaskResultMessage result;
            result.task_id = task_id;
            result.status = status;
            const SendStatus sent = send_message(transport_, from, result);
            held_->let_go(task_id, generation);
            if (sent != SendStatus::Queued)
                SR_LOG_DEBUG(K_DISTRIBUTED_LOG_CATEGORY,
                             "task {}: TASK_RESULT to peer {} was not queued (status {})", task_id,
                             from.value, static_cast<int>(sent));
        }

        void DispatchSettler::abandon(const DispatchJob& job, const char* cause) noexcept
        {
            try
            {
                SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY,
                             "task {}: the worker failed outside the kernel: {}; reported as "
                             "kernel failed",
                             job.dispatch.task_id, cause);
                fail(job.from, job.dispatch.task_id, job.generation,
                     Wire::TaskResultStatus::KernelFailed);
            }
            catch (...)
            {
                held_->let_go(job.dispatch.task_id, job.generation);
                SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY,
                             "the worker could not report a failed task; the master takes it "
                             "back as a lost result");
            }
        }

    } // namespace Distributed
} // namespace SushiRuntime
