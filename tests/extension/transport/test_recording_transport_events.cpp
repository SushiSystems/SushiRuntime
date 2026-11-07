/**************************************************************************/
/* test_recording_transport_events.cpp                                    */
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
 * @file test_recording_transport_events.cpp
 * @brief Tests RecordingTransportEvents: what it records, in what order, and its waits.
 * @see tests/extension/transport/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/recording_transport_events.hpp>
#include <SushiRuntime/transport/transport.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Names the wait of a test that expects its count to be reached already. */
    constexpr std::chrono::milliseconds K_NO_WAIT{0};

    /** @brief Hands one frame to a receiver through its port, as a transport does. */
    void deliver(TransportEvents& events, PeerID peer, MessageType type, std::uint8_t flags,
                 const std::vector<std::byte>& payload)
    {
        events.on_frame(peer, type, flags,
                        span<const std::byte>(payload.data(), payload.size()));
    }
} // namespace

/** @brief Tests that a frame is recorded with its peer, type, flags and a copy of its payload. */
TEST(Small_RecordingTransportEvents, RecordsAFrameWithACopyOfItsPayload)
{
    RecordingTransportEvents events;
    std::vector<std::byte> payload{std::byte{1}, std::byte{2}, std::byte{3}};
    deliver(events, PeerID{4}, MessageType::Ping, 1, payload);
    payload.assign(3, std::byte{9});

    const std::vector<RecordedFrame> frames = events.frames();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].peer, PeerID{4});
    EXPECT_EQ(frames[0].type, MessageType::Ping);
    EXPECT_EQ(frames[0].flags, 1u);
    EXPECT_EQ(frames[0].payload, (std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}}));
}

/** @brief Tests that frames_of() and frame_count() select one type and keep arrival order. */
TEST(Small_RecordingTransportEvents, SelectsFramesOfOneTypeInArrivalOrder)
{
    RecordingTransportEvents events;
    deliver(events, PeerID{1}, MessageType::Ping, 0, {std::byte{1}});
    deliver(events, PeerID{1}, MessageType::Pong, 0, {std::byte{2}});
    deliver(events, PeerID{1}, MessageType::Ping, 0, {std::byte{3}});

    const std::vector<RecordedFrame> pings = events.frames_of(MessageType::Ping);
    ASSERT_EQ(pings.size(), 2u);
    EXPECT_EQ(pings[0].payload, std::vector<std::byte>{std::byte{1}});
    EXPECT_EQ(pings[1].payload, std::vector<std::byte>{std::byte{3}});
    EXPECT_EQ(events.frame_count(MessageType::Ping), 2u);
    EXPECT_EQ(events.frame_count(MessageType::Hello), 0u);
    EXPECT_EQ(events.frames().size(), 3u);
}

/** @brief Tests that connections open and end in the order the transport reported them. */
TEST(Small_RecordingTransportEvents, RecordsConnectionsAndLossesInOrder)
{
    RecordingTransportEvents events;
    TransportEvents& port = events;
    PeerLoss closed;
    closed.cause = PeerLossCause::ClosedByPeer;
    PeerLoss failed;
    failed.cause = PeerLossCause::IOFailed;
    failed.text = "reset";

    port.on_peer_connected(PeerID{1}, PeerDescription{});
    port.on_peer_connected(PeerID{2}, PeerDescription{});
    port.on_peer_lost(PeerID{2}, failed);
    port.on_peer_lost(PeerID{1}, closed);

    EXPECT_EQ(events.connected(), (std::vector<PeerID>{PeerID{1}, PeerID{2}}));
    const std::vector<RecordedLoss> losses = events.losses();
    ASSERT_EQ(losses.size(), 2u);
    EXPECT_EQ(losses[0].peer, PeerID{2});
    EXPECT_EQ(losses[0].loss.cause, PeerLossCause::IOFailed);
    EXPECT_EQ(losses[0].loss.text, "reset");
    EXPECT_EQ(losses[1].peer, PeerID{1});
    EXPECT_EQ(losses[1].loss.cause, PeerLossCause::ClosedByPeer);
}

/** @brief Tests that the cause of an abandoned connect is recorded. */
TEST(Small_RecordingTransportEvents, RecordsTheCauseOfAnAbandonedConnect)
{
    RecordingTransportEvents events;
    TransportEvents& port = events;

    port.on_connect_abandoned("connection refused by 127.0.0.1:9");

    EXPECT_EQ(events.abandoned(), std::vector<std::string>{"connection refused by 127.0.0.1:9"});
}

/** @brief Tests that each wait is true once its count is reached and false below it. */
TEST(Small_RecordingTransportEvents, AWaitIsTrueExactlyFromItsCount)
{
    RecordingTransportEvents events;
    TransportEvents& port = events;
    port.on_peer_connected(PeerID{1}, PeerDescription{});
    port.on_peer_lost(PeerID{1}, PeerLoss{});
    deliver(events, PeerID{1}, MessageType::Ping, 0, {});

    EXPECT_TRUE(events.wait_for_connections(1, K_NO_WAIT));
    EXPECT_FALSE(events.wait_for_connections(2, K_NO_WAIT));
    EXPECT_TRUE(events.wait_for_losses(1, K_NO_WAIT));
    EXPECT_FALSE(events.wait_for_losses(2, K_NO_WAIT));
    EXPECT_TRUE(events.wait_for_frames(MessageType::Ping, 1, K_NO_WAIT));
    EXPECT_FALSE(events.wait_for_frames(MessageType::Ping, 2, K_NO_WAIT));
    EXPECT_FALSE(events.wait_for_frames(MessageType::Pong, 1, K_NO_WAIT));
}
