/**************************************************************************/
/* buffer_exchange.cpp                                                    */
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
 * @file buffer_exchange.cpp
 * @brief Streams a buffer to a worker chunk by chunk and applies the chunks a worker returns.
 * @author Mustafa Garip
 * @see modules/extension/buffer_exchange/README.md
 */

#include <SushiRuntime/buffer_exchange/buffer_exchange.hpp>

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <vector>

#include <SushiRuntime/wire/wire_frame.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Holds the next expected offset of every incoming transfer, by task and buffer.
         *
         * It has no lock of its own; BufferExchange calls it under its mutex. A task's
         * transfers share one vector, so a chunk after the task's first allocates nothing.
         */
        class OutputTransferTable final
        {
            public:
                /** @brief Names where one chunk lands and who sent it. */
                struct Step
                {
                    std::uint64_t task_id = 0;    /**< The task the bytes belong to. */
                    GlobalBufferID id;            /**< The buffer the bytes go into. */
                    PeerID from;                  /**< The connection the chunk arrived on. */
                    std::uint64_t size_bytes = 0; /**< The whole size the chunk declares. */
                    std::uint64_t offset = 0;     /**< The position of the chunk's first byte. */
                    std::uint64_t end = 0;        /**< The position after the chunk's last byte. */
                    bool last = false;            /**< Whether the chunk closes the transfer. */
                };

                /**
                 * @brief Moves a transfer forward by one chunk: offset 0 starts it over, the
                 *        next expected offset continues it, anything else discards it.
                 * @return Applied or Completed when the chunk is in order; Gap, or Malformed
                 *         for a size that differs from the transfer's, with the transfer gone.
                 */
                [[nodiscard]] ChunkStatus advance(const Step& step)
                {
                    if (step.offset == 0)
                    {
                        std::vector<Transfer>& transfers = by_task_[step.task_id];
                        auto transfer = find(transfers, step.id);
                        if (transfer == transfers.end())
                            transfer = transfers.insert(transfers.end(), Transfer{});

                        *transfer =
                            Transfer{step.id, step.from, step.size_bytes, step.end, step.last};
                        return step.last ? ChunkStatus::Completed : ChunkStatus::Applied;
                    }

                    const auto task = by_task_.find(step.task_id);
                    if (task == by_task_.end())
                        return ChunkStatus::Gap;

                    const auto transfer = find(task->second, step.id);
                    if (transfer == task->second.end())
                        return ChunkStatus::Gap;

                    if (transfer->size_bytes != step.size_bytes)
                    {
                        erase(task, transfer);
                        return ChunkStatus::Malformed;
                    }
                    if (transfer->from != step.from || transfer->next_offset != step.offset)
                    {
                        erase(task, transfer);
                        return ChunkStatus::Gap;
                    }

                    transfer->next_offset = step.end;
                    transfer->whole = step.last;
                    return step.last ? ChunkStatus::Completed : ChunkStatus::Applied;
                }

                /** @brief Forgets one transfer, if it is known. */
                void discard(std::uint64_t task_id, GlobalBufferID id) noexcept
                {
                    const auto task = by_task_.find(task_id);
                    if (task == by_task_.end())
                        return;

                    const auto transfer = find(task->second, id);
                    if (transfer != task->second.end())
                        erase(task, transfer);
                }

                /**
                 * @brief Forgets a task and returns whether each of its outputs arrived whole,
                 *        at the size the output was dispatched with.
                 */
                [[nodiscard]] bool
                take_task(std::uint64_t task_id,
                          SushiRuntime::span<const BufferHandle> outputs) noexcept
                {
                    const auto task = by_task_.find(task_id);
                    if (task == by_task_.end())
                        return outputs.empty();

                    bool whole = true;
                    for (const BufferHandle& output : outputs)
                    {
                        const auto transfer = find(task->second, output.id);
                        whole = whole && transfer != task->second.end() && transfer->whole &&
                                transfer->size_bytes == output.size_bytes;
                    }
                    by_task_.erase(task);
                    return whole;
                }

                /** @brief Forgets every transfer of a task. */
                void forget_task(std::uint64_t task_id) noexcept
                {
                    by_task_.erase(task_id);
                }

            private:
                /** @brief Holds how far one buffer of one task has come. */
                struct Transfer
                {
                    GlobalBufferID id;             /**< The buffer being written. */
                    PeerID from;                   /**< The connection that started it. */
                    std::uint64_t size_bytes = 0;  /**< The size its first chunk declared. */
                    std::uint64_t next_offset = 0; /**< The only offset accepted besides 0. */
                    bool whole = false;            /**< Whether its latest chunk was the last. */
                };

                /** @brief Names the transfers of one task. */
                using Transfers = std::vector<Transfer>;

                /** @brief Names the tasks that have a transfer. */
                using Tasks = std::unordered_map<std::uint64_t, Transfers>;

                /** @brief Returns the transfer of a buffer, or the end of the vector. */
                [[nodiscard]] static Transfers::iterator find(Transfers& transfers,
                                                              GlobalBufferID id) noexcept
                {
                    return std::find_if(transfers.begin(), transfers.end(),
                                        [id](const Transfer& transfer)
                                        {
                                            return transfer.id == id;
                                        });
                }

                /** @brief Removes one transfer, and its task when that was the task's last. */
                void erase(Tasks::iterator task, Transfers::iterator transfer) noexcept
                {
                    task->second.erase(transfer);
                    if (task->second.empty())
                        by_task_.erase(task);
                }

                /// @brief Transfers in progress, by the id of their task.
                Tasks by_task_;
        };

        namespace
        {
            /**
             * @brief Produces the frames that answer one BUFFER_REQUEST.
             *
             * It asks the registry for one chunk per call and keeps no address, so a buffer
             * released between two calls ends the stream with BUFFER_ABORT.
             */
            class PullSource final : public FrameSource
            {
                public:
                    /**
                     * @brief Builds the source of one buffer version.
                     * @param registry Outlives the source, as do the two counters.
                     * @param wanted The id and version asked for, with the buffer's size.
                     */
                    PullSource(const BufferRegistry& registry, const BufferHandle& wanted,
                               std::uint32_t chunk_bytes, std::atomic<std::uint64_t>& chunks_sent,
                               std::atomic<std::uint64_t>& transfers_aborted)
                        : registry_(registry), wanted_(wanted), chunk_bytes_(chunk_bytes),
                          chunks_sent_(chunks_sent), transfers_aborted_(transfers_aborted)
                    {
                        prefix_.reserve(Wire::BufferChunkMessage::K_PREFIX_BYTES);
                    }

                    /** @brief Writes the next chunk, or BUFFER_ABORT when the read is refused. */
                    [[nodiscard]] FrameSourceStatus next(OutboundFrame& frame) noexcept override
                    {
                        if (finished_)
                            return FrameSourceStatus::Finished;

                        const std::uint64_t remaining = wanted_.size_bytes - offset_;
                        const std::size_t length = static_cast<std::size_t>(
                            std::min<std::uint64_t>(remaining, chunk_bytes_));
                        const bool last = length == remaining;

                        Wire::BufferChunkMessage chunk;
                        chunk.purpose = Wire::ChunkPurpose::PullReply;
                        chunk.handle = wanted_;
                        chunk.offset = offset_;
                        prefix_.clear();
                        if (chunk.encode(prefix_) != Wire::CodecStatus::Ok)
                            return abort(frame, Wire::AbortReason::Gone);

                        // The payload keeps its size between chunks, so resize() fills nothing.
                        frame.type = MessageType::BufferChunk;
                        frame.flags = last ? Wire::K_FRAME_FLAG_LAST_CHUNK : std::uint8_t{0};
                        frame.payload.resize(prefix_.size() + length);
                        std::copy(prefix_.begin(), prefix_.end(), frame.payload.begin());

                        const BufferAccessStatus status = registry_.read(
                            wanted_.id, wanted_.version, offset_,
                            {frame.payload.data() + prefix_.size(), length});
                        if (status == BufferAccessStatus::Stale)
                            return abort(frame, Wire::AbortReason::Stale);
                        if (status != BufferAccessStatus::Ok)
                            return abort(frame, Wire::AbortReason::Gone);

                        offset_ += length;
                        chunks_sent_.fetch_add(1, std::memory_order_relaxed);
                        if (!last)
                            return FrameSourceStatus::Frame;

                        finished_ = true;
                        return FrameSourceStatus::FinalFrame;
                    }

                private:
                    /** @brief Ends the stream with a BUFFER_ABORT frame that carries the cause. */
                    [[nodiscard]] FrameSourceStatus abort(OutboundFrame& frame,
                                                          Wire::AbortReason reason) noexcept
                    {
                        finished_ = true;
                        transfers_aborted_.fetch_add(1, std::memory_order_relaxed);

                        Wire::BufferAbortMessage message;
                        message.id = wanted_.id;
                        message.version = wanted_.version;
                        message.reason = reason;

                        frame.type = MessageType::BufferAbort;
                        frame.flags = 0;
                        frame.payload.clear();
                        if (message.encode(frame.payload) != Wire::CodecStatus::Ok)
                            return FrameSourceStatus::Finished;

                        return FrameSourceStatus::FinalFrame;
                    }

                    /// @brief Copies each chunk; the source holds no address of the buffer.
                    const BufferRegistry& registry_;

                    /// @brief The id and version asked for, and the buffer's size.
                    BufferHandle wanted_;

                    /// @brief Data bytes per chunk.
                    std::uint32_t chunk_bytes_;

                    /// @brief The exchange's count of chunks produced.
                    std::atomic<std::uint64_t>& chunks_sent_;

                    /// @brief The exchange's count of pulls ended by BUFFER_ABORT.
                    std::atomic<std::uint64_t>& transfers_aborted_;

                    /// @brief The encoded fields in front of a chunk's data; capacity reused.
                    std::vector<std::byte> prefix_;

                    /// @brief The position of the next chunk inside the buffer.
                    std::uint64_t offset_ = 0;

                    /// @brief Whether the final frame has been produced.
                    bool finished_ = false;
            };

            /**
             * @brief Returns why an output chunk is refused before any byte moves, or nothing.
             *
             * A chunk at offset 0 is also compared with the size the registry holds; a later
             * chunk is compared with its transfer's size by OutputTransferTable::advance().
             */
            [[nodiscard]] std::optional<ChunkStatus>
            refusal_of(const BufferRegistry& registry, const Wire::BufferChunkMessage& chunk,
                       bool last_chunk)
            {
                if (chunk.purpose != Wire::ChunkPurpose::TaskOutput)
                    return ChunkStatus::Malformed;

                const std::uint64_t size_bytes = chunk.handle.size_bytes;
                if (chunk.offset > size_bytes || chunk.data.size() > size_bytes - chunk.offset)
                    return ChunkStatus::Malformed;

                if (last_chunk && chunk.offset + chunk.data.size() != size_bytes)
                    return ChunkStatus::Malformed;

                if (chunk.offset != 0)
                    return std::nullopt;

                const std::optional<BufferHandle> current = registry.lookup_by_id(chunk.handle.id);
                if (!current)
                    return ChunkStatus::Gone;

                if (current->size_bytes != size_bytes)
                    return ChunkStatus::Malformed;

                return std::nullopt;
            }

            /** @brief Returns the chunk status that reports a write the registry refused. */
            [[nodiscard]] ChunkStatus refused_write(BufferAccessStatus status) noexcept
            {
                if (status == BufferAccessStatus::Stale)
                    return ChunkStatus::Stale;
                if (status == BufferAccessStatus::Gone)
                    return ChunkStatus::Gone;
                return ChunkStatus::Malformed;
            }
        } // namespace

        BufferExchange::BufferExchange(BufferRegistry& registry, std::uint32_t chunk_bytes)
            : registry_(registry), chunk_bytes_(chunk_bytes),
              transfers_(std::make_unique<OutputTransferTable>())
        {
        }

        BufferExchange::~BufferExchange() = default;

        std::unique_ptr<FrameSource> BufferExchange::serve_pull(GlobalBufferID id,
                                                                std::uint32_t version)
        {
            const std::optional<BufferHandle> current = registry_.lookup_by_id(id);

            // A buffer that is gone has size 0: the first read() reports it and the stream aborts.
            BufferHandle wanted;
            wanted.id = id;
            wanted.version = version;
            wanted.size_bytes = current ? current->size_bytes : 0;

            return std::make_unique<PullSource>(registry_, wanted, chunk_bytes_, chunks_sent_,
                                                transfers_aborted_);
        }

        ChunkStatus BufferExchange::apply_output_chunk(PeerID from,
                                                       const Wire::BufferChunkMessage& chunk,
                                                       bool last_chunk)
        {
            const std::uint64_t task_id = chunk.task_id;
            const GlobalBufferID id = chunk.handle.id;

            std::optional<ChunkStatus> refusal = refusal_of(registry_, chunk, last_chunk);
            if (!refusal)
            {
                OutputTransferTable::Step step;
                step.task_id = task_id;
                step.id = id;
                step.from = from;
                step.size_bytes = chunk.handle.size_bytes;
                step.offset = chunk.offset;
                step.end = chunk.offset + chunk.data.size();
                step.last = last_chunk;

                ChunkStatus progress = ChunkStatus::Gap;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    progress = transfers_->advance(step);
                }
                if (progress != ChunkStatus::Applied && progress != ChunkStatus::Completed)
                    return progress;

                const BufferAccessStatus written =
                    registry_.write(id, chunk.handle.version, chunk.offset, chunk.data);
                if (written == BufferAccessStatus::Ok)
                {
                    chunks_received_.fetch_add(1, std::memory_order_relaxed);
                    return progress;
                }

                refusal = refused_write(written);
                if (*refusal == ChunkStatus::Stale)
                    stale_writebacks_.fetch_add(1, std::memory_order_relaxed);
            }

            std::lock_guard<std::mutex> lock(mutex_);
            transfers_->discard(task_id, id);
            return *refusal;
        }

        bool BufferExchange::finish_task(std::uint64_t task_id,
                                         SushiRuntime::span<const BufferHandle> outputs)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return transfers_->take_task(task_id, outputs);
        }

        void BufferExchange::forget_task(std::uint64_t task_id) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            transfers_->forget_task(task_id);
        }

        BufferExchangeStatistics BufferExchange::statistics() const noexcept
        {
            BufferExchangeStatistics statistics;
            statistics.chunks_sent = chunks_sent_.load(std::memory_order_relaxed);
            statistics.chunks_received = chunks_received_.load(std::memory_order_relaxed);
            statistics.transfers_aborted = transfers_aborted_.load(std::memory_order_relaxed);
            statistics.stale_writebacks = stale_writebacks_.load(std::memory_order_relaxed);
            return statistics;
        }

    } // namespace Distributed
} // namespace SushiRuntime
