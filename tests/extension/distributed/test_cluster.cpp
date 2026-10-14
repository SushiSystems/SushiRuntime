/**************************************************************************/
/* test_cluster.cpp                                                       */
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
 * @file test_cluster.cpp
 * @brief Tests the coordinator side of a cluster: buffer exchange, dispatch, pending tasks and workers.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/buffer_handle.hpp>
#include <SushiRuntime/distributed/cluster_statistics.hpp>
#include <SushiRuntime/distributed/offload_policy.hpp>
#include <SushiRuntime/distributed/transport.hpp>
#include <SushiRuntime/distributed/message_type.hpp>

#include "buffer_exchange.hpp"
#include "buffer_registry.hpp"
#include "wire_frame.hpp"
#include "wire_messages.hpp"
#include "handshake.hpp"
#include "offload_dispatcher.hpp"
#include "pending_tasks.hpp"
#include "worker_directory.hpp"
#include "sha256.hpp"

using namespace SushiRuntime;

/** @brief Tests BufferExchange: chunked pulls, fenced output chunks and aborted streams. */
namespace BufferExchangeTests
{
    using namespace SushiRuntime::Distributed;

    namespace
    {
        /** @brief Names a run of bytes. */
        using Bytes = std::vector<std::byte>;

        /** @brief Holds the data bytes per chunk of every exchange in this file: 4 KiB. */
        constexpr std::uint32_t K_CHUNK_BYTES = K_MINIMUM_CHUNK_BYTES;

        /** @brief Holds the task every output chunk in this file belongs to. */
        constexpr std::uint64_t K_TASK_ID = 41;

        /** @brief Holds the connection every output chunk in this file arrives on. */
        constexpr PeerID K_WORKER{3};

        /** @brief Holds the byte kept after a buffer's last byte to show a write past its end. */
        constexpr std::byte K_GUARD{0xEE};

        /** @brief Holds the most frames a test stream may produce before the test gives up. */
        constexpr std::size_t K_FRAME_CEILING = 64;

        /** @brief Holds the buffer sizes that sit on and around a chunk boundary. */
        const std::vector<std::size_t> K_BOUNDARY_SIZES = {
            0, 1, K_CHUNK_BYTES - 1, K_CHUNK_BYTES, K_CHUNK_BYTES + 1, 3 * K_CHUNK_BYTES + 7};

        /** @brief Holds one frame a source produced, copied out of the writer's frame. */
        struct SentFrame
        {
            FrameSourceStatus status = FrameSourceStatus::Finished; /**< What next() returned. */
            MessageType type = MessageType::Hello;                  /**< The frame's type. */
            std::uint8_t flags = 0;                                 /**< The frame's flag byte. */
            Bytes payload;                                          /**< The frame's payload. */
        };

        /** @brief Returns size bytes that differ by position and by seed, then one guard byte. */
        Bytes guarded_pattern(std::size_t size, unsigned seed)
        {
            Bytes bytes(size + 1, K_GUARD);
            for (std::size_t index = 0; index < size; ++index)
                bytes[index] = static_cast<std::byte>((index * 131u + seed) & 0xFFu);
            return bytes;
        }

        /** @brief Returns a read-only view of a byte vector. */
        SushiRuntime::span<const std::byte> view_of(const Bytes& bytes)
        {
            return {bytes.data(), bytes.size()};
        }

        /** @brief Pulls a source through one reused frame until its final frame. */
        std::vector<SentFrame> drain(FrameSource& source, OutboundFrame& frame)
        {
            std::vector<SentFrame> frames;
            while (frames.size() < K_FRAME_CEILING)
            {
                const FrameSourceStatus status = source.next(frame);
                if (status == FrameSourceStatus::Finished)
                    break;

                frames.push_back(SentFrame{status, frame.type, frame.flags, frame.payload});
                if (status == FrameSourceStatus::FinalFrame)
                    break;
            }
            return frames;
        }

        /** @brief Returns the output chunk that carries piece index of a payload for a buffer. */
        Wire::BufferChunkMessage output_chunk(const BufferHandle& handle, const Bytes& payload,
                                              std::size_t index)
        {
            const std::size_t size = static_cast<std::size_t>(handle.size_bytes);
            const std::size_t offset = std::min<std::size_t>(index * K_CHUNK_BYTES, size);

            Wire::BufferChunkMessage chunk;
            chunk.purpose = Wire::ChunkPurpose::TaskOutput;
            chunk.task_id = K_TASK_ID;
            chunk.handle = handle;
            chunk.offset = offset;
            chunk.data = {payload.data() + offset, std::min<std::size_t>(K_CHUNK_BYTES, size - offset)};
            return chunk;
        }

        /** @brief Applies piece index of a payload, flagged last when it ends the buffer. */
        ChunkStatus apply_piece(BufferExchange& exchange, const BufferHandle& handle,
                                const Bytes& payload, std::size_t index)
        {
            const Wire::BufferChunkMessage chunk = output_chunk(handle, payload, index);
            const bool last = chunk.offset + chunk.data.size() == handle.size_bytes;
            return exchange.apply_output_chunk(K_WORKER, chunk, last);
        }

        /** @brief Decodes a frame as BUFFER_ABORT and checks it names the buffer and the cause. */
        void expect_abort(const SentFrame& frame, const BufferHandle& handle, Wire::AbortReason reason)
        {
            EXPECT_EQ(frame.status, FrameSourceStatus::FinalFrame);
            ASSERT_EQ(frame.type, MessageType::BufferAbort);
            EXPECT_EQ(frame.flags, 0);

            Wire::BufferAbortMessage abort;
            ASSERT_TRUE(Wire::BufferAbortMessage::decode(view_of(frame.payload), abort).ok());
            EXPECT_EQ(abort.id, handle.id);
            EXPECT_EQ(abort.version, handle.version);
            EXPECT_EQ(abort.task_id, 0u);
            EXPECT_EQ(abort.reason, reason);
        }
    } // namespace

    TEST(Unit_BufferExchangeTest, ChunkedRoundTrip)
    {
        for (const std::size_t size : K_BOUNDARY_SIZES)
        {
            SCOPED_TRACE("buffer of " + std::to_string(size) + " bytes");
            Bytes source = guarded_pattern(size, 7);
            Bytes destination(size + 1, K_GUARD);

            BufferRegistry registry;
            BufferExchange exchange(registry, K_CHUNK_BYTES);
            const BufferHandle pulled = registry.register_buffer(source.data(), size);
            const BufferHandle written = registry.register_buffer(destination.data(), size);

            OutboundFrame frame;
            const std::unique_ptr<FrameSource> stream = exchange.serve_pull(pulled.id, pulled.version);
            const std::vector<SentFrame> frames = drain(*stream, frame);
            const std::size_t expected_frames =
                size == 0 ? 1 : (size + K_CHUNK_BYTES - 1) / K_CHUNK_BYTES;
            ASSERT_EQ(frames.size(), expected_frames);

            Bytes reassembled;
            for (std::size_t index = 0; index < frames.size(); ++index)
            {
                const bool last = index + 1 == frames.size();
                ASSERT_EQ(frames[index].type, MessageType::BufferChunk);
                EXPECT_EQ(frames[index].flags, last ? Wire::K_FRAME_FLAG_LAST_CHUNK : 0);

                Wire::BufferChunkMessage chunk;
                ASSERT_TRUE(
                    Wire::BufferChunkMessage::decode(view_of(frames[index].payload), chunk).ok());
                EXPECT_EQ(chunk.purpose, Wire::ChunkPurpose::PullReply);
                EXPECT_EQ(chunk.task_id, 0u);
                EXPECT_EQ(chunk.handle.id, pulled.id);
                EXPECT_EQ(chunk.handle.version, pulled.version);
                EXPECT_EQ(chunk.handle.size_bytes, size);
                EXPECT_EQ(chunk.offset, reassembled.size());
                EXPECT_LE(chunk.data.size(), K_CHUNK_BYTES);
                EXPECT_EQ(frames[index].payload.size(),
                          Wire::BufferChunkMessage::K_PREFIX_BYTES + chunk.data.size());

                Wire::BufferChunkMessage output = chunk;
                output.purpose = Wire::ChunkPurpose::TaskOutput;
                output.task_id = K_TASK_ID;
                output.handle = written;
                EXPECT_EQ(exchange.apply_output_chunk(K_WORKER, output, last),
                          last ? ChunkStatus::Completed : ChunkStatus::Applied);

                reassembled.insert(reassembled.end(), chunk.data.begin(), chunk.data.end());
            }

            reassembled.push_back(K_GUARD);
            EXPECT_EQ(reassembled, source);
            EXPECT_EQ(destination, source);
            EXPECT_TRUE(exchange.finish_task(K_TASK_ID, {&written, 1}));
            EXPECT_FALSE(exchange.finish_task(K_TASK_ID, {&written, 1}));

            const BufferExchangeStatistics statistics = exchange.statistics();
            EXPECT_EQ(statistics.chunks_sent, expected_frames);
            EXPECT_EQ(statistics.chunks_received, expected_frames);
            EXPECT_EQ(statistics.transfers_aborted, 0u);
            EXPECT_EQ(statistics.stale_writebacks, 0u);
        }
    }

    TEST(Unit_BufferExchangeTest, EveryStreamEndsWithALastChunk)
    {
        for (const std::size_t size : K_BOUNDARY_SIZES)
        {
            SCOPED_TRACE("buffer of " + std::to_string(size) + " bytes");
            Bytes source = guarded_pattern(size, 11);

            BufferRegistry registry;
            BufferExchange exchange(registry, K_CHUNK_BYTES);
            const BufferHandle handle = registry.register_buffer(source.data(), size);

            OutboundFrame frame;
            const std::unique_ptr<FrameSource> stream = exchange.serve_pull(handle.id, handle.version);
            const std::vector<SentFrame> frames = drain(*stream, frame);
            ASSERT_FALSE(frames.empty());

            for (std::size_t index = 0; index + 1 < frames.size(); ++index)
            {
                EXPECT_EQ(frames[index].status, FrameSourceStatus::Frame);
                EXPECT_EQ(frames[index].flags, 0);
            }
            EXPECT_EQ(frames.back().status, FrameSourceStatus::FinalFrame);
            EXPECT_EQ(frames.back().type, MessageType::BufferChunk);
            EXPECT_EQ(frames.back().flags, Wire::K_FRAME_FLAG_LAST_CHUNK);

            const Bytes final_payload = frame.payload;
            EXPECT_EQ(stream->next(frame), FrameSourceStatus::Finished);
            EXPECT_EQ(frame.payload, final_payload);
        }
    }

    TEST(Unit_BufferExchangeTest, ForgedVersionFenced)
    {
        const std::size_t size = 2 * K_CHUNK_BYTES;
        const Bytes untouched = guarded_pattern(size, 23);
        const Bytes payload = guarded_pattern(size, 59);
        Bytes destination = untouched;

        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        BufferHandle dispatched = registry.register_buffer(destination.data(), size);

        BufferHandle ahead = dispatched;
        ++ahead.version;
        EXPECT_EQ(apply_piece(exchange, ahead, payload, 0), ChunkStatus::Stale);

        BufferHandle behind = dispatched;
        --behind.version;
        EXPECT_EQ(apply_piece(exchange, behind, payload, 0), ChunkStatus::Stale);
        EXPECT_EQ(destination, untouched);

        EXPECT_EQ(apply_piece(exchange, dispatched, payload, 0), ChunkStatus::Applied);

        // A reaper bumps a task's outputs before it resubmits; the chunk in flight is then late.
        BufferHandle bumped = dispatched;
        registry.bump({&bumped, 1});
        EXPECT_EQ(apply_piece(exchange, dispatched, payload, 1), ChunkStatus::Stale);

        EXPECT_TRUE(std::equal(destination.begin() + K_CHUNK_BYTES, destination.end(),
                               untouched.begin() + K_CHUNK_BYTES));
        EXPECT_FALSE(exchange.finish_task(K_TASK_ID, {&dispatched, 1}));

        const BufferExchangeStatistics statistics = exchange.statistics();
        EXPECT_EQ(statistics.stale_writebacks, 3u);
        EXPECT_EQ(statistics.chunks_received, 1u);
    }

    TEST(Unit_BufferExchangeTest, GapDiscardsTransfer)
    {
        const std::size_t size = 3 * K_CHUNK_BYTES + 7;
        const Bytes payload = guarded_pattern(size, 5);
        Bytes destination(size + 1, K_GUARD);

        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(destination.data(), size);

        EXPECT_EQ(apply_piece(exchange, handle, payload, 0), ChunkStatus::Applied);
        EXPECT_EQ(apply_piece(exchange, handle, payload, 2), ChunkStatus::Gap);

        // Piece 1 was the next expected one; the gap discarded the transfer, so it is refused too.
        EXPECT_EQ(apply_piece(exchange, handle, payload, 1), ChunkStatus::Gap);
        EXPECT_FALSE(exchange.finish_task(K_TASK_ID, {&handle, 1}));

        EXPECT_EQ(apply_piece(exchange, handle, payload, 0), ChunkStatus::Applied);
        EXPECT_EQ(apply_piece(exchange, handle, payload, 1), ChunkStatus::Applied);
        EXPECT_EQ(apply_piece(exchange, handle, payload, 2), ChunkStatus::Applied);
        EXPECT_EQ(apply_piece(exchange, handle, payload, 3), ChunkStatus::Completed);
        EXPECT_TRUE(exchange.finish_task(K_TASK_ID, {&handle, 1}));
        EXPECT_EQ(destination, payload);
    }

    TEST(Unit_BufferExchangeTest, DuplicateChunkDiscardsTransfer)
    {
        const std::size_t size = 3 * K_CHUNK_BYTES;
        const Bytes payload = guarded_pattern(size, 3);
        Bytes destination(size + 1, K_GUARD);

        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(destination.data(), size);

        EXPECT_EQ(apply_piece(exchange, handle, payload, 0), ChunkStatus::Applied);
        EXPECT_EQ(apply_piece(exchange, handle, payload, 1), ChunkStatus::Applied);
        EXPECT_EQ(apply_piece(exchange, handle, payload, 1), ChunkStatus::Gap);
        EXPECT_EQ(apply_piece(exchange, handle, payload, 2), ChunkStatus::Gap);
        EXPECT_FALSE(exchange.finish_task(K_TASK_ID, {&handle, 1}));
        EXPECT_EQ(exchange.statistics().chunks_received, 2u);
    }

    TEST(Unit_BufferExchangeTest, ReleaseDuringStreamAborts)
    {
        const std::size_t size = 3 * K_CHUNK_BYTES;
        std::unique_ptr<std::byte[]> storage = std::make_unique<std::byte[]>(size);
        std::fill(storage.get(), storage.get() + size, std::byte{0x5A});

        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(storage.get(), size);

        OutboundFrame frame;
        const std::unique_ptr<FrameSource> stream = exchange.serve_pull(handle.id, handle.version);
        ASSERT_EQ(stream->next(frame), FrameSourceStatus::Frame);
        ASSERT_EQ(frame.type, MessageType::BufferChunk);

        // The allocation is freed for real, so a read of it after this line fails under ASan.
        EXPECT_EQ(registry.unregister_buffer(storage.get()), handle.id);
        storage.reset();

        const std::vector<SentFrame> rest = drain(*stream, frame);
        ASSERT_EQ(rest.size(), 1u);
        expect_abort(rest.front(), handle, Wire::AbortReason::Gone);
        EXPECT_EQ(stream->next(frame), FrameSourceStatus::Finished);

        BufferHandle unknown;
        unknown.id.value = 999;
        unknown.version = 1;
        const std::unique_ptr<FrameSource> refused = exchange.serve_pull(unknown.id, unknown.version);
        const std::vector<SentFrame> only = drain(*refused, frame);
        ASSERT_EQ(only.size(), 1u);
        expect_abort(only.front(), unknown, Wire::AbortReason::Gone);

        const BufferExchangeStatistics statistics = exchange.statistics();
        EXPECT_EQ(statistics.chunks_sent, 1u);
        EXPECT_EQ(statistics.transfers_aborted, 2u);
    }

    TEST(Unit_BufferExchangeTest, StaleVersionAbortsThePull)
    {
        const std::size_t size = 3 * K_CHUNK_BYTES;
        Bytes source = guarded_pattern(size, 13);

        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(source.data(), size);

        OutboundFrame frame;
        const std::unique_ptr<FrameSource> stream = exchange.serve_pull(handle.id, handle.version);
        ASSERT_EQ(stream->next(frame), FrameSourceStatus::Frame);

        void* const address = source.data();
        registry.bump_addresses({&address, 1});

        const std::vector<SentFrame> rest = drain(*stream, frame);
        ASSERT_EQ(rest.size(), 1u);
        expect_abort(rest.front(), handle, Wire::AbortReason::Stale);

        // A request that names a version the buffer has already left gets the abort alone.
        const std::unique_ptr<FrameSource> late = exchange.serve_pull(handle.id, handle.version);
        const std::vector<SentFrame> only = drain(*late, frame);
        ASSERT_EQ(only.size(), 1u);
        expect_abort(only.front(), handle, Wire::AbortReason::Stale);

        const BufferExchangeStatistics statistics = exchange.statistics();
        EXPECT_EQ(statistics.chunks_sent, 1u);
        EXPECT_EQ(statistics.transfers_aborted, 2u);
    }

    TEST(Unit_BufferExchangeTest, MalformedChunkIsRefusedBeforeAnyWrite)
    {
        const std::size_t size = 2 * K_CHUNK_BYTES;
        const Bytes untouched = guarded_pattern(size, 17);
        const Bytes payload = guarded_pattern(size, 29);
        Bytes destination = untouched;

        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(destination.data(), size);

        Wire::BufferChunkMessage pull_reply = output_chunk(handle, payload, 0);
        pull_reply.purpose = Wire::ChunkPurpose::PullReply;
        EXPECT_EQ(exchange.apply_output_chunk(K_WORKER, pull_reply, false), ChunkStatus::Malformed);

        BufferHandle larger = handle;
        larger.size_bytes = size + 1;
        EXPECT_EQ(apply_piece(exchange, larger, payload, 0), ChunkStatus::Malformed);

        Wire::BufferChunkMessage past_the_end = output_chunk(handle, payload, 1);
        past_the_end.offset += 1;
        EXPECT_EQ(exchange.apply_output_chunk(K_WORKER, past_the_end, false), ChunkStatus::Malformed);

        const Wire::BufferChunkMessage early_last = output_chunk(handle, payload, 0);
        EXPECT_EQ(exchange.apply_output_chunk(K_WORKER, early_last, true), ChunkStatus::Malformed);

        EXPECT_EQ(destination, untouched);
        EXPECT_EQ(exchange.statistics().chunks_received, 0u);
    }

    TEST(Unit_BufferExchangeTest, ForgottenTaskStartsOver)
    {
        const std::size_t size = 2 * K_CHUNK_BYTES;
        const Bytes payload = guarded_pattern(size, 31);
        Bytes destination(size + 1, K_GUARD);

        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(destination.data(), size);

        EXPECT_EQ(apply_piece(exchange, handle, payload, 0), ChunkStatus::Applied);
        exchange.forget_task(K_TASK_ID);
        EXPECT_EQ(apply_piece(exchange, handle, payload, 1), ChunkStatus::Gap);
        EXPECT_FALSE(exchange.finish_task(K_TASK_ID, {&handle, 1}));

        // A chunk that continues a transfer must come from the connection that started it.
        EXPECT_EQ(apply_piece(exchange, handle, payload, 0), ChunkStatus::Applied);
        const Wire::BufferChunkMessage foreign = output_chunk(handle, payload, 1);
        EXPECT_EQ(exchange.apply_output_chunk(PeerID{K_WORKER.value + 1}, foreign, true),
                  ChunkStatus::Gap);
    }
} // namespace BufferExchangeTests

/** @brief Tests OffloadDispatcher over real bricks, a fake transport and a fake policy. */
namespace OffloadDispatcherTests
{
    using namespace SushiRuntime::Distributed;

    namespace
    {
        /** @brief Names the worker every test registers. */
        constexpr PeerID K_WORKER{1};

        /** @brief Names an operation the worker has a kernel for. */
        constexpr OperationID K_KNOWN_OPERATION{501};

        /** @brief Names an operation no worker has a kernel for. */
        constexpr OperationID K_ABSENT_OPERATION{502};

        /** @brief Holds the tasks the worker says it queues at most. */
        constexpr std::uint32_t K_WORKER_MAX_JOBS = 8;

        /** @brief Holds the elements of each buffer a test shares. */
        constexpr std::size_t K_BUFFER_ELEMENTS = 16;

        /** @brief Names the storage of one buffer of a test. */
        using Storage = std::array<float, K_BUFFER_ELEMENTS>;

        /** @brief Holds one frame a test transport was asked to send. */
        struct OfferedFrame
        {
            PeerID peer;                    /**< The peer the frame was for. */
            MessageType type;               /**< The type of the frame. */
            std::vector<std::byte> payload; /**< A copy of the payload. */
        };

        /**
         * @brief Records every send() and answers it with a status the test sets.
         */
        class RecordingTransport final : public Transport
        {
            public:
                /** @brief Reports a start that needs nothing. */
                [[nodiscard]] TransportResult start(TransportEvents& /*events*/) override
                {
                    return TransportResult();
                }

                /** @brief Does nothing; the transport owns no thread. */
                void stop() noexcept override
                {

                }

                /** @brief Keeps a copy of the frame and returns the status set for the test. */
                [[nodiscard]] SendStatus send(PeerID peer, MessageType type,
                                              span<const std::byte> payload) override
                {
                    OfferedFrame frame{peer, type, std::vector<std::byte>()};
                    frame.payload.assign(payload.begin(), payload.end());
                    offered.push_back(std::move(frame));
                    return answer;
                }

                /** @brief Refuses every stream; the dispatcher sends none. */
                [[nodiscard]] SendStatus send_stream(PeerID /*peer*/,
                                                     std::unique_ptr<FrameSource> /*source*/) override
                {
                    return SendStatus::Stopped;
                }

                /** @brief Does nothing; the transport holds no connection. */
                void close_peer(PeerID /*peer*/) noexcept override
                {

                }

                /** @brief Reports a transport only this process reaches. */
                [[nodiscard]] TransportExposure exposure() const noexcept override
                {
                    return TransportExposure::Loopback;
                }

                /** @brief Reports that no byte moved. */
                [[nodiscard]] TransportStatistics statistics() const noexcept override
                {
                    return TransportStatistics();
                }

                /** @brief Holds what send() returns. */
                SendStatus answer = SendStatus::Queued;

                /** @brief Holds every frame send() was given, in order. */
                std::vector<OfferedFrame> offered;
        };

        /**
         * @brief Picks the first worker it is offered and counts how often it was asked.
         */
        class FirstWorkerPolicy final : public OffloadPolicy
        {
            public:
                /** @brief Returns the first offered worker, or nothing when none was offered. */
                std::optional<int> pick_worker(const NodeOptions& /*task*/,
                                               span<const WorkerLoad> workers,
                                               std::uint64_t /*est_bytes*/) override
                {
                    ++calls;
                    if (workers.empty())
                        return std::nullopt;
                    return workers.begin()->worker_id;
                }

                /** @brief Counts the calls to pick_worker(). */
                int calls = 0;
        };

        /** @brief Returns handshake octets as the bytes a wire message carries. */
        template <std::size_t Count>
        std::array<std::byte, Count> to_wire(const std::array<std::uint8_t, Count>& octets)
        {
            std::array<std::byte, Count> bytes{};
            std::transform(octets.begin(), octets.end(), bytes.begin(),
                           [](std::uint8_t octet) { return std::byte{octet}; });
            return bytes;
        }

        /** @brief Returns the dispatch a transport was offered, decoded. */
        Wire::TaskDispatchMessage decode_dispatch(const OfferedFrame& frame)
        {
            Wire::TaskDispatchMessage dispatch;
            const Wire::DecodeResult decoded = Wire::TaskDispatchMessage::decode(
                span<const std::byte>(frame.payload.data(), frame.payload.size()), dispatch);
            EXPECT_TRUE(decoded.ok());
            return dispatch;
        }

        /**
         * @brief Builds a dispatcher over a registry, a directory with one worker, a pending
         *        table and a recording transport, and restores the logger a test changed.
         */
        class Unit_OffloadDispatcherTest : public ::testing::Test
        {
            protected:
                /** @brief Registers the worker and shares two of the three buffers. */
                void SetUp() override
                {
                    saved_level_ = log_level();
                    set_log_synchronous(true);
                    set_log_level(LogLevel::Warn);

                    register_worker();
                    registry_.register_buffer(first_.data(), sizeof(Storage));
                    registry_.register_buffer(second_.data(), sizeof(Storage));
                }

                /** @brief Puts the default sink and the saved level back. */
                void TearDown() override
                {
                    set_log_sink(nullptr);
                    set_log_level(saved_level_);
                }

                /** @brief Takes the worker through HELLO and PROOF under the empty secret. */
                void register_worker()
                {
                    const std::chrono::steady_clock::time_point now =
                        std::chrono::steady_clock::now();
                    HandshakeNonce worker_nonce{};
                    worker_nonce.bytes.fill(0x11);
                    HandshakeNonce master_nonce{};
                    master_nonce.bytes.fill(0x22);

                    directory_.add_peer(K_WORKER, "test:1", now);

                    Wire::HelloMessage hello;
                    hello.worker_nonce = to_wire(worker_nonce.bytes);
                    hello.build_id = "test-build";
                    hello.node_name = "worker";
                    ASSERT_EQ(directory_.accept_hello(K_WORKER, hello, master_nonce).step,
                              HandshakeStep::Accepted);

                    Wire::ProofMessage proof;
                    proof.worker_proof = to_wire(compute_proof(
                        HandshakeRole::Worker, span<const std::uint8_t>(), worker_nonce,
                        master_nonce));
                    proof.ops = {K_KNOWN_OPERATION};
                    proof.max_jobs = K_WORKER_MAX_JOBS;
                    ASSERT_EQ(directory_.accept_proof(K_WORKER, proof, now), HandshakeStep::Accepted);
                }

                /** @brief Returns a node that reads @p read and writes @p written under @p op. */
                static sushi_ptr<Internal::Graph::Node> fixed_node(OperationID op, void* read, void* written)
                {
                    sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
                    node->metadata.op_id = op;
                    node->io_reads_.emplace_back(read);
                    node->io_writes_.emplace_back(written);
                    return node;
                }

                /** @brief Runs prepare() over the given nodes. */
                void prepare(const std::vector<sushi_ptr<Internal::Graph::Node>>& nodes)
                {
                    dispatcher_.prepare(
                        span<const sushi_ptr<Internal::Graph::Node>>(nodes.data(), nodes.size()));
                }

                /** @brief Returns the version the registry holds for an address. */
                std::uint32_t version_of(void* address) const
                {
                    const std::optional<BufferHandle> handle = registry_.lookup(address);
                    EXPECT_TRUE(handle.has_value());
                    return handle.has_value() ? handle->version : 0;
                }

                /** @brief Returns the worker's inflight count as the directory reports it. */
                std::uint32_t worker_inflight() const
                {
                    std::vector<WorkerSnapshot> workers;
                    directory_.snapshot(std::chrono::steady_clock::now(), workers);
                    EXPECT_EQ(workers.size(), 1u);
                    return workers.empty() ? 0 : workers.front().inflight;
                }

                /// @brief The storage of the first shared buffer.
                Storage first_{};

                /// @brief The storage of the second shared buffer.
                Storage second_{};

                /// @brief The storage of a buffer no test shares.
                Storage unshared_{};

                /// @brief The registry the dispatcher reads handles from.
                BufferRegistry registry_;

                /// @brief The policy the directory asks.
                std::shared_ptr<FirstWorkerPolicy> policy_ = std::make_shared<FirstWorkerPolicy>();

                /// @brief The directory with the one worker.
                WorkerDirectory directory_{span<const std::uint8_t>(), policy_};

                /// @brief The table that takes each dispatched node.
                PendingTasks pending_;

                /// @brief The transport that records each dispatch.
                RecordingTransport transport_;

                /// @brief The dispatcher under test.
                OffloadDispatcher dispatcher_{registry_, directory_, pending_, transport_};

                /// @brief The level the process had when the test began.
                LogLevel saved_level_ = LogLevel::Warn;
        };
    } // namespace

    /** @brief Tests that a node at the attempt cap is declined and offered again next run. */
    TEST_F(Unit_OffloadDispatcherTest, AttemptCapDeclinesWithoutClearingOffloadable)
    {
        const sushi_ptr<Internal::Graph::Node> node =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        prepare({node});
        Internal::Graph::OffloadState* state = node->offload_state();
        ASSERT_NE(state, nullptr);
        ASSERT_TRUE(state->offloadable);

        state->attempts.store(K_MAXIMUM_OFFLOAD_ATTEMPTS);
        EXPECT_FALSE(dispatcher_.try_offload(*node));
        EXPECT_TRUE(state->offloadable);
        EXPECT_EQ(state->attempts.load(), K_MAXIMUM_OFFLOAD_ATTEMPTS);
        EXPECT_TRUE(transport_.offered.empty());
        EXPECT_EQ(policy_->calls, 0);
        EXPECT_EQ(dispatcher_.statistics().declined, 1u);

        dispatcher_.complete_local(*node);
        EXPECT_EQ(state->attempts.load(), 0u);

        EXPECT_TRUE(dispatcher_.try_offload(*node));
        EXPECT_EQ(transport_.offered.size(), 1u);
        EXPECT_EQ(dispatcher_.statistics().offloaded, 1u);
        EXPECT_EQ(pending_.size(), 1u);

        std::vector<PendingTask> held;
        pending_.drain(held);
        ASSERT_EQ(held.size(), 1u);
        EXPECT_EQ(held.front().node, node.get());
    }

    /** @brief Tests that a send the transport refuses leaves no task, no load and a false. */
    TEST_F(Unit_OffloadDispatcherTest, RefusedSendUndoesThePendingEntry)
    {
        const sushi_ptr<Internal::Graph::Node> node =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        prepare({node});
        const std::uint32_t output_version = version_of(second_.data());

        transport_.answer = SendStatus::QueueFull;
        EXPECT_FALSE(dispatcher_.try_offload(*node));

        ASSERT_EQ(transport_.offered.size(), 1u);
        EXPECT_EQ(transport_.offered.front().type, MessageType::TaskDispatch);
        EXPECT_EQ(transport_.offered.front().peer, K_WORKER);
        EXPECT_EQ(pending_.size(), 0u);
        EXPECT_EQ(worker_inflight(), 0u);

        const OffloadDispatcherStatistics counters = dispatcher_.statistics();
        EXPECT_EQ(counters.offloaded, 0u);
        EXPECT_EQ(counters.declined, 1u);
        EXPECT_EQ(counters.send_queue_full, 1u);

        // The dispatch frame named the bumped version, so the registry keeps it.
        EXPECT_EQ(version_of(second_.data()), output_version + 1);
        EXPECT_EQ(node->offload_state()->attempts.load(), 1u);
    }

    /** @brief Tests that nothing is dispatched once the pending table and the directory closed. */
    TEST_F(Unit_OffloadDispatcherTest, StoppedCoordinatorDeclines)
    {
        const sushi_ptr<Internal::Graph::Node> node =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        prepare({node});

        // With only the table closed the worker is picked, then given back.
        pending_.close();
        EXPECT_FALSE(dispatcher_.try_offload(*node));
        EXPECT_EQ(policy_->calls, 1);
        EXPECT_EQ(worker_inflight(), 0u);
        EXPECT_TRUE(transport_.offered.empty());

        directory_.close();
        EXPECT_FALSE(dispatcher_.try_offload(*node));
        EXPECT_EQ(policy_->calls, 1);
        EXPECT_TRUE(transport_.offered.empty());
        EXPECT_EQ(pending_.size(), 0u);
        EXPECT_EQ(dispatcher_.statistics().declined, 2u);
        EXPECT_TRUE(node->offload_state()->offloadable);
    }

    /** @brief Tests that a double-buffered node dispatches and settles its live lists. */
    TEST_F(Unit_OffloadDispatcherTest, DynamicNodeResolvesItsLiveLists)
    {
        const sushi_ptr<Internal::Graph::Node> node =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        Internal::Graph::OffloadState& state = node->offload();
        state.live_reads = std::make_shared<std::vector<void*>>(1, static_cast<void*>(first_.data()));
        state.live_writes = std::make_shared<std::vector<void*>>(1, static_cast<void*>(second_.data()));
        prepare({node});
        ASSERT_TRUE(state.offloadable);
        EXPECT_EQ(state.payload, nullptr);

        const GlobalBufferID first_id = registry_.lookup(first_.data())->id;
        const GlobalBufferID second_id = registry_.lookup(second_.data())->id;

        ASSERT_TRUE(dispatcher_.try_offload(*node));
        ASSERT_EQ(transport_.offered.size(), 1u);
        const Wire::TaskDispatchMessage before_swap = decode_dispatch(transport_.offered[0]);
        ASSERT_EQ(before_swap.inputs.size(), 1u);
        ASSERT_EQ(before_swap.outputs.size(), 1u);
        EXPECT_EQ(before_swap.inputs[0].id, first_id);
        EXPECT_EQ(before_swap.outputs[0].id, second_id);
        EXPECT_EQ(before_swap.outputs[0].version, version_of(second_.data()));

        std::vector<PendingTask> held;
        pending_.drain(held);
        ASSERT_EQ(held.size(), 1u);
        directory_.release_inflight(held.front().worker);
        dispatcher_.complete_remote(*node);

        // The step boundary swaps the two buffers; the baked lists still name the old roles.
        state.live_reads->assign(1, static_cast<void*>(second_.data()));
        state.live_writes->assign(1, static_cast<void*>(first_.data()));

        ASSERT_TRUE(dispatcher_.try_offload(*node));
        ASSERT_EQ(transport_.offered.size(), 2u);
        const Wire::TaskDispatchMessage after_swap = decode_dispatch(transport_.offered[1]);
        ASSERT_EQ(after_swap.inputs.size(), 1u);
        ASSERT_EQ(after_swap.outputs.size(), 1u);
        EXPECT_EQ(after_swap.inputs[0].id, second_id);
        EXPECT_EQ(after_swap.outputs[0].id, first_id);

        held.clear();
        pending_.drain(held);
        ASSERT_EQ(held.size(), 1u);

        const std::uint32_t first_version = version_of(first_.data());
        const std::uint32_t second_version = version_of(second_.data());
        dispatcher_.complete_local(*node);
        EXPECT_EQ(version_of(first_.data()), first_version + 1);
        EXPECT_EQ(version_of(second_.data()), second_version);
    }

    /** @brief Tests that one prepare() that leaves op-tagged nodes local logs one WARN. */
    TEST_F(Unit_OffloadDispatcherTest, PrepareWarnsOnceForLocalOnlyOps)
    {
        std::vector<std::string> warnings;
        std::vector<std::string>* const sink_target = &warnings;
        set_log_sink([sink_target](LogLevel level, std::string_view line)
        {
            if (level == LogLevel::Warn)
                sink_target->push_back(std::string(line));
        });

        const sushi_ptr<Internal::Graph::Node> offloadable =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        const sushi_ptr<Internal::Graph::Node> without_worker =
            fixed_node(K_ABSENT_OPERATION, first_.data(), second_.data());
        const sushi_ptr<Internal::Graph::Node> without_share =
            fixed_node(K_KNOWN_OPERATION, first_.data(), unshared_.data());
        const sushi_ptr<Internal::Graph::Node> untagged =
            fixed_node(OperationID(0), first_.data(), unshared_.data());

        prepare({offloadable, without_worker, without_share, untagged, sushi_ptr<Internal::Graph::Node>()});

        ASSERT_EQ(warnings.size(), 1u);
        const std::string expected =
            "2 of 3 op-tagged nodes will run locally: 1 no live worker for the op, 1 buffer not "
            "shared, 0 buffer written twice (first: op " +
            std::to_string(K_ABSENT_OPERATION.value) + ")";
        EXPECT_NE(warnings.front().find(expected), std::string::npos) << warnings.front();

        EXPECT_TRUE(offloadable->offload_state()->offloadable);
        EXPECT_FALSE(without_worker->offload_state()->offloadable);
        EXPECT_EQ(without_share->offload_state(), nullptr);
        EXPECT_EQ(untagged->offload_state(), nullptr);

        prepare({offloadable});
        EXPECT_EQ(warnings.size(), 1u);
    }

    /** @brief Tests that a node that stays local raises only the shared buffers it writes. */
    TEST_F(Unit_OffloadDispatcherTest, LocalNodeBumpsOnlyItsSharedWrites)
    {
        const sushi_ptr<Internal::Graph::Node> writer =
            fixed_node(OperationID(0), unshared_.data(), second_.data());
        const sushi_ptr<Internal::Graph::Node> reader =
            fixed_node(OperationID(0), second_.data(), unshared_.data());
        prepare({writer, reader});

        EXPECT_EQ(reader->offload_state(), nullptr);
        ASSERT_NE(writer->offload_state(), nullptr);
        EXPECT_FALSE(writer->offload_state()->offloadable);

        const std::uint32_t first_version = version_of(first_.data());
        const std::uint32_t second_version = version_of(second_.data());
        dispatcher_.complete_local(*writer);
        dispatcher_.complete_local(*reader);
        EXPECT_EQ(version_of(first_.data()), first_version);
        EXPECT_EQ(version_of(second_.data()), second_version + 1);
    }

    /** @brief Tests that a node whose writes name one buffer twice stays local and is counted. */
    TEST_F(Unit_OffloadDispatcherTest, NodeThatWritesOneBufferTwiceStaysLocal)
    {
        std::vector<std::string> warnings;
        std::vector<std::string>* const sink_target = &warnings;
        set_log_sink([sink_target](LogLevel level, std::string_view line)
        {
            if (level == LogLevel::Warn)
                sink_target->push_back(std::string(line));
        });

        const sushi_ptr<Internal::Graph::Node> fixed =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        fixed->io_writes_.emplace_back(second_.data());

        const sushi_ptr<Internal::Graph::Node> dynamic =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        Internal::Graph::OffloadState& dynamic_state = dynamic->offload();
        dynamic_state.live_reads =
            std::make_shared<std::vector<void*>>(1, static_cast<void*>(first_.data()));
        dynamic_state.live_writes =
            std::make_shared<std::vector<void*>>(2, static_cast<void*>(second_.data()));

        // The same buffer as an input and as an output is one write, so this node may leave.
        const sushi_ptr<Internal::Graph::Node> in_place =
            fixed_node(K_KNOWN_OPERATION, second_.data(), second_.data());

        prepare({fixed, dynamic, in_place});

        ASSERT_EQ(warnings.size(), 1u);
        const std::string expected =
            "2 of 3 op-tagged nodes will run locally: 0 no live worker for the op, 0 buffer not "
            "shared, 2 buffer written twice (first: op " +
            std::to_string(K_KNOWN_OPERATION.value) + ")";
        EXPECT_NE(warnings.front().find(expected), std::string::npos) << warnings.front();

        ASSERT_NE(fixed->offload_state(), nullptr);
        EXPECT_FALSE(fixed->offload_state()->offloadable);
        EXPECT_FALSE(dynamic_state.offloadable);
        EXPECT_TRUE(in_place->offload_state()->offloadable);

        // The local run still raises the buffer the node wrote, so a worker's copy goes stale.
        const std::uint32_t version = version_of(second_.data());
        dispatcher_.complete_local(*fixed);
        EXPECT_GT(version_of(second_.data()), version);
    }
} // namespace OffloadDispatcherTests

/** @brief Tests PendingTasks: ownership of offloaded nodes, the removals and the three reapers. */
namespace PendingTasksTests
{
    using namespace SushiRuntime::Distributed;

    namespace
    {
        using Internal::Graph::Node;
        using std::chrono::milliseconds;

        /** @brief Names the worker most tests dispatch to. */
        constexpr PeerID K_WORKER{1};

        /** @brief Names a second worker. */
        constexpr PeerID K_OTHER_WORKER{2};

        /** @brief Holds the acknowledgement limit the reaper tests use, in milliseconds. */
        constexpr milliseconds K_ACKNOWLEDGE_TIMEOUT{5000};

        /** @brief Counts the rounds of the two-thread removal test. */
        constexpr int K_RACE_ROUNDS = 200;

        /** @brief Returns a fixed time point @p offset after the tests' zero. */
        std::chrono::steady_clock::time_point at(milliseconds offset)
        {
            return std::chrono::steady_clock::time_point(std::chrono::hours(1)) + offset;
        }

        /** @brief Returns the limits of a cluster whose task deadline is @p task_deadline. */
        OverdueLimits limits_with(milliseconds task_deadline)
        {
            OverdueLimits limits;
            limits.task_deadline = task_deadline;
            limits.acknowledge_timeout = K_ACKNOWLEDGE_TIMEOUT;
            return limits;
        }

        /** @brief Returns an output handle of 64 bytes. */
        BufferHandle output_of(std::uint64_t id, std::uint32_t version)
        {
            BufferHandle handle;
            handle.id = GlobalBufferID{id};
            handle.version = version;
            handle.size_bytes = 64;
            return handle;
        }

        /** @brief Inserts a task with no outputs and returns whether the table took it. */
        bool insert_plain(PendingTasks& table, std::uint64_t task_id, Node& node, PeerID worker,
                          milliseconds offset = milliseconds(0))
        {
            return table.insert(task_id, node, worker, span<const BufferHandle>(), at(offset));
        }

        /** @brief Returns the ids of the given tasks in rising order. */
        std::vector<std::uint64_t> sorted_ids(const std::vector<PendingTask>& tasks)
        {
            std::vector<std::uint64_t> ids;
            ids.reserve(tasks.size());
            for (const PendingTask& task : tasks)
                ids.push_back(task.task_id);
            std::sort(ids.begin(), ids.end());
            return ids;
        }

        /** @brief Releases two threads at the same moment, once both have arrived. */
        class StartGate final
        {
            public:
                /** @brief Reports arrival, then spins until both threads have arrived. */
                void arrive_and_wait() noexcept
                {
                    arrived_.fetch_add(1, std::memory_order_acq_rel);
                    while (arrived_.load(std::memory_order_acquire) < 2)
                        std::this_thread::yield();
                }

            private:
                /** @brief Counts the threads that have arrived. */
                std::atomic<int> arrived_{0};
        };
    } // namespace

    TEST(Unit_PendingTasks, TaskIDsRiseFromOne)
    {
        PendingTasks table;

        EXPECT_EQ(table.allocate_task_id(), 1u);
        EXPECT_EQ(table.allocate_task_id(), 2u);
        EXPECT_EQ(table.allocate_task_id(), 3u);
    }

    TEST(Unit_PendingTasks, InsertAfterCloseRefused)
    {
        PendingTasks table;
        const sushi_ptr<Node> first = make_sushi<Node>();
        const sushi_ptr<Node> second = make_sushi<Node>();

        ASSERT_TRUE(insert_plain(table, 1, *first, K_WORKER));
        EXPECT_FALSE(insert_plain(table, 1, *second, K_WORKER)) << "an id in the table is refused";

        table.close();

        EXPECT_FALSE(insert_plain(table, 2, *second, K_WORKER));
        EXPECT_EQ(table.size(), 1u);
        EXPECT_EQ(table.count_for_worker(K_WORKER), 1u);

        std::vector<PendingTask> drained;
        table.drain(drained);
        ASSERT_EQ(drained.size(), 1u);
        EXPECT_EQ(drained[0].node, first.get());
    }

    TEST(Unit_PendingTasks, TakeIsExactlyOnce)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();
        const std::vector<BufferHandle> outputs = {output_of(7, 3), output_of(8, 1)};

        ASSERT_TRUE(table.insert(5, *node, K_WORKER,
                                 span<const BufferHandle>(outputs.data(), outputs.size()),
                                 at(milliseconds(0))));
        EXPECT_EQ(table.size(), 1u);

        const std::optional<PendingTask> taken = table.take(5, K_WORKER);
        ASSERT_TRUE(taken.has_value());
        EXPECT_EQ(taken->task_id, 5u);
        EXPECT_EQ(taken->node, node.get());
        EXPECT_EQ(taken->worker, K_WORKER);
        ASSERT_EQ(taken->outputs.size(), 2u);
        EXPECT_EQ(taken->outputs[0].id, GlobalBufferID{7});
        EXPECT_EQ(taken->outputs[0].version, 3u);
        EXPECT_EQ(taken->outputs[1].id, GlobalBufferID{8});
        EXPECT_EQ(taken->outputs[1].size_bytes, 64u);

        EXPECT_FALSE(table.take(5, K_WORKER).has_value());
        EXPECT_EQ(table.size(), 0u);
        EXPECT_EQ(table.count_for_worker(K_WORKER), 0u);

        std::vector<PendingTask> rest;
        table.take_for_worker(K_WORKER, rest);
        table.take_overdue(at(milliseconds(1000000)), limits_with(milliseconds(1)), rest);
        table.drain(rest);
        EXPECT_TRUE(rest.empty()) << "a taken task leaves through no second removal";
    }

    TEST(Unit_PendingTasks, DrainReturnsEveryNode)
    {
        PendingTasks table;
        const std::vector<sushi_ptr<Node>> nodes = {make_sushi<Node>(), make_sushi<Node>(),
                                                    make_sushi<Node>()};

        ASSERT_TRUE(insert_plain(table, 1, *nodes[0], K_WORKER));
        ASSERT_TRUE(insert_plain(table, 2, *nodes[1], K_OTHER_WORKER));
        ASSERT_TRUE(insert_plain(table, 3, *nodes[2], K_WORKER));
        ASSERT_TRUE(table.acknowledge(3, K_WORKER));

        table.close();
        std::vector<PendingTask> drained;
        table.drain(drained);

        ASSERT_EQ(sorted_ids(drained), (std::vector<std::uint64_t>{1, 2, 3}));
        for (const PendingTask& task : drained)
            EXPECT_EQ(task.node, nodes[task.task_id - 1].get());

        EXPECT_EQ(table.size(), 0u);
        EXPECT_EQ(table.count_for_worker(K_WORKER), 0u);
        EXPECT_EQ(table.count_for_worker(K_OTHER_WORKER), 0u);

        table.drain(drained);
        EXPECT_EQ(drained.size(), 3u) << "a second drain finds nothing";
    }

    TEST(Unit_PendingTasks, LateResultLeavesInflightAlone)
    {
        PendingTasks table;
        const sushi_ptr<Node> lost = make_sushi<Node>();
        const sushi_ptr<Node> running = make_sushi<Node>();

        EXPECT_FALSE(table.take(9, K_WORKER).has_value()) << "an id that was never dispatched";

        ASSERT_TRUE(insert_plain(table, 1, *lost, K_WORKER));
        ASSERT_TRUE(insert_plain(table, 2, *running, K_OTHER_WORKER));

        std::vector<PendingTask> returned;
        table.take_for_worker(K_WORKER, returned);
        ASSERT_EQ(returned.size(), 1u);
        EXPECT_EQ(returned[0].node, lost.get());

        EXPECT_FALSE(table.take(1, K_WORKER).has_value()) << "the result of a task already returned";
        EXPECT_FALSE(table.acknowledge(1, K_WORKER));
        EXPECT_EQ(table.size(), 1u);
        EXPECT_EQ(table.count_for_worker(K_OTHER_WORKER), 1u);
    }

    TEST(Unit_PendingTasks, ForeignWorkerCannotTake)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();
        const std::vector<BufferHandle> outputs = {output_of(7, 3)};

        ASSERT_TRUE(table.insert(1, *node, K_WORKER,
                                 span<const BufferHandle>(outputs.data(), outputs.size()),
                                 at(milliseconds(0))));

        EXPECT_FALSE(table.take(1, K_OTHER_WORKER).has_value());
        EXPECT_FALSE(table.acknowledge(1, K_OTHER_WORKER));
        EXPECT_FALSE(table.expects_output(1, K_OTHER_WORKER, GlobalBufferID{7}, 3));
        EXPECT_EQ(table.size(), 1u);

        std::vector<PendingTask> returned;
        table.take_for_worker(K_OTHER_WORKER, returned);
        EXPECT_TRUE(returned.empty());

        const std::optional<PendingTask> taken = table.take(1, K_WORKER);
        ASSERT_TRUE(taken.has_value());
        EXPECT_EQ(taken->node, node.get());
    }

    TEST(Unit_PendingTasks, ExpectsOutputOnlyAtTheDispatchedVersion)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();
        const std::vector<BufferHandle> outputs = {output_of(7, 3)};

        ASSERT_TRUE(table.insert(1, *node, K_WORKER,
                                 span<const BufferHandle>(outputs.data(), outputs.size()),
                                 at(milliseconds(0))));

        EXPECT_TRUE(table.expects_output(1, K_WORKER, GlobalBufferID{7}, 3));
        EXPECT_FALSE(table.expects_output(1, K_WORKER, GlobalBufferID{7}, 2));
        EXPECT_FALSE(table.expects_output(1, K_WORKER, GlobalBufferID{7}, 4));
        EXPECT_FALSE(table.expects_output(1, K_WORKER, GlobalBufferID{8}, 3));
        EXPECT_FALSE(table.expects_output(2, K_WORKER, GlobalBufferID{7}, 3));

        ASSERT_TRUE(table.take(1, K_WORKER).has_value());
        EXPECT_FALSE(table.expects_output(1, K_WORKER, GlobalBufferID{7}, 3));
    }

    TEST(Unit_PendingTasks, UnacknowledgedTaskReapedWithDeadlineZero)
    {
        PendingTasks table;
        const sushi_ptr<Node> silent = make_sushi<Node>();
        const sushi_ptr<Node> accepted = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(0));

        ASSERT_TRUE(insert_plain(table, 1, *silent, K_WORKER));
        ASSERT_TRUE(insert_plain(table, 2, *accepted, K_WORKER));
        ASSERT_TRUE(table.acknowledge(2, K_WORKER));

        std::vector<PendingTask> reaped;
        table.take_overdue(at(K_ACKNOWLEDGE_TIMEOUT), limits, reaped);
        EXPECT_TRUE(reaped.empty()) << "the limit itself is still within the limit";

        table.take_overdue(at(K_ACKNOWLEDGE_TIMEOUT + milliseconds(1)), limits, reaped);
        ASSERT_EQ(reaped.size(), 1u);
        EXPECT_EQ(reaped[0].task_id, 1u);
        EXPECT_EQ(reaped[0].node, silent.get());
        EXPECT_EQ(reaped[0].cause, ReapCause::Unacknowledged);

        table.take_overdue(at(milliseconds(100000000)), limits, reaped);
        EXPECT_EQ(reaped.size(), 1u) << "deadline 0 puts no bound on an accepted task";
        EXPECT_EQ(table.size(), 1u);
    }

    TEST(Unit_PendingTasks, AcknowledgedTaskBelowOldestHeldReaped)
    {
        PendingTasks table;
        const std::vector<sushi_ptr<Node>> nodes = {make_sushi<Node>(), make_sushi<Node>(),
                                                    make_sushi<Node>(), make_sushi<Node>()};
        const OverdueLimits limits = limits_with(milliseconds(0));

        ASSERT_TRUE(insert_plain(table, 1, *nodes[0], K_WORKER));
        ASSERT_TRUE(insert_plain(table, 2, *nodes[1], K_WORKER));
        ASSERT_TRUE(insert_plain(table, 3, *nodes[2], K_WORKER));
        ASSERT_TRUE(insert_plain(table, 4, *nodes[3], K_OTHER_WORKER));
        ASSERT_TRUE(table.acknowledge(1, K_WORKER));
        ASSERT_TRUE(table.acknowledge(2, K_WORKER));
        ASSERT_TRUE(table.acknowledge(3, K_WORKER));
        ASSERT_TRUE(table.acknowledge(4, K_OTHER_WORKER));

        std::vector<PendingTask> reaped;

        // The worker holds 3 and up: tasks 1 and 2 miss once, which is one short of lost.
        table.note_heartbeat(K_WORKER, 3);
        table.take_overdue(at(milliseconds(1)), limits, reaped);
        EXPECT_TRUE(reaped.empty());

        // The worker lists 2 again, so only task 1 has missed twice in a row.
        table.note_heartbeat(K_WORKER, 2);
        table.take_overdue(at(milliseconds(2)), limits, reaped);
        ASSERT_EQ(reaped.size(), 1u);
        EXPECT_EQ(reaped[0].task_id, 1u);
        EXPECT_EQ(reaped[0].node, nodes[0].get());
        EXPECT_EQ(reaped[0].cause, ReapCause::LostResult);

        // Task 2 starts a second run of misses; task 4 belongs to a worker that sent nothing.
        table.note_heartbeat(K_WORKER, 3);
        table.take_overdue(at(milliseconds(3)), limits, reaped);
        EXPECT_EQ(reaped.size(), 1u);

        table.note_heartbeat(K_WORKER, 3);
        table.take_overdue(at(milliseconds(4)), limits, reaped);
        ASSERT_EQ(reaped.size(), 2u);
        EXPECT_EQ(reaped[1].task_id, 2u);
        EXPECT_EQ(reaped[1].cause, ReapCause::LostResult);

        EXPECT_EQ(table.count_for_worker(K_WORKER), 1u);
        EXPECT_EQ(table.count_for_worker(K_OTHER_WORKER), 1u);
    }

    TEST(Unit_PendingTasks, HeartbeatLeavesAnUnacknowledgedTaskToItsOwnLimit)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(0));

        ASSERT_TRUE(insert_plain(table, 1, *node, K_WORKER));
        table.note_heartbeat(K_WORKER, 5);
        table.note_heartbeat(K_WORKER, 5);
        table.note_heartbeat(K_WORKER, 5);

        std::vector<PendingTask> reaped;
        table.take_overdue(at(K_ACKNOWLEDGE_TIMEOUT), limits, reaped);
        EXPECT_TRUE(reaped.empty()) << "the dispatch may still be on its way to the worker";

        table.take_overdue(at(K_ACKNOWLEDGE_TIMEOUT + milliseconds(1)), limits, reaped);
        ASSERT_EQ(reaped.size(), 1u);
        EXPECT_EQ(reaped[0].cause, ReapCause::Unacknowledged);
    }

    TEST(Unit_PendingTasks, ChunkRestartsTheDeadline)
    {
        PendingTasks table;
        const sushi_ptr<Node> busy = make_sushi<Node>();
        const sushi_ptr<Node> idle = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(1000));

        ASSERT_TRUE(insert_plain(table, 1, *busy, K_WORKER));
        ASSERT_TRUE(insert_plain(table, 2, *idle, K_OTHER_WORKER));
        ASSERT_TRUE(table.acknowledge(1, K_WORKER));
        ASSERT_TRUE(table.acknowledge(2, K_OTHER_WORKER));

        table.note_chunk(K_WORKER, at(milliseconds(900)));

        std::vector<PendingTask> reaped;
        table.take_overdue(at(milliseconds(1000)), limits, reaped);
        EXPECT_TRUE(reaped.empty());

        table.take_overdue(at(milliseconds(1001)), limits, reaped);
        ASSERT_EQ(reaped.size(), 1u);
        EXPECT_EQ(reaped[0].task_id, 2u);
        EXPECT_EQ(reaped[0].cause, ReapCause::Deadline);

        table.take_overdue(at(milliseconds(1900)), limits, reaped);
        EXPECT_EQ(reaped.size(), 1u);

        table.take_overdue(at(milliseconds(1901)), limits, reaped);
        ASSERT_EQ(reaped.size(), 2u);
        EXPECT_EQ(reaped[1].task_id, 1u);
        EXPECT_EQ(reaped[1].node, busy.get());
        EXPECT_EQ(reaped[1].cause, ReapCause::Deadline);
    }

    TEST(Unit_PendingTasks, TakeForWorkerForgetsItsChunkTime)
    {
        PendingTasks table;
        const sushi_ptr<Node> first = make_sushi<Node>();
        const sushi_ptr<Node> second = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(1000));

        ASSERT_TRUE(insert_plain(table, 1, *first, K_WORKER));
        table.note_chunk(K_WORKER, at(milliseconds(50000)));

        std::vector<PendingTask> returned;
        table.take_for_worker(K_WORKER, returned);
        ASSERT_EQ(returned.size(), 1u);

        ASSERT_TRUE(insert_plain(table, 2, *second, K_WORKER, milliseconds(100)));
        ASSERT_TRUE(table.acknowledge(2, K_WORKER));

        std::vector<PendingTask> reaped;
        table.take_overdue(at(milliseconds(1101)), limits, reaped);
        ASSERT_EQ(reaped.size(), 1u) << "the deadline runs from the dispatch, not from the old chunk";
        EXPECT_EQ(reaped[0].task_id, 2u);
        EXPECT_EQ(reaped[0].cause, ReapCause::Deadline);
    }

    TEST(Unit_PendingTasks, TwoRemovalsOfOneTaskHaveOneWinner)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();

        for (int round = 0; round < K_RACE_ROUNDS; ++round)
        {
            const std::uint64_t task_id = table.allocate_task_id();
            ASSERT_TRUE(insert_plain(table, task_id, *node, K_WORKER));

            StartGate gate;
            std::optional<PendingTask> by_result;
            std::vector<PendingTask> by_loss;

            std::thread result_thread([&]
            {
                gate.arrive_and_wait();
                by_result = table.take(task_id, K_WORKER);
            });
            std::thread loss_thread([&]
            {
                gate.arrive_and_wait();
                table.take_for_worker(K_WORKER, by_loss);
            });
            result_thread.join();
            loss_thread.join();

            const std::size_t winners = (by_result.has_value() ? 1u : 0u) + by_loss.size();
            ASSERT_EQ(winners, 1u) << "round " << round;
            const PendingTask& won = by_result.has_value() ? *by_result : by_loss.front();
            EXPECT_EQ(won.node, node.get());
            EXPECT_EQ(won.task_id, task_id);
            ASSERT_EQ(table.size(), 0u);
        }
    }
} // namespace PendingTasksTests

/** @brief Tests WorkerDirectory: the handshake decisions, the peer gate, liveness and pick(). */
namespace WorkerDirectoryTests
{
    using namespace SushiRuntime::Distributed;

    namespace
    {
        using std::chrono::milliseconds;

        /** @brief Names the first peer of a test. */
        constexpr PeerID K_FIRST{1};

        /** @brief Names the second peer of a test. */
        constexpr PeerID K_SECOND{2};

        /** @brief Names the third peer of a test. */
        constexpr PeerID K_THIRD{3};

        /** @brief Names a peer no test adds to the directory. */
        constexpr PeerID K_STRANGER{9};

        /** @brief Holds a wait no test outlasts; a wake that never comes hangs the test. */
        constexpr milliseconds K_UNTIL_WOKEN = std::chrono::hours(24);

        /** @brief Names an operation only the first worker of a test runs. */
        constexpr OperationID K_RARE_OPERATION{101};

        /** @brief Names an operation every worker of a test runs. */
        constexpr OperationID K_COMMON_OPERATION{202};

        /** @brief Builds a node tagged with the operation every worker of these tests holds. */
        sushi_ptr<Internal::Graph::Node> common_node()
        {
            sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
            node->metadata.op_id = K_COMMON_OPERATION;
            return node;
        }

        /** @brief Names an operation no worker of a test runs. */
        constexpr OperationID K_ABSENT_OPERATION{303};

        /** @brief Returns a fixed time point @p offset after the tests' zero. */
        std::chrono::steady_clock::time_point at(milliseconds offset)
        {
            return std::chrono::steady_clock::time_point(std::chrono::hours(1)) + offset;
        }

        /** @brief Returns a nonce whose bytes all hold @p value. */
        HandshakeNonce nonce_of(std::uint8_t value)
        {
            HandshakeNonce nonce{};
            nonce.bytes.fill(value);
            return nonce;
        }

        /** @brief Returns handshake octets as the bytes a wire message carries. */
        template <std::size_t Count>
        std::array<std::byte, Count> to_wire(const std::array<std::uint8_t, Count>& octets)
        {
            std::array<std::byte, Count> bytes{};
            std::transform(octets.begin(), octets.end(), bytes.begin(),
                           [](std::uint8_t octet) { return std::byte{octet}; });
            return bytes;
        }

        /** @brief Names the build every HELLO of a test carries unless the test names another. */
        constexpr const char* K_BUILD = "test-build";

        /** @brief Returns the HELLO of a worker called "worker" with the given nonce and build. */
        Wire::HelloMessage hello_with(const HandshakeNonce& worker_nonce,
                                      const char* build_id = K_BUILD)
        {
            Wire::HelloMessage hello;
            hello.worker_nonce = to_wire(worker_nonce.bytes);
            hello.build_id = build_id;
            hello.node_name = "worker";
            return hello;
        }

        /** @brief Returns the PROOF a worker holding @p secret sends for the two nonces. */
        Wire::ProofMessage proof_with(span<const std::uint8_t> secret,
                                      const HandshakeNonce& worker_nonce,
                                      const HandshakeNonce& master_nonce,
                                      std::vector<OperationID> operations, std::uint32_t max_jobs)
        {
            Wire::ProofMessage proof;
            proof.worker_proof = to_wire(
                compute_proof(HandshakeRole::Worker, secret, worker_nonce, master_nonce));
            proof.ops = std::move(operations);
            proof.max_jobs = max_jobs;
            return proof;
        }

        /** @brief Returns the type bytes of the messages the directory admits from a peer. */
        std::vector<int> admitted_types(const WorkerDirectory& directory, PeerID peer)
        {
            std::vector<int> admitted;
            for (int type = K_FIRST_MESSAGE_TYPE; type <= K_LAST_MESSAGE_TYPE; ++type)
            {
                if (directory.admits(peer, static_cast<MessageType>(type)))
                    admitted.push_back(type);
            }
            return admitted;
        }

        /** @brief Returns the type bytes of the given messages. */
        std::vector<int> type_bytes(const std::vector<MessageType>& types)
        {
            std::vector<int> bytes;
            for (const MessageType type : types)
                bytes.push_back(static_cast<int>(type));
            return bytes;
        }

        /** @brief Returns the values of the given peer ids. */
        std::vector<std::uint32_t> values_of(const std::vector<PeerID>& peers)
        {
            std::vector<std::uint32_t> values;
            for (const PeerID peer : peers)
                values.push_back(peer.value);
            return values;
        }

        /** @brief Picks the first worker it is offered and records every call. */
        class RecordingPolicy final : public OffloadPolicy
        {
            public:
                /** @brief Records the offer, then returns @ref answer or the first worker. */
                std::optional<int> pick_worker(const NodeOptions&, span<const WorkerLoad> workers,
                                               std::uint64_t) override
                {
                    ++calls;
                    offered.assign(workers.begin(), workers.end());
                    if (keeps_local)
                        return std::nullopt;
                    if (answer.has_value())
                        return answer;
                    return workers.front().worker_id;
                }

                /** @brief Returns the ids of the workers of the last call. */
                [[nodiscard]] std::vector<int> offered_ids() const
                {
                    std::vector<int> ids;
                    for (const WorkerLoad& worker : offered)
                        ids.push_back(worker.worker_id);
                    return ids;
                }

                /** @brief Counts the calls. */
                int calls = 0;

                /** @brief Holds the workers of the last call. */
                std::vector<WorkerLoad> offered;

                /** @brief Holds the id to answer with in place of the first worker. */
                std::optional<int> answer;

                /** @brief Makes every call keep the task local. */
                bool keeps_local = false;
        };

        /** @brief Builds a directory over a 32-byte secret and a recording policy. */
        class Unit_WorkerDirectory : public ::testing::Test
        {
            protected:
                /** @brief Fills the secret with the bytes 0 to 31 and builds the directory. */
                Unit_WorkerDirectory()
                    : secret_(make_secret()),
                      policy_(std::make_shared<RecordingPolicy>()),
                      directory_(secret(), policy_)
                {

                }

                /** @brief Returns the secret the directory verifies proofs with. */
                [[nodiscard]] span<const std::uint8_t> secret() const noexcept
                {
                    return span<const std::uint8_t>(secret_.data(), secret_.size());
                }

                /** @brief Connects a peer and takes it through HELLO and PROOF at time @p now. */
                HandshakeStep register_worker(PeerID peer, std::vector<OperationID> operations,
                                              std::uint32_t max_jobs,
                                              std::chrono::steady_clock::time_point now)
                {
                    const HandshakeNonce worker_nonce =
                        nonce_of(static_cast<std::uint8_t>(0x10 + peer.value));
                    const HandshakeNonce master_nonce =
                        nonce_of(static_cast<std::uint8_t>(0x80 + peer.value));

                    directory_.add_peer(peer, "10.0.0." + std::to_string(peer.value) + ":7000", now);
                    const HelloReply reply =
                        directory_.accept_hello(peer, hello_with(worker_nonce), master_nonce);
                    if (reply.step != HandshakeStep::Accepted)
                        return reply.step;

                    return directory_.accept_proof(
                        peer,
                        proof_with(secret(), worker_nonce, master_nonce, std::move(operations),
                                   max_jobs),
                        now);
                }

                /** @brief Holds the cluster key. */
                std::vector<std::uint8_t> secret_;

                /** @brief Holds the policy the directory asks. */
                std::shared_ptr<RecordingPolicy> policy_;

                /** @brief Holds the directory under test. */
                WorkerDirectory directory_;

            private:
                /** @brief Returns the bytes 0 to 31. */
                static std::vector<std::uint8_t> make_secret()
                {
                    std::vector<std::uint8_t> bytes(32);
                    for (std::size_t index = 0; index < bytes.size(); ++index)
                        bytes[index] = static_cast<std::uint8_t>(index);
                    return bytes;
                }
        };
    } // namespace

    TEST_F(Unit_WorkerDirectory, UnregisteredPeerMessagesDropped)
    {
        const HandshakeNonce worker_nonce = nonce_of(0x11);
        const HandshakeNonce master_nonce = nonce_of(0x22);

        EXPECT_TRUE(admitted_types(directory_, K_STRANGER).empty());

        directory_.add_peer(K_FIRST, "10.0.0.1:7000", at(milliseconds(0)));
        EXPECT_EQ(admitted_types(directory_, K_FIRST),
                  type_bytes({MessageType::Hello, MessageType::Goodbye}));

        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(worker_nonce), master_nonce).step,
                  HandshakeStep::Accepted);
        EXPECT_EQ(admitted_types(directory_, K_FIRST),
                  type_bytes({MessageType::Hello, MessageType::Proof, MessageType::Goodbye}));
        EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::BufferRequest));
        EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::BufferChunk));
        EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::TaskResult));

        ASSERT_EQ(directory_.accept_proof(
                      K_FIRST, proof_with(secret(), worker_nonce, master_nonce, {}, 4),
                      at(milliseconds(1))),
                  HandshakeStep::Accepted);

        std::vector<int> every_type_but_proof;
        for (int type = K_FIRST_MESSAGE_TYPE; type <= K_LAST_MESSAGE_TYPE; ++type)
        {
            if (type != static_cast<int>(MessageType::Proof))
                every_type_but_proof.push_back(type);
        }
        EXPECT_EQ(admitted_types(directory_, K_FIRST), every_type_but_proof);

        const RemovedPeer removed = directory_.remove_peer(K_FIRST);
        EXPECT_TRUE(removed.known);
        EXPECT_TRUE(admitted_types(directory_, K_FIRST).empty());
    }

    TEST_F(Unit_WorkerDirectory, HandshakeDeadlineClosesPeer)
    {
        const milliseconds limit(5000);

        directory_.add_peer(K_FIRST, "10.0.0.1:7000", at(milliseconds(0)));
        ASSERT_EQ(register_worker(K_SECOND, {K_COMMON_OPERATION}, 4, at(milliseconds(0))), HandshakeStep::Accepted);
        directory_.add_peer(K_THIRD, "10.0.0.3:7000", at(milliseconds(400)));

        // A peer that keeps sending HELLO still owes PROOF, and its time runs from the connect.
        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(nonce_of(1)), nonce_of(2)).step,
                  HandshakeStep::Accepted);
        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(nonce_of(3)), nonce_of(4)).step,
                  HandshakeStep::Accepted);

        std::vector<PeerID> overdue;
        directory_.take_handshake_overdue(at(milliseconds(5000)), limit, overdue);
        EXPECT_TRUE(overdue.empty()) << "the limit itself is still within the limit";

        directory_.take_handshake_overdue(at(milliseconds(5001)), limit, overdue);
        EXPECT_EQ(values_of(overdue), (std::vector<std::uint32_t>{1}));

        directory_.take_handshake_overdue(at(milliseconds(5400)), limit, overdue);
        EXPECT_EQ(values_of(overdue), (std::vector<std::uint32_t>{1}));

        directory_.take_handshake_overdue(at(milliseconds(5401)), limit, overdue);
        EXPECT_EQ(values_of(overdue), (std::vector<std::uint32_t>{1, 3}));

        directory_.take_handshake_overdue(at(milliseconds(900000)), limit, overdue);
        EXPECT_EQ(overdue.size(), 2u) << "each peer is listed once and a worker never";

        const RemovedPeer removed = directory_.remove_peer(K_FIRST);
        EXPECT_TRUE(removed.known);
        EXPECT_FALSE(removed.registered);
        EXPECT_EQ(removed.name, "worker");
        EXPECT_EQ(removed.endpoint, "10.0.0.1:7000");
        EXPECT_FALSE(directory_.remove_peer(K_FIRST).known);
        EXPECT_EQ(directory_.alive_count(), 1u);
    }

    TEST_F(Unit_WorkerDirectory, WaitForWorkersWakesOnRegistration)
    {
        EXPECT_TRUE(directory_.wait_for_workers(0, milliseconds(0)));
        EXPECT_FALSE(directory_.wait_for_workers(1, milliseconds(0)));

        std::atomic<bool> reached{false};
        std::thread waiter([&] { reached = directory_.wait_for_workers(1, K_UNTIL_WOKEN); });
        const HandshakeStep step = register_worker(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0)));
        waiter.join();

        EXPECT_EQ(step, HandshakeStep::Accepted);
        EXPECT_TRUE(reached.load());
        EXPECT_TRUE(directory_.wait_for_workers(1, milliseconds(0)));

        std::atomic<bool> reached_two{true};
        std::thread second_waiter(
            [&] { reached_two = directory_.wait_for_workers(2, K_UNTIL_WOKEN); });
        directory_.close();
        second_waiter.join();

        EXPECT_FALSE(reached_two.load()) << "close() wakes a waiter whose count was not reached";
    }

    TEST_F(Unit_WorkerDirectory, WrongProofIsRejected)
    {
        const HandshakeNonce worker_nonce = nonce_of(0x11);
        const HandshakeNonce master_nonce = nonce_of(0x22);
        const std::vector<std::uint8_t> other_secret(32, 0xee);
        const span<const std::uint8_t> other(other_secret.data(), other_secret.size());

        directory_.add_peer(K_FIRST, "10.0.0.1:7000", at(milliseconds(0)));
        const HelloReply reply =
            directory_.accept_hello(K_FIRST, hello_with(worker_nonce), master_nonce);
        ASSERT_EQ(reply.step, HandshakeStep::Accepted);
        EXPECT_EQ(reply.master_proof,
                  compute_proof(HandshakeRole::Master, secret(), worker_nonce, master_nonce));

        EXPECT_EQ(directory_.accept_proof(
                      K_FIRST, proof_with(other, worker_nonce, master_nonce, {}, 4),
                      at(milliseconds(1))),
                  HandshakeStep::AuthenticationFailed);
        EXPECT_EQ(directory_.alive_count(), 0u);
        EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::TaskResult));
        EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::Proof));

        // The refused proof spent the nonces, so the right proof for them comes too late.
        EXPECT_EQ(directory_.accept_proof(
                      K_FIRST, proof_with(secret(), worker_nonce, master_nonce, {}, 4),
                      at(milliseconds(2))),
                  HandshakeStep::OutOfOrder);

        const HelloReply second =
            directory_.accept_hello(K_FIRST, hello_with(worker_nonce), master_nonce);
        ASSERT_EQ(second.step, HandshakeStep::Accepted);
        Wire::ProofMessage replayed;
        replayed.worker_proof = to_wire(second.master_proof);
        replayed.max_jobs = 4;
        EXPECT_EQ(directory_.accept_proof(K_FIRST, replayed, at(milliseconds(3))),
                  HandshakeStep::AuthenticationFailed)
            << "the master's own proof does not pass as the worker's";

        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(worker_nonce), master_nonce).step,
                  HandshakeStep::Accepted);
        EXPECT_EQ(directory_.accept_proof(
                      K_FIRST,
                      proof_with(span<const std::uint8_t>(), worker_nonce, master_nonce, {}, 4),
                      at(milliseconds(4))),
                  HandshakeStep::AuthenticationFailed)
            << "a worker without the secret file proves under an empty key";

        EXPECT_EQ(directory_.alive_count(), 0u);
        EXPECT_FALSE(directory_.remove_peer(K_FIRST).registered);
    }

    TEST_F(Unit_WorkerDirectory, SecondHelloRestartsTheHandshake)
    {
        const HandshakeNonce first_worker_nonce = nonce_of(0x11);
        const HandshakeNonce first_master_nonce = nonce_of(0x22);
        const HandshakeNonce second_worker_nonce = nonce_of(0x33);
        const HandshakeNonce second_master_nonce = nonce_of(0x44);

        EXPECT_EQ(directory_.accept_hello(K_STRANGER, hello_with(first_worker_nonce),
                                          first_master_nonce).step,
                  HandshakeStep::UnknownPeer);
        EXPECT_EQ(directory_.accept_proof(
                      K_STRANGER,
                      proof_with(secret(), first_worker_nonce, first_master_nonce, {}, 4),
                      at(milliseconds(0))),
                  HandshakeStep::UnknownPeer);

        directory_.add_peer(K_FIRST, "10.0.0.1:7000", at(milliseconds(0)));
        directory_.add_peer(K_SECOND, "10.0.0.2:7000", at(milliseconds(0)));
        for (const PeerID peer : {K_FIRST, K_SECOND})
        {
            const HelloReply first =
                directory_.accept_hello(peer, hello_with(first_worker_nonce), first_master_nonce);
            const HelloReply second =
                directory_.accept_hello(peer, hello_with(second_worker_nonce), second_master_nonce);
            ASSERT_EQ(first.step, HandshakeStep::Accepted);
            ASSERT_EQ(second.step, HandshakeStep::Accepted);
            EXPECT_NE(first.master_proof, second.master_proof);
            EXPECT_EQ(second.master_proof,
                      compute_proof(HandshakeRole::Master, secret(), second_worker_nonce,
                                    second_master_nonce));
        }

        EXPECT_EQ(directory_.accept_proof(
                      K_FIRST,
                      proof_with(secret(), first_worker_nonce, first_master_nonce, {}, 4),
                      at(milliseconds(1))),
                  HandshakeStep::AuthenticationFailed)
            << "a proof for the nonces of the first HELLO";
        EXPECT_EQ(directory_.accept_proof(
                      K_SECOND,
                      proof_with(secret(), second_worker_nonce, second_master_nonce, {}, 4),
                      at(milliseconds(1))),
                  HandshakeStep::Accepted);
        EXPECT_EQ(directory_.alive_count(), 1u);

        // A registered worker owes no handshake frame, and sending one costs it nothing.
        EXPECT_EQ(directory_.accept_hello(K_SECOND, hello_with(first_worker_nonce),
                                          first_master_nonce).step,
                  HandshakeStep::AlreadyRegistered);
        EXPECT_EQ(directory_.accept_proof(
                      K_SECOND,
                      proof_with(secret(), second_worker_nonce, second_master_nonce, {}, 4),
                      at(milliseconds(2))),
                  HandshakeStep::OutOfOrder);
        EXPECT_EQ(directory_.alive_count(), 1u);
        EXPECT_TRUE(directory_.admits(K_SECOND, MessageType::TaskResult));
    }

    TEST_F(Unit_WorkerDirectory, SilentWorkerIsTakenOnce)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();
        const milliseconds liveness(500);

        ASSERT_EQ(register_worker(K_FIRST, {K_RARE_OPERATION, K_COMMON_OPERATION}, 4,
                                  at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_EQ(register_worker(K_SECOND, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        EXPECT_EQ(directory_.alive_count(), 2u);
        EXPECT_TRUE(directory_.supports(K_RARE_OPERATION));
        EXPECT_TRUE(directory_.supports(K_COMMON_OPERATION));
        EXPECT_FALSE(directory_.supports(K_ABSENT_OPERATION));

        std::vector<PeerID> silent;
        directory_.take_silent(at(milliseconds(500)), liveness, silent);
        EXPECT_TRUE(silent.empty()) << "the limit itself is still within the limit";

        directory_.note_frame(K_SECOND, at(milliseconds(900)));
        directory_.take_silent(at(milliseconds(1000)), liveness, silent);
        EXPECT_EQ(values_of(silent), (std::vector<std::uint32_t>{1}));
        EXPECT_EQ(directory_.alive_count(), 1u);

        directory_.take_silent(at(milliseconds(1000)), liveness, silent);
        EXPECT_EQ(silent.size(), 1u) << "a worker already taken is not listed again";

        std::vector<PeerID> workers;
        directory_.list_workers(workers);
        EXPECT_EQ(values_of(workers), (std::vector<std::uint32_t>{2}));
        EXPECT_FALSE(directory_.supports(K_RARE_OPERATION));
        EXPECT_TRUE(directory_.supports(K_COMMON_OPERATION));

        const std::optional<PeerID> picked = directory_.pick(*node, 1024);
        ASSERT_TRUE(picked.has_value());
        EXPECT_EQ(picked->value, 2u);
        EXPECT_EQ(policy_->offered_ids(), (std::vector<int>{2}));

        // A late frame does not bring the worker back; only a new connection registers.
        directory_.note_frame(K_FIRST, at(milliseconds(1001)));
        EXPECT_EQ(directory_.alive_count(), 1u);

        const RemovedPeer removed = directory_.remove_peer(K_FIRST);
        EXPECT_TRUE(removed.known);
        EXPECT_TRUE(removed.registered);
        EXPECT_EQ(directory_.alive_count(), 1u) << "the worker stopped counting when it was taken";

        directory_.take_silent(at(milliseconds(1401)), liveness, silent);
        EXPECT_EQ(values_of(silent), (std::vector<std::uint32_t>{1, 2}));
        EXPECT_EQ(directory_.alive_count(), 0u);
        EXPECT_FALSE(directory_.supports(K_COMMON_OPERATION));
        EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
    }

    TEST_F(Unit_WorkerDirectory, PickSkipsWorkersAtTheirJobLimit)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();

        EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
        EXPECT_EQ(policy_->calls, 0) << "the policy is not asked when no worker has room";

        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 1, at(milliseconds(0))), HandshakeStep::Accepted);
        ASSERT_EQ(register_worker(K_SECOND, {K_COMMON_OPERATION}, 2, at(milliseconds(0))), HandshakeStep::Accepted);

        std::optional<PeerID> picked = directory_.pick(*node, 1024);
        ASSERT_TRUE(picked.has_value());
        EXPECT_EQ(picked->value, 1u);
        EXPECT_EQ(policy_->offered_ids(), (std::vector<int>{1, 2}));

        picked = directory_.pick(*node, 1024);
        ASSERT_TRUE(picked.has_value());
        EXPECT_EQ(picked->value, 2u);
        EXPECT_EQ(policy_->offered_ids(), (std::vector<int>{2}));
        ASSERT_EQ(policy_->offered.size(), 1u);
        EXPECT_TRUE(policy_->offered[0].alive);
        EXPECT_EQ(policy_->offered[0].inflight, 0u);

        picked = directory_.pick(*node, 1024);
        ASSERT_TRUE(picked.has_value());
        EXPECT_EQ(picked->value, 2u);
        EXPECT_EQ(policy_->offered[0].inflight, 1u);
        EXPECT_EQ(policy_->calls, 3);

        EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
        EXPECT_EQ(policy_->calls, 3) << "both workers are at the limit they sent in PROOF";

        directory_.release_inflight(K_FIRST);
        directory_.release_inflight(K_FIRST);
        picked = directory_.pick(*node, 1024);
        ASSERT_TRUE(picked.has_value());
        EXPECT_EQ(picked->value, 1u) << "one release frees one slot, and a second lowers nothing";
        EXPECT_FALSE(directory_.pick(*node, 1024).has_value());

        // An answer the policy was not offered counts as no pick and raises no count.
        directory_.release_inflight(K_SECOND);
        policy_->answer = 1;
        EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
        policy_->answer = 77;
        EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
        policy_->answer.reset();

        policy_->keeps_local = true;
        EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
        policy_->keeps_local = false;

        directory_.close();
        const int calls_before_close = policy_->calls;
        EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
        EXPECT_EQ(policy_->calls, calls_before_close) << "a closed directory asks nobody";
    }

    /** @brief Tests that pick() offers a node only to a worker that holds its kernel. */
    TEST_F(Unit_WorkerDirectory, PickOffersOnlyWorkersWithTheKernel)
    {
        using std::chrono::milliseconds;

        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_EQ(register_worker(K_SECOND, {K_RARE_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);

        const sushi_ptr<Internal::Graph::Node> rare = make_sushi<Internal::Graph::Node>();
        rare->metadata.op_id = K_RARE_OPERATION;
        for (int round = 0; round < 4; ++round)
        {
            const std::optional<PeerID> picked = directory_.pick(*rare, 1024);
            ASSERT_TRUE(picked.has_value());
            EXPECT_EQ(picked->value, K_SECOND.value) << "the task went to a worker without its kernel";
        }

        // The only worker with the kernel is full; the other one is not a fallback.
        EXPECT_FALSE(directory_.pick(*rare, 1024).has_value());

        const sushi_ptr<Internal::Graph::Node> untagged = make_sushi<Internal::Graph::Node>();
        EXPECT_FALSE(directory_.pick(*untagged, 1024).has_value())
            << "a node with no operation was offered to a worker";
    }

    TEST_F(Unit_WorkerDirectory, SnapshotReportsWhatTheWorkerSent)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();

        directory_.add_peer(K_FIRST, "10.0.0.1:7000", at(milliseconds(0)));
        ASSERT_EQ(register_worker(K_SECOND, {K_COMMON_OPERATION}, 4, at(milliseconds(100))),
                  HandshakeStep::Accepted);

        directory_.note_pong(K_SECOND, 250, 4096);
        directory_.note_heartbeat(K_SECOND, 3, 8192);
        directory_.note_frame(K_SECOND, at(milliseconds(400)));
        directory_.note_pong(K_FIRST, 1, 1);
        ASSERT_TRUE(directory_.pick(*node, 1024).has_value());

        std::vector<WorkerSnapshot> workers;
        directory_.snapshot(at(milliseconds(1000)), workers);

        ASSERT_EQ(workers.size(), 1u) << "a peer that has not finished the handshake is no worker";
        EXPECT_EQ(workers[0].id, 2u);
        EXPECT_EQ(workers[0].name, "worker");
        EXPECT_EQ(workers[0].endpoint, "10.0.0.2:7000");
        EXPECT_EQ(workers[0].inflight, 1u);
        EXPECT_EQ(workers[0].rtt_micros, 250u);
        EXPECT_EQ(workers[0].free_mem, 4096u);
        EXPECT_EQ(workers[0].queue_depth, 3u);
        EXPECT_EQ(workers[0].cache_bytes, 8192u);
        EXPECT_EQ(workers[0].ms_since_last_frame, 600u);
    }

    TEST_F(Unit_WorkerDirectory, IdentityCarriesTheBuildOfTheLastHello)
    {
        const HandshakeNonce first_worker_nonce = nonce_of(0x11);
        const HandshakeNonce first_master_nonce = nonce_of(0x22);
        const HandshakeNonce second_worker_nonce = nonce_of(0x33);
        const HandshakeNonce second_master_nonce = nonce_of(0x44);

        EXPECT_FALSE(directory_.identity(K_STRANGER).has_value());

        directory_.add_peer(K_FIRST, "10.0.0.1:7000", at(milliseconds(0)));
        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(first_worker_nonce, "build-a"),
                                          first_master_nonce).step,
                  HandshakeStep::Accepted);
        EXPECT_FALSE(directory_.identity(K_FIRST).has_value())
            << "a peer that owes PROOF is no worker yet";

        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(second_worker_nonce, "build-b"),
                                          second_master_nonce).step,
                  HandshakeStep::Accepted);
        ASSERT_EQ(directory_.accept_proof(
                      K_FIRST,
                      proof_with(secret(), second_worker_nonce, second_master_nonce, {}, 4),
                      at(milliseconds(1))),
                  HandshakeStep::Accepted);

        const std::optional<WorkerIdentity> worker = directory_.identity(K_FIRST);
        ASSERT_TRUE(worker.has_value());
        EXPECT_EQ(worker->name, "worker");
        EXPECT_EQ(worker->endpoint, "10.0.0.1:7000");
        EXPECT_EQ(worker->build_id, "build-b");

        // A registered worker's HELLO leaves what it registered with.
        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(first_worker_nonce, "build-c"),
                                          first_master_nonce).step,
                  HandshakeStep::AlreadyRegistered);
        EXPECT_EQ(directory_.identity(K_FIRST)->build_id, "build-b");

        EXPECT_TRUE(directory_.remove_peer(K_FIRST).registered);
        EXPECT_FALSE(directory_.identity(K_FIRST).has_value());
    }

    TEST_F(Unit_WorkerDirectory, HelloFromARegisteredWorkerRegistersNothingTwice)
    {
        ASSERT_EQ(register_worker(K_FIRST, {K_RARE_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        const sushi_ptr<Internal::Graph::Node> rare = make_sushi<Internal::Graph::Node>();
        rare->metadata.op_id = K_RARE_OPERATION;
        ASSERT_TRUE(directory_.pick(*rare, 0).has_value());

        // The worker lost its WELCOME and says HELLO again with a nonce of its own choosing.
        ASSERT_TRUE(directory_.admits(K_FIRST, MessageType::Hello));
        const HelloReply again =
            directory_.accept_hello(K_FIRST, hello_with(nonce_of(0x77), "build-z"), nonce_of(0x88));

        EXPECT_EQ(again.step, HandshakeStep::AlreadyRegistered);
        EXPECT_EQ(directory_.alive_count(), 1u);
        EXPECT_TRUE(directory_.supports(K_RARE_OPERATION));
        EXPECT_TRUE(directory_.admits(K_FIRST, MessageType::TaskResult));
        EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::Proof))
            << "the repeated HELLO reopened the handshake";

        std::vector<WorkerSnapshot> workers;
        directory_.snapshot(at(milliseconds(1)), workers);
        ASSERT_EQ(workers.size(), 1u);
        EXPECT_EQ(workers[0].id, K_FIRST.value);
        EXPECT_EQ(workers[0].inflight, 1u) << "the repeated HELLO forgot a task that is out";
        EXPECT_EQ(directory_.identity(K_FIRST)->build_id, K_BUILD);
    }

    /** @brief Tests that close() leaves no worker alive, no operation supported and no way in. */
    TEST_F(Unit_WorkerDirectory, CloseRetiresEveryWorker)
    {
        ASSERT_EQ(register_worker(K_FIRST, {K_RARE_OPERATION, K_COMMON_OPERATION}, 4,
                                  at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_EQ(register_worker(K_SECOND, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_TRUE(directory_.wait_for_workers(2, milliseconds(0)));

        directory_.close();

        EXPECT_EQ(directory_.alive_count(), 0u);
        EXPECT_FALSE(directory_.supports(K_RARE_OPERATION));
        EXPECT_FALSE(directory_.supports(K_COMMON_OPERATION));
        EXPECT_FALSE(directory_.wait_for_workers(2, milliseconds(100)))
            << "a closed directory reported the workers it held when it closed";
        EXPECT_FALSE(directory_.wait_for_workers(0, milliseconds(0)))
            << "a closed directory reached a count";

        std::vector<PeerID> listed;
        directory_.list_workers(listed);
        EXPECT_TRUE(listed.empty());

        std::vector<WorkerSnapshot> workers;
        directory_.snapshot(at(milliseconds(1)), workers);
        EXPECT_TRUE(workers.empty());

        EXPECT_EQ(register_worker(K_THIRD, {K_COMMON_OPERATION}, 4, at(milliseconds(2))),
                  HandshakeStep::OutOfOrder)
            << "a peer registered with a closed directory";
        EXPECT_EQ(directory_.alive_count(), 0u);

        // The transport still reports each loss, and the session it ends was a worker's.
        const RemovedPeer removed = directory_.remove_peer(K_FIRST);
        EXPECT_TRUE(removed.registered);
        EXPECT_EQ(directory_.alive_count(), 0u) << "remove_peer() uncounted a worker twice";

        directory_.close();
        EXPECT_EQ(directory_.alive_count(), 0u) << "a second close() uncounted a worker twice";
    }
} // namespace WorkerDirectoryTests
