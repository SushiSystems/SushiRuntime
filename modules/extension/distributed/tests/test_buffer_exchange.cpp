/**************************************************************************/
/* test_buffer_exchange.cpp                                               */
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
 * @file test_buffer_exchange.cpp
 * @brief Tests BufferExchange: chunked pulls, fenced output chunks and aborted streams.
 * @see modules/extension/distributed/tests/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include <SushiRuntime/distributed/cluster.hpp>

#include "buffer_exchange.hpp"
#include "buffer_registry.hpp"
#include "wire_frame.hpp"
#include "wire_messages.hpp"

using namespace SushiRuntime;
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
    constexpr PeerId K_WORKER{3};

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
    EXPECT_EQ(exchange.apply_output_chunk(PeerId{K_WORKER.value + 1}, foreign, true),
              ChunkStatus::Gap);
}
