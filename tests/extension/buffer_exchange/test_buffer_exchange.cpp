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
 * @file test_buffer_exchange.cpp
 * @brief Tests BufferExchange: chunking, reassembly, aborts and its statistics.
 * @see tests/extension/buffer_exchange/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

#include <SushiRuntime/buffer_exchange/buffer_exchange.hpp>
#include <SushiRuntime/buffer_exchange/buffer_registry.hpp>

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
        constexpr std::uint32_t K_CHUNK_BYTES = 4u * 1024u;

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

        /** @brief Returns how many chunk frames a buffer of the given size is served in. */
        std::size_t expected_frame_count(std::size_t size)
        {
            return size == 0 ? 1 : (size + K_CHUNK_BYTES - 1) / K_CHUNK_BYTES;
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

        /** @brief Serves a pull of a whole buffer and returns every frame it produced. */
        std::vector<SentFrame> pull_all(BufferExchange& exchange, const BufferHandle& handle)
        {
            OutboundFrame frame;
            const std::unique_ptr<FrameSource> stream =
                exchange.serve_pull(handle.id, handle.version);
            return drain(*stream, frame);
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
            chunk.data = {payload.data() + offset,
                          std::min<std::size_t>(K_CHUNK_BYTES, size - offset)};
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
        void expect_abort(const SentFrame& frame, const BufferHandle& handle,
                          Wire::AbortReason reason)
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

        /** @brief Checks each frame is a pull-reply chunk of the buffer and joins their data. */
        void expect_pull_frames_and_join(const std::vector<SentFrame>& frames,
                                         const BufferHandle& pulled, Bytes& joined)
        {
            joined.clear();
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
                EXPECT_EQ(chunk.handle.size_bytes, pulled.size_bytes);
                EXPECT_EQ(chunk.offset, joined.size());
                EXPECT_LE(chunk.data.size(), K_CHUNK_BYTES);
                EXPECT_EQ(frames[index].payload.size(),
                          Wire::BufferChunkMessage::K_PREFIX_BYTES + chunk.data.size());
                joined.insert(joined.end(), chunk.data.begin(), chunk.data.end());
            }
        }

        /** @brief Sends each pulled chunk back as a task-output chunk for another buffer. */
        void expect_frames_apply_as_output(BufferExchange& exchange,
                                           const std::vector<SentFrame>& frames,
                                           const BufferHandle& written)
        {
            for (std::size_t index = 0; index < frames.size(); ++index)
            {
                const bool last = index + 1 == frames.size();
                Wire::BufferChunkMessage chunk;
                ASSERT_TRUE(
                    Wire::BufferChunkMessage::decode(view_of(frames[index].payload), chunk).ok());
                chunk.purpose = Wire::ChunkPurpose::TaskOutput;
                chunk.task_id = K_TASK_ID;
                chunk.handle = written;
                EXPECT_EQ(exchange.apply_output_chunk(K_WORKER, chunk, last),
                          last ? ChunkStatus::Completed : ChunkStatus::Applied);
            }
        }

        /** @brief Checks only the final frame carries the last-chunk flag and ends the stream. */
        void expect_only_the_final_frame_is_last(const std::vector<SentFrame>& frames)
        {
            ASSERT_FALSE(frames.empty());
            for (std::size_t index = 0; index + 1 < frames.size(); ++index)
            {
                EXPECT_EQ(frames[index].status, FrameSourceStatus::Frame);
                EXPECT_EQ(frames[index].flags, 0);
            }
            EXPECT_EQ(frames.back().status, FrameSourceStatus::FinalFrame);
            EXPECT_EQ(frames.back().type, MessageType::BufferChunk);
            EXPECT_EQ(frames.back().flags, Wire::K_FRAME_FLAG_LAST_CHUNK);
        }
    } // namespace

    /** @brief Runs a test once per buffer size on and around a chunk boundary. */
    class Small_BufferExchangeBoundary : public ::testing::TestWithParam<std::size_t>
    {
    };

    TEST_P(Small_BufferExchangeBoundary, ChunkedRoundTrip)
    {
        const std::size_t size = GetParam();
        Bytes source = guarded_pattern(size, 7);
        Bytes destination(size + 1, K_GUARD);
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle pulled = registry.register_buffer(source.data(), size);
        const BufferHandle written = registry.register_buffer(destination.data(), size);

        const std::vector<SentFrame> frames = pull_all(exchange, pulled);
        ASSERT_EQ(frames.size(), expected_frame_count(size));
        Bytes reassembled;
        expect_pull_frames_and_join(frames, pulled, reassembled);
        expect_frames_apply_as_output(exchange, frames, written);

        reassembled.push_back(K_GUARD);
        EXPECT_EQ(reassembled, source);
        EXPECT_EQ(destination, source);
        EXPECT_TRUE(exchange.finish_task(K_TASK_ID, {&written, 1}));
        EXPECT_FALSE(exchange.finish_task(K_TASK_ID, {&written, 1}));
        const BufferExchangeStatistics statistics = exchange.statistics();
        EXPECT_EQ(statistics.chunks_sent, expected_frame_count(size));
        EXPECT_EQ(statistics.chunks_received, expected_frame_count(size));
        EXPECT_EQ(statistics.transfers_aborted, 0u);
        EXPECT_EQ(statistics.stale_writebacks, 0u);
    }

    TEST_P(Small_BufferExchangeBoundary, EveryStreamEndsWithALastChunk)
    {
        const std::size_t size = GetParam();
        Bytes source = guarded_pattern(size, 11);
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(source.data(), size);

        const std::vector<SentFrame> frames = pull_all(exchange, handle);

        expect_only_the_final_frame_is_last(frames);
    }

    TEST_P(Small_BufferExchangeBoundary, ACallAfterTheFinalFrameFinishesAndLeavesTheFrameAlone)
    {
        const std::size_t size = GetParam();
        Bytes source = guarded_pattern(size, 11);
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(source.data(), size);
        OutboundFrame frame;
        const std::unique_ptr<FrameSource> stream = exchange.serve_pull(handle.id, handle.version);
        drain(*stream, frame);
        const Bytes final_payload = frame.payload;

        EXPECT_EQ(stream->next(frame), FrameSourceStatus::Finished);

        EXPECT_EQ(frame.payload, final_payload);
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Small_BufferExchangeBoundary,
                             ::testing::ValuesIn(K_BOUNDARY_SIZES));

    namespace
    {
        /** @brief Holds a chunk version that differs from the version the buffer is at. */
        struct ForgedVersionRow
        {
            const char* meaning; /**< How the chunk's version differs. */
            int added;           /**< Added to the buffer's version. */
        };

        /** @brief Names a row by its meaning in a test report. */
        std::string name_of_forged(const ::testing::TestParamInfo<ForgedVersionRow>& info)
        {
            return info.param.meaning;
        }

        const ForgedVersionRow K_FORGED_VERSIONS[] = {{"OneVersionAhead", 1},
                                                      {"OneVersionBehind", -1}};
    } // namespace

    /** @brief Runs a test once per way a chunk's version can disagree with the buffer's. */
    class Small_BufferExchangeForgedVersion : public ::testing::TestWithParam<ForgedVersionRow>
    {
    };

    TEST_P(Small_BufferExchangeForgedVersion, ChunkIsStaleAndWritesNothing)
    {
        const std::size_t size = 2 * K_CHUNK_BYTES;
        const Bytes untouched = guarded_pattern(size, 23);
        const Bytes payload = guarded_pattern(size, 59);
        Bytes destination = untouched;
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        BufferHandle forged = registry.register_buffer(destination.data(), size);
        forged.version = static_cast<std::uint32_t>(forged.version + GetParam().added);

        EXPECT_EQ(apply_piece(exchange, forged, payload, 0), ChunkStatus::Stale);

        EXPECT_EQ(destination, untouched);
        EXPECT_EQ(exchange.statistics().stale_writebacks, 1u);
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Small_BufferExchangeForgedVersion,
                             ::testing::ValuesIn(K_FORGED_VERSIONS), name_of_forged);

    TEST(Small_BufferExchangeTest, ABumpWhileAChunkIsInFlightMakesItStale)
    {
        const std::size_t size = 2 * K_CHUNK_BYTES;
        const Bytes untouched = guarded_pattern(size, 23);
        const Bytes payload = guarded_pattern(size, 59);
        Bytes destination = untouched;
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        BufferHandle dispatched = registry.register_buffer(destination.data(), size);
        ASSERT_EQ(apply_piece(exchange, dispatched, payload, 0), ChunkStatus::Applied);
        // A reaper bumps a task's outputs before it resubmits; the chunk in flight is then late.
        BufferHandle bumped = dispatched;
        registry.bump({&bumped, 1});

        EXPECT_EQ(apply_piece(exchange, dispatched, payload, 1), ChunkStatus::Stale);

        EXPECT_TRUE(std::equal(destination.begin() + K_CHUNK_BYTES, destination.end(),
                               untouched.begin() + K_CHUNK_BYTES));
        EXPECT_FALSE(exchange.finish_task(K_TASK_ID, {&dispatched, 1}));
        EXPECT_EQ(exchange.statistics().stale_writebacks, 1u);
        EXPECT_EQ(exchange.statistics().chunks_received, 1u);
    }

    TEST(Small_BufferExchangeTest, AGapRefusesTheMissingPieceAndTheOneAfterIt)
    {
        const std::size_t size = 3 * K_CHUNK_BYTES + 7;
        const Bytes payload = guarded_pattern(size, 5);
        Bytes destination(size + 1, K_GUARD);
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(destination.data(), size);
        ASSERT_EQ(apply_piece(exchange, handle, payload, 0), ChunkStatus::Applied);

        EXPECT_EQ(apply_piece(exchange, handle, payload, 2), ChunkStatus::Gap);

        // Piece 1 was the next expected one; the gap discarded the transfer, so it is refused too.
        EXPECT_EQ(apply_piece(exchange, handle, payload, 1), ChunkStatus::Gap);
        EXPECT_FALSE(exchange.finish_task(K_TASK_ID, {&handle, 1}));
    }

    TEST(Small_BufferExchangeTest, ATransferRestartsFromPieceZeroAfterAGap)
    {
        const std::size_t size = 3 * K_CHUNK_BYTES + 7;
        const Bytes payload = guarded_pattern(size, 5);
        Bytes destination(size + 1, K_GUARD);
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(destination.data(), size);
        ASSERT_EQ(apply_piece(exchange, handle, payload, 0), ChunkStatus::Applied);
        ASSERT_EQ(apply_piece(exchange, handle, payload, 2), ChunkStatus::Gap);

        EXPECT_EQ(apply_piece(exchange, handle, payload, 0), ChunkStatus::Applied);
        EXPECT_EQ(apply_piece(exchange, handle, payload, 1), ChunkStatus::Applied);
        EXPECT_EQ(apply_piece(exchange, handle, payload, 2), ChunkStatus::Applied);
        EXPECT_EQ(apply_piece(exchange, handle, payload, 3), ChunkStatus::Completed);

        EXPECT_TRUE(exchange.finish_task(K_TASK_ID, {&handle, 1}));
        EXPECT_EQ(destination, payload);
    }

    TEST(Small_BufferExchangeTest, ADuplicateChunkDiscardsTheTransfer)
    {
        const std::size_t size = 3 * K_CHUNK_BYTES;
        const Bytes payload = guarded_pattern(size, 3);
        Bytes destination(size + 1, K_GUARD);
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(destination.data(), size);
        ASSERT_EQ(apply_piece(exchange, handle, payload, 0), ChunkStatus::Applied);
        ASSERT_EQ(apply_piece(exchange, handle, payload, 1), ChunkStatus::Applied);

        EXPECT_EQ(apply_piece(exchange, handle, payload, 1), ChunkStatus::Gap);

        EXPECT_EQ(apply_piece(exchange, handle, payload, 2), ChunkStatus::Gap);
        EXPECT_FALSE(exchange.finish_task(K_TASK_ID, {&handle, 1}));
        EXPECT_EQ(exchange.statistics().chunks_received, 2u);
    }

    TEST(Small_BufferExchangeTest, ReleasingABufferDuringAStreamAbortsItWithGone)
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
        ASSERT_EQ(registry.unregister_buffer(storage.get()), handle.id);
        storage.reset();

        const std::vector<SentFrame> rest = drain(*stream, frame);

        ASSERT_EQ(rest.size(), 1u);
        expect_abort(rest.front(), handle, Wire::AbortReason::Gone);
        EXPECT_EQ(stream->next(frame), FrameSourceStatus::Finished);
        EXPECT_EQ(exchange.statistics().chunks_sent, 1u);
        EXPECT_EQ(exchange.statistics().transfers_aborted, 1u);
    }

    TEST(Small_BufferExchangeTest, APullOfAnUnknownBufferIsRefusedWithGone)
    {
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        BufferHandle unknown;
        unknown.id.value = 999;
        unknown.version = 1;

        const std::vector<SentFrame> only = pull_all(exchange, unknown);

        ASSERT_EQ(only.size(), 1u);
        expect_abort(only.front(), unknown, Wire::AbortReason::Gone);
        EXPECT_EQ(exchange.statistics().transfers_aborted, 1u);
    }

    TEST(Small_BufferExchangeTest, ABumpDuringAStreamAbortsItWithStale)
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
        EXPECT_EQ(exchange.statistics().chunks_sent, 1u);
        EXPECT_EQ(exchange.statistics().transfers_aborted, 1u);
    }

    TEST(Small_BufferExchangeTest, APullNamingAVersionAlreadyLeftGetsTheAbortAlone)
    {
        const std::size_t size = 3 * K_CHUNK_BYTES;
        Bytes source = guarded_pattern(size, 13);
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(source.data(), size);
        void* const address = source.data();
        registry.bump_addresses({&address, 1});

        const std::vector<SentFrame> only = pull_all(exchange, handle);

        ASSERT_EQ(only.size(), 1u);
        expect_abort(only.front(), handle, Wire::AbortReason::Stale);
        EXPECT_EQ(exchange.statistics().chunks_sent, 0u);
    }

    namespace
    {
        /** @brief Holds a chunk that is malformed in one way, and whether it is flagged last. */
        struct MalformedRow
        {
            const char* meaning; /**< What is wrong with the chunk. */
            Wire::BufferChunkMessage (*build)(const BufferHandle&, const Bytes&); /**< Builds it. */
            bool last;                                                            /**< Its flag. */
        };

        /** @brief Names a row by its meaning in a test report. */
        std::string name_of_malformed(const ::testing::TestParamInfo<MalformedRow>& info)
        {
            return info.param.meaning;
        }

        /** @brief Returns a first piece whose purpose is a pull reply. */
        Wire::BufferChunkMessage pull_reply_purpose(const BufferHandle& handle, const Bytes& payload)
        {
            Wire::BufferChunkMessage chunk = output_chunk(handle, payload, 0);
            chunk.purpose = Wire::ChunkPurpose::PullReply;
            return chunk;
        }

        /** @brief Returns a first piece that declares one byte more than the buffer holds. */
        Wire::BufferChunkMessage larger_declared_size(const BufferHandle& handle,
                                                      const Bytes& payload)
        {
            BufferHandle larger = handle;
            larger.size_bytes = handle.size_bytes + 1;
            return output_chunk(larger, payload, 0);
        }

        /** @brief Returns a second piece shifted one byte past the buffer's end. */
        Wire::BufferChunkMessage runs_past_the_end(const BufferHandle& handle, const Bytes& payload)
        {
            Wire::BufferChunkMessage chunk = output_chunk(handle, payload, 1);
            chunk.offset += 1;
            return chunk;
        }

        /** @brief Returns the first piece unchanged. */
        Wire::BufferChunkMessage honest_first_piece(const BufferHandle& handle,
                                                    const Bytes& payload)
        {
            return output_chunk(handle, payload, 0);
        }

        const MalformedRow K_MALFORMED_CHUNKS[] = {
            {"PullReplyPurpose", pull_reply_purpose, false},
            {"DeclaredSizeOtherThanTheBuffers", larger_declared_size, false},
            {"RunningPastTheEnd", runs_past_the_end, false},
            {"FlaggedLastBeforeTheEnd", honest_first_piece, true},
        };
    } // namespace

    /** @brief Runs a test once per way a task-output chunk can be malformed. */
    class Small_BufferExchangeMalformedChunk : public ::testing::TestWithParam<MalformedRow>
    {
    };

    TEST_P(Small_BufferExchangeMalformedChunk, IsRefusedBeforeAnyWrite)
    {
        const std::size_t size = 2 * K_CHUNK_BYTES;
        const Bytes untouched = guarded_pattern(size, 17);
        const Bytes payload = guarded_pattern(size, 29);
        Bytes destination = untouched;
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(destination.data(), size);
        const Wire::BufferChunkMessage chunk = GetParam().build(handle, payload);

        EXPECT_EQ(exchange.apply_output_chunk(K_WORKER, chunk, GetParam().last),
                  ChunkStatus::Malformed);

        EXPECT_EQ(destination, untouched);
        EXPECT_EQ(exchange.statistics().chunks_received, 0u);
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Small_BufferExchangeMalformedChunk,
                             ::testing::ValuesIn(K_MALFORMED_CHUNKS), name_of_malformed);

    TEST(Small_BufferExchangeTest, ForgettingATaskDiscardsItsTransfer)
    {
        const std::size_t size = 2 * K_CHUNK_BYTES;
        const Bytes payload = guarded_pattern(size, 31);
        Bytes destination(size + 1, K_GUARD);
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(destination.data(), size);
        ASSERT_EQ(apply_piece(exchange, handle, payload, 0), ChunkStatus::Applied);

        exchange.forget_task(K_TASK_ID);

        EXPECT_EQ(apply_piece(exchange, handle, payload, 1), ChunkStatus::Gap);
        EXPECT_FALSE(exchange.finish_task(K_TASK_ID, {&handle, 1}));
    }

    TEST(Small_BufferExchangeTest, AContinuationFromAnotherConnectionIsRefused)
    {
        const std::size_t size = 2 * K_CHUNK_BYTES;
        const Bytes payload = guarded_pattern(size, 31);
        Bytes destination(size + 1, K_GUARD);
        BufferRegistry registry;
        BufferExchange exchange(registry, K_CHUNK_BYTES);
        const BufferHandle handle = registry.register_buffer(destination.data(), size);
        ASSERT_EQ(apply_piece(exchange, handle, payload, 0), ChunkStatus::Applied);
        const Wire::BufferChunkMessage foreign = output_chunk(handle, payload, 1);

        EXPECT_EQ(exchange.apply_output_chunk(PeerID{K_WORKER.value + 1}, foreign, true),
                  ChunkStatus::Gap);
    }
} // namespace BufferExchangeTests
