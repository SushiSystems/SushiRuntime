/**************************************************************************/
/* test_in_process_transport.cpp                                          */
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
 * @file test_in_process_transport.cpp
 * @brief Tests InProcessTransport: a fabric delivers frames both ways.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Names the bytes of a payload or a frame. */
    using Bytes = std::vector<std::byte>;

    /** @brief Holds how long a transport test waits for an event. */
    constexpr std::chrono::seconds K_EVENT_LIMIT{10};

    /** @brief Returns a read-only view of a byte vector. */
    span<const std::byte> view_of(const Bytes& bytes)
    {
        return span<const std::byte>(bytes.data(), bytes.size());
    }

    /** @brief Holds one frame a transport delivered. */
    struct ReceivedFrame
    {
        /** @brief The peer the frame came from. */
        PeerID peer;

        /** @brief The type the header carried. */
        MessageType type = MessageType::Hello;

        /** @brief The flag byte the header carried. */
        std::uint8_t flags = 0;

        /** @brief A copy of the payload. */
        Bytes payload;
    };

    /**
     * @brief Records the connections and the frames of one endpoint and wakes a waiting test.
     */
    class RecordingEvents final : public TransportEvents
    {
        public:
            /** @brief Records the peer of a connection that opened. */
            void on_peer_connected(PeerID peer, const PeerDescription& /*info*/) override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    connected_.push_back(peer);
                }
                changed_.notify_all();
            }

            /** @brief Records a copy of a frame. */
            void on_frame(PeerID peer, MessageType type, std::uint8_t flags,
                          span<const std::byte> payload) override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    frames_.push_back(
                        ReceivedFrame{peer, type, flags, Bytes(payload.begin(), payload.end())});
                }
                changed_.notify_all();
            }

            /** @brief Ignores a loss; the test stops both endpoints itself. */
            void on_peer_lost(PeerID /*peer*/, const PeerLoss& /*loss*/) override
            {

            }

            /** @brief Ignores an abandoned connect; the test sets no connect timeout. */
            void on_connect_abandoned(const std::string& /*cause*/) override
            {

            }

            /** @brief Waits for the first connection and returns its peer, or nothing. */
            [[nodiscard]] std::optional<PeerID> wait_for_peer()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!changed_.wait_for(lock, K_EVENT_LIMIT, [&] { return !connected_.empty(); }))
                    return std::nullopt;
                return connected_.front();
            }

            /** @brief Waits for the first frame and returns it, or nothing. */
            [[nodiscard]] std::optional<ReceivedFrame> wait_for_frame()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!changed_.wait_for(lock, K_EVENT_LIMIT, [&] { return !frames_.empty(); }))
                    return std::nullopt;
                return frames_.front();
            }

        private:
            /// @brief Guards the two lists.
            std::mutex mutex_;

            /// @brief Wakes a waiting test on every event.
            std::condition_variable changed_;

            /// @brief The peers that connected, in order.
            std::vector<PeerID> connected_;

            /// @brief The frames that arrived, in order.
            std::vector<ReceivedFrame> frames_;
    };
}

/** @brief Tests that frames of both directions arrive with the sender's peer id and type. */
TEST(Unit_DistributedTest, InProcessTransportDeliversBothDirections)
{
    RecordingEvents master_events;
    RecordingEvents worker_events;
    InProcessFabric fabric;
    const std::shared_ptr<InProcessTransport> master = InProcessTransport::make_master(fabric);
    const std::shared_ptr<InProcessTransport> worker = InProcessTransport::make_worker(fabric);
    EXPECT_EQ(fabric.worker_count(), 1u);
    EXPECT_EQ(worker->worker_index(), 0u);

    ASSERT_TRUE(master->start(master_events).is_ok());
    ASSERT_TRUE(worker->start(worker_events).is_ok());

    const std::optional<PeerID> worker_as_seen = master_events.wait_for_peer();
    const std::optional<PeerID> master_as_seen = worker_events.wait_for_peer();
    ASSERT_TRUE(worker_as_seen.has_value());
    ASSERT_TRUE(master_as_seen.has_value());
    EXPECT_TRUE(worker_as_seen->valid());
    EXPECT_TRUE(master_as_seen->valid());

    const Bytes outbound = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    const Bytes inbound = {std::byte{0x0A}, std::byte{0x0B}};
    ASSERT_EQ(master->send(*worker_as_seen, MessageType::TaskDispatch, view_of(outbound)),
              SendStatus::Queued);
    ASSERT_EQ(worker->send(*master_as_seen, MessageType::TaskResult, view_of(inbound)),
              SendStatus::Queued);

    const std::optional<ReceivedFrame> at_worker = worker_events.wait_for_frame();
    const std::optional<ReceivedFrame> at_master = master_events.wait_for_frame();
    ASSERT_TRUE(at_worker.has_value());
    ASSERT_TRUE(at_master.has_value());

    EXPECT_EQ(at_worker->peer, *master_as_seen);
    EXPECT_EQ(at_worker->type, MessageType::TaskDispatch);
    EXPECT_EQ(at_worker->flags, 0u);
    EXPECT_EQ(at_worker->payload, outbound);

    EXPECT_EQ(at_master->peer, *worker_as_seen);
    EXPECT_EQ(at_master->type, MessageType::TaskResult);
    EXPECT_EQ(at_master->flags, 0u);
    EXPECT_EQ(at_master->payload, inbound);

    worker->stop();
    master->stop();
}

