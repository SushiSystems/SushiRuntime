/**************************************************************************/
/* test_outbox.cpp                                                        */
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
 * @file test_outbox.cpp
 * @brief Tests Outbox: control before stream, streams taking turns, capacity and close.
 * @see tests/extension/frame_transport/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

#include <SushiRuntime/buffer_exchange/buffer_exchange.hpp>
#include <SushiRuntime/buffer_exchange/buffer_registry.hpp>
#include <SushiRuntime/frame_transport/outbox.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Holds the data bytes per chunk of the exchange the streams come from: 4 KiB. */
    constexpr std::uint32_t K_CHUNK_BYTES = 4u * 1024u;

    /** @brief Names the buffer a stream frame belongs to and where the frame starts in it. */
    struct StreamPosition
    {
        std::uint64_t buffer = 0; /**< Id of the buffer the frame carries a piece of. */
        std::uint64_t offset = 0; /**< Offset of the piece in the buffer. */

        /** @brief Returns whether two positions name the same piece. */
        bool operator==(const StreamPosition& other) const
        {
            return buffer == other.buffer && offset == other.offset;
        }
    };

    /**
     * @brief Offers real pull streams over host buffers, the FrameSources the transports queue.
     */
    class PullStreams
    {
        public:
            PullStreams() : exchange_(registry_, K_CHUNK_BYTES)
            {
            }

            /** @brief Registers a buffer of the given chunk count and returns its pull stream. */
            [[nodiscard]] std::unique_ptr<FrameSource> stream_of_chunks(std::size_t chunks)
            {
                storage_.emplace_back(chunks * K_CHUNK_BYTES);
                const BufferHandle handle =
                    registry_.register_buffer(storage_.back().data(), storage_.back().size());
                return exchange_.serve_pull(handle.id, handle.version);
            }

            /** @brief Returns the id the registry gave to the buffer registered n-th, from 0. */
            [[nodiscard]] std::uint64_t id_of(std::size_t n)
            {
                const std::optional<BufferHandle> handle = registry_.lookup(storage_[n].data());
                return handle->id.value;
            }

        private:
            /// @brief Holds the bytes each stream reads; a moved vector keeps its heap block.
            std::vector<std::vector<std::byte>> storage_;

            /// @brief Holds the shared buffers.
            BufferRegistry registry_;

            /// @brief Serves the pulls.
            BufferExchange exchange_;
    };

    /** @brief Pops one frame without waiting and returns the status. */
    PopStatus pop_now(Outbox& outbox, OutboundFrame& frame)
    {
        return outbox.pop(frame, std::chrono::milliseconds(0));
    }

    /** @brief Pops one stream frame and returns the piece it carries. */
    StreamPosition pop_position(Outbox& outbox)
    {
        OutboundFrame frame;
        EXPECT_EQ(pop_now(outbox, frame), PopStatus::Frame);
        Wire::BufferChunkMessage chunk;
        EXPECT_TRUE(Wire::BufferChunkMessage::decode(
                        SushiRuntime::span<const std::byte>(frame.payload.data(),
                                                            frame.payload.size()),
                        chunk)
                        .ok());
        return StreamPosition{chunk.handle.id.value, chunk.offset};
    }

    /** @brief Pops one control frame and returns its first payload byte as a number. */
    int pop_control_tag(Outbox& outbox)
    {
        OutboundFrame frame;
        EXPECT_EQ(pop_now(outbox, frame), PopStatus::Frame);
        EXPECT_EQ(frame.type, MessageType::Ping);
        return frame.payload.empty() ? -1 : std::to_integer<int>(frame.payload.front());
    }

    /** @brief Pushes a control frame whose payload is one byte, the tag. */
    SendStatus push_tagged(Outbox& outbox, std::uint8_t tag)
    {
        const std::vector<std::byte> payload = {static_cast<std::byte>(tag)};
        return outbox.push_control(MessageType::Ping, {payload.data(), payload.size()});
    }
} // namespace

/** @brief Checks that pop() returns every control frame before any frame of a stream. */
TEST(Small_OutboxTest, ControlFramesComeBeforeAnyFrameOfAStream)
{
    PullStreams streams;
    Outbox outbox(8);
    ASSERT_EQ(outbox.push_stream(streams.stream_of_chunks(2)), SendStatus::Queued);
    ASSERT_EQ(push_tagged(outbox, 1), SendStatus::Queued);
    ASSERT_EQ(push_tagged(outbox, 2), SendStatus::Queued);
    EXPECT_EQ(outbox.control_depth(), 2u);
    EXPECT_EQ(outbox.stream_count(), 1u);

    EXPECT_EQ(pop_control_tag(outbox), 1);
    EXPECT_EQ(pop_control_tag(outbox), 2);
    EXPECT_EQ(pop_position(outbox), (StreamPosition{streams.id_of(0), 0}));
}

/** @brief Checks that pop() reports a timeout when nothing is queued. */
TEST(Small_OutboxTest, PopOnAnEmptyOutboxTimesOut)
{
    Outbox outbox(8);
    OutboundFrame frame;

    EXPECT_EQ(pop_now(outbox, frame), PopStatus::Timeout);
}

/** @brief Checks that a stream leaves the outbox with its final frame. */
TEST(Small_OutboxTest, AStreamLeavesTheOutboxWithItsFinalFrame)
{
    PullStreams streams;
    Outbox outbox(8);
    ASSERT_EQ(outbox.push_stream(streams.stream_of_chunks(2)), SendStatus::Queued);

    pop_position(outbox);
    EXPECT_EQ(outbox.stream_count(), 1u);
    pop_position(outbox);

    EXPECT_EQ(outbox.stream_count(), 0u);
}

/** @brief Checks that streams are served one frame each in turn. */
TEST(Small_OutboxTest, StreamsTakeTurns)
{
    PullStreams streams;
    Outbox outbox(8);
    ASSERT_EQ(outbox.push_stream(streams.stream_of_chunks(3)), SendStatus::Queued);
    ASSERT_EQ(outbox.push_stream(streams.stream_of_chunks(2)), SendStatus::Queued);
    const std::uint64_t long_id = streams.id_of(0);
    const std::uint64_t short_id = streams.id_of(1);

    EXPECT_EQ(pop_position(outbox), (StreamPosition{long_id, 0}));
    EXPECT_EQ(pop_position(outbox), (StreamPosition{short_id, 0}));
    EXPECT_EQ(pop_position(outbox), (StreamPosition{long_id, K_CHUNK_BYTES}));
    EXPECT_EQ(pop_position(outbox), (StreamPosition{short_id, K_CHUNK_BYTES}));
    EXPECT_EQ(pop_position(outbox), (StreamPosition{long_id, 2u * K_CHUNK_BYTES}));
    EXPECT_EQ(outbox.stream_count(), 0u);
}

/** @brief Checks that a full control queue refuses a frame and keeps its depth. */
TEST(Small_OutboxTest, TheControlQueueRefusesAFrameAtItsCapacity)
{
    Outbox outbox(2);
    ASSERT_EQ(push_tagged(outbox, 1), SendStatus::Queued);
    ASSERT_EQ(push_tagged(outbox, 2), SendStatus::Queued);

    EXPECT_EQ(push_tagged(outbox, 3), SendStatus::QueueFull);

    EXPECT_EQ(outbox.control_depth(), 2u);
}

/** @brief Checks that popping a control frame makes room for another. */
TEST(Small_OutboxTest, APopMakesRoomInTheControlQueue)
{
    Outbox outbox(2);
    ASSERT_EQ(push_tagged(outbox, 1), SendStatus::Queued);
    ASSERT_EQ(push_tagged(outbox, 2), SendStatus::Queued);
    ASSERT_EQ(push_tagged(outbox, 3), SendStatus::QueueFull);
    pop_control_tag(outbox);

    EXPECT_EQ(push_tagged(outbox, 3), SendStatus::Queued);
}

/** @brief Checks that a payload above the frame limit is too long whatever the queue holds. */
TEST(Small_OutboxTest, AnOversizedPayloadIsTooLongWhateverTheQueueHolds)
{
    Outbox outbox(1);
    ASSERT_EQ(push_tagged(outbox, 1), SendStatus::Queued);
    const std::vector<std::byte> oversized(Wire::K_MAX_FRAME_PAYLOAD + 1);

    EXPECT_EQ(outbox.push_control(MessageType::Ping, {oversized.data(), oversized.size()}),
              SendStatus::TooLong);
}

/** @brief Checks that a payload of exactly the frame limit is not too long. */
TEST(Small_OutboxTest, APayloadOfExactlyTheFrameLimitIsNotTooLong)
{
    Outbox outbox(1);
    ASSERT_EQ(push_tagged(outbox, 1), SendStatus::Queued);
    const std::vector<std::byte> largest(Wire::K_MAX_FRAME_PAYLOAD);

    EXPECT_EQ(outbox.push_control(MessageType::Ping, {largest.data(), largest.size()}),
              SendStatus::QueueFull);
}

/** @brief Checks that close() drops the queued streams. */
TEST(Small_OutboxTest, CloseDropsTheQueuedStreams)
{
    PullStreams streams;
    Outbox outbox(8);
    ASSERT_EQ(outbox.push_stream(streams.stream_of_chunks(4)), SendStatus::Queued);

    outbox.close();

    EXPECT_EQ(outbox.stream_count(), 0u);
}

/** @brief Checks that close() refuses every later push. */
TEST(Small_OutboxTest, CloseRefusesLaterPushes)
{
    PullStreams streams;
    Outbox outbox(8);
    outbox.close();

    EXPECT_EQ(push_tagged(outbox, 1), SendStatus::UnknownPeer);
    EXPECT_EQ(outbox.push_stream(streams.stream_of_chunks(1)), SendStatus::UnknownPeer);
}

/** @brief Checks that a control frame queued before close() still comes out, then pop() closes. */
TEST(Small_OutboxTest, ControlQueuedBeforeCloseStillComesOut)
{
    Outbox outbox(8);
    ASSERT_EQ(push_tagged(outbox, 9), SendStatus::Queued);
    outbox.close();

    EXPECT_EQ(pop_control_tag(outbox), 9);

    OutboundFrame frame;
    EXPECT_EQ(pop_now(outbox, frame), PopStatus::Closed);
}
