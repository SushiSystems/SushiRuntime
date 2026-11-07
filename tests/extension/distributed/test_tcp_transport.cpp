/**************************************************************************/
/* test_tcp_transport.cpp                                                 */
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
 * @file test_tcp_transport.cpp
 * @brief Tests TCPTransport against the Transport contract.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */


#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/distributed/tcp_transport.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>

#include "support/transport_contract.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using namespace TestSupport;

namespace
{
    /** @brief Number of frames the gated stream of ControlFrameOvertakesStream yields. */
    constexpr std::uint32_t K_STREAM_FRAMES = 8;

    /** @brief Builds a TCP master on a free loopback port and a worker that connects to it. */
    class TCPLoopbackRig final
    {
        public:
            /** @brief Returns the name the typed suite gives this rig. */
            static std::string name()
            {
                return "TCPLoopback";
            }

            /** @brief Creates the master endpoint on 127.0.0.1, port 0. */
            std::shared_ptr<Transport> make_master()
            {
                master_ = TCPTransport::make_master();
                return master_;
            }

            /**
             * @brief Creates a worker endpoint for the port the master bound.
             * @pre The master made by make_master() was started.
             */
            std::shared_ptr<Transport> make_worker(bool reconnect)
            {
                TCPWorkerOptions options;
                options.reconnect = reconnect;
                options.backoff_initial = std::chrono::milliseconds(10);
                return TCPTransport::make_worker(master_->local_endpoint(), options);
            }

            /** @brief Waits on the recorder until it holds @p count events of one kind. */
            [[nodiscard]] bool await(EventRecorder& recorder, EventKind kind, std::size_t count)
            {
                return recorder.wait_for_count(kind, count);
            }

        private:
            /// @brief The master, kept to read its port.
            std::shared_ptr<TCPTransport> master_;
    };

    using TCPRigs = ::testing::Types<TCPLoopbackRig>;

    /** @brief Holds what a GatedSource and its case share. */
    struct Gate
    {
        std::mutex mutex;                /**< Guards the two flags. */
        std::condition_variable changed; /**< Wakes the case and the source. */
        bool is_first_pulled = false;    /**< Set when the source wrote its first frame. */
        bool is_open = false;            /**< Set by the case to let the second frame out. */
    };

    /**
     * @brief Yields K_STREAM_FRAMES chunks and holds the second until the case opens the gate.
     *
     * The wait stands in for a writer that is busy with a chunk while a control frame is
     * queued; a production source never waits.
     */
    class GatedSource final : public FrameSource
    {
        public:
            /** @param gate Shared with the case, so a failed case leaves no dangling wait. */
            explicit GatedSource(std::shared_ptr<Gate> gate) : gate_(std::move(gate))
            {

            }

            /** @brief Writes the next chunk; the last carries the last-chunk flag. */
            [[nodiscard]] FrameSourceStatus next(OutboundFrame& frame) noexcept override
            {
                {
                    std::unique_lock<std::mutex> lock(gate_->mutex);
                    if (next_ == 1)
                        gate_->changed.wait_for(lock, K_EVENT_LIMIT,
                                                [this] { return gate_->is_open; });
                    gate_->is_first_pulled = true;
                }
                gate_->changed.notify_all();

                const bool is_last = next_ + 1 == K_STREAM_FRAMES;
                frame.type = MessageType::BufferChunk;
                frame.flags = is_last ? Wire::K_FRAME_FLAG_LAST_CHUNK : std::uint8_t{0};
                frame.payload = encode_index(next_);
                ++next_;
                return is_last ? FrameSourceStatus::FinalFrame : FrameSourceStatus::Frame;
            }

        private:
            /// @brief What the source shares with the case.
            std::shared_ptr<Gate> gate_;

            /// @brief Index of the chunk the next call writes.
            std::uint32_t next_ = 0;
    };

    /**
     * @brief Returns how many chunks of the stream precede the control frame, in the frames seen.
     * @param seen The master's events.
     * @return K_STREAM_FRAMES + 1 when no control frame was seen.
     */
    std::uint32_t chunks_before_the_control_frame(const std::vector<Event>& seen)
    {
        std::uint32_t chunks = 0;
        for (const Event& event : seen)
        {
            if (event.kind != EventKind::Frame)
                continue;
            if (event.type == MessageType::Heartbeat)
                return chunks;
            ++chunks;
        }
        return K_STREAM_FRAMES + 1;
    }

    /** @brief Expects the stream chunks among the frames seen to be in order, the last flagged. */
    void expect_chunks_in_order(const std::vector<Event>& seen)
    {
        std::uint32_t chunks = 0;
        for (const Event& event : seen)
        {
            if (event.kind != EventKind::Frame || event.type == MessageType::Heartbeat)
                continue;
            ASSERT_EQ(event.type, MessageType::BufferChunk);
            ASSERT_EQ(decode_index(event.payload), chunks);
            const bool is_last = chunks + 1 == K_STREAM_FRAMES;
            EXPECT_EQ(event.flags, is_last ? Wire::K_FRAME_FLAG_LAST_CHUNK : std::uint8_t{0});
            ++chunks;
        }
        EXPECT_EQ(chunks, K_STREAM_FRAMES);
    }
} // namespace

INSTANTIATE_TYPED_TEST_SUITE_P(Medium_TCPTransport, TransportContract, TCPRigs, RigNames);
INSTANTIATE_TYPED_TEST_SUITE_P(Medium_TCPTransport, ThreadedTransportContract, TCPRigs, RigNames);

/** @brief Runs the case that needs a TCP writer thread busy with a stream. */
class Medium_TCPTransportStreams : public TransportContract<TCPLoopbackRig>
{
};

/** @brief Checks that a control frame queued behind a running stream waits one chunk at most. */
TEST_F(Medium_TCPTransportStreams, ControlFrameOvertakesStream)
{
    connect(false);

    const std::shared_ptr<Gate> gate = std::make_shared<Gate>();
    ASSERT_EQ(worker_->send_stream(master_peer_, std::make_unique<GatedSource>(gate)),
              SendStatus::Queued);
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        ASSERT_TRUE(gate->changed.wait_for(lock, K_EVENT_LIMIT,
                                           [&] { return gate->is_first_pulled; }));
    }
    const SendStatus control = worker_->send(master_peer_, MessageType::Heartbeat, {});
    {
        const std::lock_guard<std::mutex> lock(gate->mutex);
        gate->is_open = true;
    }
    gate->changed.notify_all();
    ASSERT_EQ(control, SendStatus::Queued);

    ASSERT_TRUE(await(master_events_, EventKind::Frame, K_STREAM_FRAMES + 1));

    const std::vector<Event> seen = master_events_.events();
    expect_chunks_in_order(seen);
    // The control frame was queued while chunk 1 was being produced, so chunk 2 follows it.
    EXPECT_GE(chunks_before_the_control_frame(seen), 1u);
    EXPECT_LE(chunks_before_the_control_frame(seen), 2u);
}
