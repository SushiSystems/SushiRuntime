/**************************************************************************/
/* test_frame_sending.cpp                                                 */
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
 * @file test_frame_sending.cpp
 * @brief Tests send_message(): one control frame of the message's type, whose payload decodes back.
 * @see tests/extension/frame_transport/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

#include <SushiRuntime/frame_transport/frame_sending.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Keeps the last frame it was asked to send. */
    class LastFrameTransport final : public Transport
    {
        public:
            TransportResult start(TransportEvents&) override { return TransportResult{}; }
            void stop() noexcept override {}
            SendStatus send(PeerID peer, MessageType type, span<const std::byte> payload) override
            {
                last_peer = peer;
                last_type = type;
                last_payload.assign(payload.begin(), payload.end());
                ++sends;
                return SendStatus::Queued;
            }
            SendStatus send_stream(PeerID, std::unique_ptr<FrameSource>) override { return SendStatus::Stopped; }
            void close_peer(PeerID) noexcept override {}
            TransportExposure exposure() const noexcept override { return TransportExposure::Loopback; }
            TransportStatistics statistics() const noexcept override { return TransportStatistics{}; }

            PeerID last_peer{0};                   /**< The peer of the last frame. */
            MessageType last_type = MessageType::Hello; /**< The type of the last frame. */
            std::vector<std::byte> last_payload;   /**< The payload of the last frame. */
            int sends = 0;                         /**< The calls of send(). */
    };
}

/** @brief Tests that a PING leaves as one frame of its type whose payload carries its token. */
TEST(Medium_FrameSending, AMessageLeavesAsOneFrameOfItsType)
{
    LastFrameTransport transport;
    Wire::PingMessage ping;
    ping.token = 99;
    std::vector<std::byte> scratch;
    EXPECT_EQ(send_message(transport, PeerID{4}, ping, scratch), SendStatus::Queued);
    EXPECT_EQ(transport.sends, 1);
    EXPECT_EQ(transport.last_peer, PeerID{4});
    EXPECT_EQ(transport.last_type, MessageType::Ping);

    Wire::PingMessage decoded;
    ASSERT_TRUE(Wire::PingMessage::decode(
        span<const std::byte>(transport.last_payload.data(), transport.last_payload.size()), decoded).ok());
    EXPECT_EQ(decoded.token, 99u);
    EXPECT_EQ(send_message(transport, PeerID{4}, ping), SendStatus::Queued);
}
