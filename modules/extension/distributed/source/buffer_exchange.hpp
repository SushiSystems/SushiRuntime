/**************************************************************************/
/* buffer_exchange.hpp                                                    */
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
 * @file buffer_exchange.hpp
 * @brief Declares BufferExchange, which serves buffer pulls and applies output chunks.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/buffer_handle.hpp>
#include <SushiRuntime/distributed/transport.hpp>

#include "buffer_registry.hpp"
#include "wire_messages.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        class OutputTransferTable;

        /**
         * @brief Names what apply_output_chunk() did with a chunk.
         */
        enum class ChunkStatus : std::uint8_t
        {
            Applied,   /**< The bytes are in the buffer; more chunks are expected. */
            Completed, /**< The bytes are in the buffer and the transfer is whole. */
            Stale,     /**< The registry is not at the chunk's version; nothing was written. */
            Gone,      /**< The buffer is not shared any more; nothing was written. */
            Gap,       /**< The offset is neither 0 nor the next expected; transfer discarded. */
            Malformed, /**< Wrong purpose, a size that differs from the buffer's, or overflow. */
        };

        /**
         * @brief Holds the exchange's counters since it was built.
         */
        struct BufferExchangeStatistics
        {
            std::uint64_t chunks_sent = 0;       /**< Chunks its pull sources produced. */
            std::uint64_t chunks_received = 0;   /**< Output chunks written into a buffer. */
            std::uint64_t transfers_aborted = 0; /**< Pulls ended by BUFFER_ABORT. */
            std::uint64_t stale_writebacks = 0;  /**< Output chunks refused as Stale. */
        };

        /**
         * @brief Moves buffer bytes between the registry and frames, one chunk at a time.
         *
         * Every transfer ends with a chunk that carries the last-chunk flag; a buffer of 0
         * bytes is one such chunk with no data. The exchange owns one mutex, over the progress
         * of incoming transfers, and does not hold it while it calls the registry. It calls no
         * transport. It outlives the sources it made, which read its registry and counters.
         */
        class SUSHIRUNTIME_API BufferExchange final
        {
            public:
                /**
                 * @brief Builds an exchange over a registry.
                 * @param registry Outlives the exchange and every source it made.
                 * @param chunk_bytes Data bytes per chunk, within the limits of cluster.hpp.
                 */
                BufferExchange(BufferRegistry& registry, std::uint32_t chunk_bytes);

                /** @brief Destroys the exchange. */
                ~BufferExchange();

                BufferExchange(const BufferExchange&) = delete;
                BufferExchange& operator=(const BufferExchange&) = delete;
                BufferExchange(BufferExchange&&) = delete;
                BufferExchange& operator=(BufferExchange&&) = delete;

                /**
                 * @brief Makes the source that answers one BUFFER_REQUEST, for send_stream().
                 *
                 * Each next() writes the 37-byte chunk prefix, then lets BufferRegistry::read()
                 * copy one chunk straight behind it. When read() reports Stale or Gone the
                 * source's final frame is BUFFER_ABORT with that reason.
                 * @return Never null.
                 */
                [[nodiscard]] std::unique_ptr<FrameSource> serve_pull(GlobalBufferID id,
                                                                      std::uint32_t version);

                /**
                 * @brief Writes one task-output chunk in place through BufferRegistry::write().
                 *
                 * Accepts offset 0 or the next expected offset of that task and buffer; a gap
                 * or a repeat discards the transfer's progress.
                 * @pre PendingTasks::expects_output() returned true for this chunk and peer.
                 * @param last_chunk The frame's last-chunk flag.
                 */
                [[nodiscard]] ChunkStatus apply_output_chunk(PeerID from,
                                                             const Wire::BufferChunkMessage& chunk,
                                                             bool last_chunk);

                /**
                 * @brief Returns whether every output of a task arrived whole, then forgets the
                 *        task's progress; called once, for its TASK_RESULT.
                 * @param outputs The handles the task was dispatched with.
                 */
                [[nodiscard]] bool finish_task(std::uint64_t task_id,
                                               SushiRuntime::span<const BufferHandle> outputs);

                /** @brief Forgets the progress of a task that was taken back. */
                void forget_task(std::uint64_t task_id) noexcept;

                /** @brief Returns the counters; reads atomics and takes no lock. */
                [[nodiscard]] BufferExchangeStatistics statistics() const noexcept;

            private:
                /// @brief Guards the progress of incoming transfers; free during a registry call.
                mutable std::mutex mutex_;

                /// @brief Source and destination of every copy.
                BufferRegistry& registry_;

                /// @brief Data bytes per chunk.
                std::uint32_t chunk_bytes_;

                /// @brief Chunks the pull sources produced.
                std::atomic<std::uint64_t> chunks_sent_{0};

                /// @brief Output chunks written into a buffer.
                std::atomic<std::uint64_t> chunks_received_{0};

                /// @brief Pulls ended by BUFFER_ABORT.
                std::atomic<std::uint64_t> transfers_aborted_{0};

                /// @brief Output chunks refused as Stale.
                std::atomic<std::uint64_t> stale_writebacks_{0};

                /// @brief Next expected offset of every incoming transfer, by task and buffer.
                std::unique_ptr<OutputTransferTable> transfers_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
