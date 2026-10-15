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
 * @file test_outbox.cpp
 * @brief Tests Outbox: control before stream, streams taking turns, capacity and close.
 * @see tests/extension/frame_transport/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>

#include <SushiRuntime/frame_transport/outbox.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /**
     * @brief Yields a fixed number of tagged frames and records its own destruction.
     */
    class ScriptedSource final : public FrameSource
    {
        public:
            /**
             * @param tag First payload byte of every frame.
             * @param frames Number of frames; the last is returned as FinalFrame.
             * @param destroyed Set by the destructor; outlives the source.
             */
            ScriptedSource(std::uint8_t tag, std::uint32_t frames, std::atomic<bool>& destroyed)
                : tag_(tag), frames_(frames), destroyed_(destroyed)
            {

            }

            /** @brief Records the destruction. */
            ~ScriptedSource() override
            {
                destroyed_.store(true);
            }

            /** @brief Writes the tag and the frame's index. */
            [[nodiscard]] FrameSourceStatus next(OutboundFrame& frame) noexcept override
            {
                if (next_ >= frames_)
                    return FrameSourceStatus::Finished;
                frame.type = MessageType::BufferChunk;
                frame.flags = 0;
                frame.payload.assign(
                    {static_cast<std::byte>(tag_), static_cast<std::byte>(next_)});
                ++next_;
                return next_ == frames_ ? FrameSourceStatus::FinalFrame
                                        : FrameSourceStatus::Frame;
            }

        private:
            /// @brief First payload byte of every frame.
            std::uint8_t tag_;

            /// @brief Number of frames the source yields.
            std::uint32_t frames_;

            /// @brief Index of the frame the next call writes.
            std::uint32_t next_ = 0;

            /// @brief Set when the source is destroyed.
            std::atomic<bool>& destroyed_;
    };

    /** @brief Pops one frame without waiting and returns its payload bytes as numbers. */
    std::vector<int> pop_bytes(Outbox& outbox, PopStatus& status)
    {
        OutboundFrame frame;
        status = outbox.pop(frame, std::chrono::milliseconds(0));
        std::vector<int> bytes;
        if (status == PopStatus::Frame)
            for (const std::byte value : frame.payload)
                bytes.push_back(std::to_integer<int>(value));
        return bytes;
    }
}

/**
 * @brief Checks that pop() returns every control frame before any frame of a stream.
 */
TEST(Medium_OutboxTest, ControlComesBeforeStream)
{
    Outbox outbox(8);
    std::atomic<bool> is_destroyed{false};
    ASSERT_EQ(outbox.push_stream(
                  std::make_unique<ScriptedSource>(std::uint8_t{7}, 2u, is_destroyed)),
              SendStatus::Queued);

    const std::vector<std::byte> first = {std::byte{1}};
    const std::vector<std::byte> second = {std::byte{2}};
    ASSERT_EQ(outbox.push_control(MessageType::Ping, {first.data(), first.size()}),
              SendStatus::Queued);
    ASSERT_EQ(outbox.push_control(MessageType::Ping, {second.data(), second.size()}),
              SendStatus::Queued);
    EXPECT_EQ(outbox.control_depth(), 2u);
    EXPECT_EQ(outbox.stream_count(), 1u);

    PopStatus status = PopStatus::Closed;
    EXPECT_EQ(pop_bytes(outbox, status), (std::vector<int>{1}));
    EXPECT_EQ(pop_bytes(outbox, status), (std::vector<int>{2}));
    EXPECT_EQ(pop_bytes(outbox, status), (std::vector<int>{7, 0}));
    EXPECT_FALSE(is_destroyed.load());
    EXPECT_EQ(pop_bytes(outbox, status), (std::vector<int>{7, 1}));
    EXPECT_TRUE(is_destroyed.load());

    EXPECT_TRUE(pop_bytes(outbox, status).empty());
    EXPECT_EQ(status, PopStatus::Timeout);
}

/**
 * @brief Checks that streams are served one frame each in turn.
 */
TEST(Medium_OutboxTest, StreamsTakeTurns)
{
    Outbox outbox(8);
    std::atomic<bool> is_long_destroyed{false};
    std::atomic<bool> is_short_destroyed{false};
    ASSERT_EQ(outbox.push_stream(
                  std::make_unique<ScriptedSource>(std::uint8_t{1}, 3u, is_long_destroyed)),
              SendStatus::Queued);
    ASSERT_EQ(outbox.push_stream(
                  std::make_unique<ScriptedSource>(std::uint8_t{2}, 2u, is_short_destroyed)),
              SendStatus::Queued);

    PopStatus status = PopStatus::Closed;
    EXPECT_EQ(pop_bytes(outbox, status), (std::vector<int>{1, 0}));
    EXPECT_EQ(pop_bytes(outbox, status), (std::vector<int>{2, 0}));
    EXPECT_EQ(pop_bytes(outbox, status), (std::vector<int>{1, 1}));
    EXPECT_EQ(pop_bytes(outbox, status), (std::vector<int>{2, 1}));
    EXPECT_TRUE(is_short_destroyed.load());
    EXPECT_FALSE(is_long_destroyed.load());
    EXPECT_EQ(pop_bytes(outbox, status), (std::vector<int>{1, 2}));
    EXPECT_TRUE(is_long_destroyed.load());
    EXPECT_EQ(outbox.stream_count(), 0u);
}

/**
 * @brief Checks the bound of the control queue and of one payload.
 */
TEST(Medium_OutboxTest, QueueFullAtCapacity)
{
    Outbox outbox(2);
    EXPECT_EQ(outbox.push_control(MessageType::Ping, {}), SendStatus::Queued);
    EXPECT_EQ(outbox.push_control(MessageType::Ping, {}), SendStatus::Queued);
    EXPECT_EQ(outbox.push_control(MessageType::Ping, {}), SendStatus::QueueFull);
    EXPECT_EQ(outbox.control_depth(), 2u);

    PopStatus status = PopStatus::Closed;
    EXPECT_TRUE(pop_bytes(outbox, status).empty());
    EXPECT_EQ(status, PopStatus::Frame);
    EXPECT_EQ(outbox.push_control(MessageType::Ping, {}), SendStatus::Queued);

    const std::vector<std::byte> oversized(Wire::K_MAX_FRAME_PAYLOAD + 1);
    EXPECT_EQ(outbox.push_control(MessageType::Ping, {oversized.data(), oversized.size()}),
              SendStatus::TooLong);
    const std::vector<std::byte> largest(Wire::K_MAX_FRAME_PAYLOAD);
    EXPECT_EQ(outbox.push_control(MessageType::Ping, {largest.data(), largest.size()}),
              SendStatus::QueueFull);
}

/**
 * @brief Checks that close() keeps queued control frames, drops streams and refuses pushes.
 */
TEST(Medium_OutboxTest, CloseKeepsControlAndDropsStreams)
{
    Outbox outbox(8);
    std::atomic<bool> is_queued_destroyed{false};
    std::atomic<bool> is_late_destroyed{false};
    const std::vector<std::byte> goodbye = {std::byte{9}};
    ASSERT_EQ(outbox.push_control(MessageType::Goodbye, {goodbye.data(), goodbye.size()}),
              SendStatus::Queued);
    ASSERT_EQ(outbox.push_stream(
                  std::make_unique<ScriptedSource>(std::uint8_t{3}, 4u, is_queued_destroyed)),
              SendStatus::Queued);

    outbox.close();
    EXPECT_TRUE(is_queued_destroyed.load());
    EXPECT_EQ(outbox.stream_count(), 0u);
    EXPECT_EQ(outbox.push_control(MessageType::Ping, {}), SendStatus::UnknownPeer);
    EXPECT_EQ(outbox.push_stream(
                  std::make_unique<ScriptedSource>(std::uint8_t{4}, 1u, is_late_destroyed)),
              SendStatus::UnknownPeer);
    EXPECT_TRUE(is_late_destroyed.load());

    PopStatus status = PopStatus::Timeout;
    EXPECT_EQ(pop_bytes(outbox, status), (std::vector<int>{9}));
    EXPECT_EQ(status, PopStatus::Frame);
    EXPECT_TRUE(pop_bytes(outbox, status).empty());
    EXPECT_EQ(status, PopStatus::Closed);
}
