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
 * @file test_frame_sending.cpp
 * @brief Tests send_message(): one control frame of the message's type, whose payload decodes back.
 * @see tests/extension/frame_transport/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/in_process_transport/in_process_transport.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/recording_transport_events.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

#include <SushiRuntime/frame_transport/frame_sending.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Asks a recorder for what it holds now, without waiting. */
    constexpr std::chrono::milliseconds K_NO_WAIT{0};

    /** @brief Holds the token the PING of these tests carries. */
    constexpr std::uint64_t K_TOKEN = 99;

    /** @brief Joins a master endpoint and a worker endpoint, both threadless, on one fabric. */
    class Small_FrameSending : public ::testing::Test
    {
        protected:
            /** @brief Starts both endpoints and steps the executor until each sees the other. */
            void SetUp() override
            {
                InProcessMasterOptions master_options;
                master_options.executor = executor_;
                master_options.clock = clock_;
                InProcessWorkerOptions worker_options;
                worker_options.executor = executor_;
                worker_options.clock = clock_;
                master_ = InProcessTransport::make_master(fabric_, master_options);
                worker_ = InProcessTransport::make_worker(fabric_, worker_options);
                ASSERT_TRUE(master_->start(master_events_).is_ok());
                ASSERT_TRUE(worker_->start(worker_events_).is_ok());
                settle();
                ASSERT_TRUE(master_events_.wait_for_connections(1, K_NO_WAIT));
                ASSERT_TRUE(worker_events_.wait_for_connections(1, K_NO_WAIT));
            }

            /** @brief Stops both endpoints, so no job outlives the recorders. */
            void TearDown() override
            {
                worker_->stop();
                master_->stop();
            }

            /** @brief Runs every posted pump round, so each queued frame is delivered. */
            void settle()
            {
                executor_->run_all();
            }

            /** @brief Returns the PING the tests send. */
            static Wire::PingMessage ping()
            {
                Wire::PingMessage message;
                message.token = K_TOKEN;
                return message;
            }

            /** @brief Returns the token a received PING frame carries. */
            static std::uint64_t token_of(const RecordedFrame& frame)
            {
                Wire::PingMessage decoded;
                EXPECT_TRUE(Wire::PingMessage::decode(
                                span<const std::byte>(frame.payload.data(), frame.payload.size()),
                                decoded)
                                .ok());
                return decoded.token;
            }

            /// @brief Runs the pump rounds of both endpoints on the stepping thread.
            std::shared_ptr<SeededExecutor> executor_ = std::make_shared<SeededExecutor>(1);

            /// @brief Times the endpoints; no test advances it.
            std::shared_ptr<const Clock> clock_ = std::make_shared<ManualClock>();

            /// @brief The fabric the two endpoints share.
            InProcessFabric fabric_;

            /// @brief What the master endpoint reports; it outlives the endpoint.
            RecordingTransportEvents master_events_;

            /// @brief What the worker endpoint reports; it outlives the endpoint.
            RecordingTransportEvents worker_events_;

            /// @brief The endpoint the messages leave from.
            std::shared_ptr<InProcessTransport> master_;

            /// @brief The endpoint the messages arrive at.
            std::shared_ptr<InProcessTransport> worker_;
    };
} // namespace

/** @brief Tests that a PING leaves as one frame of its type whose payload carries its token. */
TEST_F(Small_FrameSending, AMessageLeavesAsOneFrameOfItsType)
{
    std::vector<std::byte> scratch;

    EXPECT_EQ(send_message(*master_, master_events_.connected().front(), ping(), scratch),
              SendStatus::Queued);

    settle();
    ASSERT_TRUE(worker_events_.wait_for_frames(MessageType::Ping, 1, K_NO_WAIT));
    const std::vector<RecordedFrame> frames = worker_events_.frames();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames.front().type, MessageType::Ping);
    EXPECT_EQ(frames.front().peer, worker_events_.connected().front());
    EXPECT_EQ(token_of(frames.front()), K_TOKEN);
}

/** @brief Tests that the form that owns its buffer queues the same frame. */
TEST_F(Small_FrameSending, TheFormWithoutABufferQueuesTheSameFrame)
{
    EXPECT_EQ(send_message(*master_, master_events_.connected().front(), ping()),
              SendStatus::Queued);

    settle();
    ASSERT_TRUE(worker_events_.wait_for_frames(MessageType::Ping, 1, K_NO_WAIT));
    EXPECT_EQ(token_of(worker_events_.frames_of(MessageType::Ping).front()), K_TOKEN);
}

/** @brief Tests that a peer the transport does not know is reported, not queued. */
TEST_F(Small_FrameSending, AnUnknownPeerIsReportedAsTheTransportSaidIt)
{
    EXPECT_EQ(send_message(*master_, PeerID{999}, ping()), SendStatus::UnknownPeer);
}
