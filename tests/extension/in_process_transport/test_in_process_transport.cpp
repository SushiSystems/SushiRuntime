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
 * @file test_in_process_transport.cpp
 * @brief Tests InProcessTransport against the Transport contract: ordering, loss, stop and the statuses of a send, on threadless endpoints, and the stop of a threaded one.
 * @see tests/extension/in_process_transport/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/in_process_transport/in_process_transport.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>

#include <SushiRuntime/buffer_exchange/buffer_exchange.hpp>
#include <SushiRuntime/buffer_exchange/buffer_registry.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Longest a test waits for an event that a healthy transport delivers at once. */
    constexpr std::chrono::milliseconds K_EVENT_LIMIT{10000};

    /** @brief Seed of the executor the threadless endpoints of a small test share. */
    constexpr std::uint64_t K_EXECUTOR_SEED = 7;

    /** @brief Number of frames the ordering test sends each way. */
    constexpr std::uint32_t K_ORDERED_FRAMES = 500;

    /** @brief Names what a recorded event was. */
    enum class EventKind : std::uint8_t
    {
        Connected, /**< on_peer_connected(). */
        Frame,     /**< on_frame(). */
        Lost,      /**< on_peer_lost(). */
        Abandoned, /**< on_connect_abandoned(). */
    };

    /** @brief Holds one recorded event with a copy of its payload. */
    struct Event
    {
        EventKind kind = EventKind::Frame;                 /**< What was called. */
        PeerID peer;                                       /**< The peer it was called for. */
        MessageType type = MessageType::Hello;             /**< The type of a frame. */
        std::uint8_t flags = 0;                            /**< The flags of a frame. */
        std::vector<std::byte> payload;                    /**< The payload of a frame. */
        PeerLossCause cause = PeerLossCause::ClosedByPeer; /**< The cause of a loss. */
    };

    /** @brief Encodes a counter as the four-byte payload the tests send. */
    std::vector<std::byte> encode_index(std::uint32_t index)
    {
        std::vector<std::byte> bytes(sizeof(index));
        for (std::size_t byte = 0; byte < bytes.size(); ++byte)
            bytes[byte] = static_cast<std::byte>((index >> (8 * byte)) & 0xFFu);
        return bytes;
    }

    /** @brief Decodes the first four bytes of a payload written by encode_index(). */
    std::uint32_t decode_index(const std::vector<std::byte>& bytes)
    {
        std::uint32_t index = 0;
        for (std::size_t byte = 0; byte < sizeof(index) && byte < bytes.size(); ++byte)
            index |= std::to_integer<std::uint32_t>(bytes[byte]) << (8 * byte);
        return index;
    }

    /**
     * @brief Records every event of one transport and lets a test wait for a condition.
     *
     * The reaction, when set, runs after the event is recorded and outside the recorder's
     * lock, on the transport thread that made the event.
     */
    class EventRecorder final : public TransportEvents
    {
        public:
            /** @brief Names the callable a test runs inside an event. */
            using Reaction = std::function<void(const Event&)>;

            /** @brief Sets the reaction; called before the transport starts. */
            void set_reaction(Reaction reaction)
            {
                reaction_ = std::move(reaction);
            }

            /** @brief Records the connection. */
            void on_peer_connected(PeerID peer, const PeerDescription&) override
            {
                Event event;
                event.kind = EventKind::Connected;
                event.peer = peer;
                record(std::move(event));
            }

            /** @brief Records the frame and copies its payload. */
            void on_frame(PeerID peer, MessageType type, std::uint8_t flags,
                          SushiRuntime::span<const std::byte> payload) override
            {
                Event event;
                event.kind = EventKind::Frame;
                event.peer = peer;
                event.type = type;
                event.flags = flags;
                event.payload.assign(payload.data(), payload.data() + payload.size());
                record(std::move(event));
            }

            /** @brief Records the loss and its cause. */
            void on_peer_lost(PeerID peer, const PeerLoss& loss) override
            {
                Event event;
                event.kind = EventKind::Lost;
                event.peer = peer;
                event.cause = loss.cause;
                record(std::move(event));
            }

            /** @brief Records that the worker endpoint gave up. */
            void on_connect_abandoned(const std::string&) override
            {
                Event event;
                event.kind = EventKind::Abandoned;
                record(std::move(event));
            }

            /** @brief Waits until at least @p count events of one kind were recorded. */
            [[nodiscard]] bool wait_for_count(EventKind kind, std::size_t count)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_EVENT_LIMIT,
                                         [&] { return count_of(events_, kind) >= count; });
            }

            /** @brief Returns whether at least @p count events of one kind are recorded now. */
            [[nodiscard]] bool has_count(EventKind kind, std::size_t count) const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return count_of(events_, kind) >= count;
            }

            /** @brief Returns a copy of every event recorded so far, in order. */
            [[nodiscard]] std::vector<Event> events() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return events_;
            }

            /** @brief Returns the number of events recorded so far. */
            [[nodiscard]] std::size_t size() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return events_.size();
            }

            /** @brief Returns the peer of the first recorded connection, or no peer. */
            [[nodiscard]] PeerID first_peer() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const Event& event : events_)
                    if (event.kind == EventKind::Connected)
                        return event.peer;
                return PeerID{};
            }

            /** @brief Counts the events of one kind in a list. */
            [[nodiscard]] static std::size_t count_of(const std::vector<Event>& events,
                                                      EventKind kind)
            {
                std::size_t count = 0;
                for (const Event& event : events)
                    count += event.kind == kind ? 1 : 0;
                return count;
            }

        private:
            /** @brief Appends an event, wakes the waiters and runs the reaction. */
            void record(Event&& event)
            {
                const Event copy = event;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    events_.push_back(std::move(event));
                }
                changed_.notify_all();
                if (reaction_)
                    reaction_(copy);
            }

            /// @brief Guards events_.
            mutable std::mutex mutex_;

            /// @brief Wakes the waiters on every event.
            std::condition_variable changed_;

            /// @brief The events in the order they were made.
            std::vector<Event> events_;

            /// @brief Runs inside each event, or is empty.
            Reaction reaction_;
    };

    /** @brief Sends the indices 0 to count - 1 as frames of one type and expects each queued. */
    void expect_indexed_frames_queued(Transport& from, PeerID to, MessageType type,
                                      std::uint32_t count)
    {
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const std::vector<std::byte> payload = encode_index(index);
            ASSERT_EQ(from.send(to, type, {payload.data(), payload.size()}), SendStatus::Queued);
        }
    }

    /** @brief Expects the frame events to carry the indices 0 to count - 1 in order. */
    void expect_frames_in_order(const EventRecorder& recorder, std::uint32_t count)
    {
        std::uint32_t expected = 0;
        for (const Event& event : recorder.events())
        {
            if (event.kind != EventKind::Frame)
                continue;
            EXPECT_EQ(event.flags, 0);
            ASSERT_EQ(decode_index(event.payload), expected);
            ++expected;
        }
        EXPECT_EQ(expected, count);
    }

    /** @brief Sends heartbeats until stopped, from a thread of its own. */
    class FloodingSender final
    {
        public:
            /** @brief Starts sending @p type frames with no payload to @p peer. */
            FloodingSender(Transport& from, PeerID peer)
                : thread_([this, &from, peer] { run(from, peer); })
            {
            }

            /** @brief Stops the thread. */
            ~FloodingSender()
            {
                is_running_.store(false);
                thread_.join();
            }

            FloodingSender(const FloodingSender&) = delete;
            FloodingSender& operator=(const FloodingSender&) = delete;

        private:
            /** @brief Sends until the flag clears. */
            void run(Transport& from, PeerID peer)
            {
                while (is_running_.load())
                {
                    if (from.send(peer, MessageType::Heartbeat, {}) != SendStatus::Queued)
                        std::this_thread::yield();
                }
            }

            /// @brief Cleared by the destructor.
            std::atomic<bool> is_running_{true};

            /// @brief The sending thread; declared last so the flag exists before it runs.
            std::thread thread_;
    };

    /** @brief Sends @p count heartbeats and returns how many were refused as Stopped. */
    std::uint32_t count_stopped_sends(Transport& from, PeerID peer, std::uint32_t count)
    {
        std::uint32_t stopped = 0;
        for (std::uint32_t index = 0; index < count; ++index)
            stopped += from.send(peer, MessageType::Heartbeat, {}) == SendStatus::Stopped ? 1 : 0;
        return stopped;
    }

    /** @brief Makes a reaction that answers a master's connection with a PING and each frame with a PONG. */
    EventRecorder::Reaction make_echoing_reaction(
        const std::shared_ptr<InProcessTransport>& master, std::atomic<std::uint32_t>& refused)
    {
        return [&master, &refused](const Event& event)
        {
            SendStatus status = SendStatus::Queued;
            if (event.kind == EventKind::Connected)
                status = master->send(event.peer, MessageType::Ping, {});
            else if (event.kind == EventKind::Frame)
                status = master->send(event.peer, MessageType::Pong,
                                      {event.payload.data(), event.payload.size()});
            if (status != SendStatus::Queued)
                refused.fetch_add(1);
        };
    }

    /** @brief Makes a reaction that closes the master's peer once the last echo arrives. */
    EventRecorder::Reaction make_closing_reaction(
        const std::shared_ptr<InProcessTransport>& worker, std::uint32_t frames)
    {
        return [&worker, frames](const Event& event)
        {
            const bool is_last_echo = event.kind == EventKind::Frame &&
                                      event.type == MessageType::Pong &&
                                      decode_index(event.payload) + 1 == frames;
            if (is_last_echo)
                worker->close_peer(event.peer);
        };
    }

    /** @brief Returns the indices the PONG frames in a recorder carry, in order. */
    std::vector<std::uint32_t> pong_indices(const EventRecorder& recorder)
    {
        std::vector<std::uint32_t> indices;
        for (const Event& event : recorder.events())
            if (event.kind == EventKind::Frame && event.type == MessageType::Pong)
                indices.push_back(decode_index(event.payload));
        return indices;
    }

    /** @brief Returns the indices 0 to count - 1. */
    std::vector<std::uint32_t> counting_to(std::uint32_t count)
    {
        std::vector<std::uint32_t> indices(count);
        for (std::uint32_t index = 0; index < count; ++index)
            indices[index] = index;
        return indices;
    }
} // namespace

namespace
{
    /**
     * @brief Builds a master and a worker endpoint on one fabric and records the events of both.
     *
     * With an executor the endpoints are threadless and a wait steps that executor; without one
     * each endpoint runs a pump thread and a wait blocks on the recorder.
     */
    class TransportRig : public ::testing::Test
    {
        protected:
            /**
             * @brief Makes a rig whose endpoints post their rounds to @p executor, or run a thread.
             * @param executor The executor of the endpoints; null starts a pump thread each.
             */
            explicit TransportRig(std::shared_ptr<SeededExecutor> executor)
                : executor_(std::move(executor))
            {
            }

            /**
             * @brief Starts both endpoints and waits until each has seen the other connect.
             * @param reconnect Whether the worker connects again after a loss.
             * @param master_options The settings of the master endpoint.
             */
            void connect(bool reconnect,
                         InProcessMasterOptions master_options = InProcessMasterOptions())
            {
                master_options.executor = executor_;
                master_options.clock = clock_;
                master_ = InProcessTransport::make_master(fabric_, master_options);
                const TransportResult master_started = master_->start(master_events_);
                ASSERT_TRUE(master_started.is_ok()) << master_started.message;

                InProcessWorkerOptions options;
                options.reconnect = reconnect;
                options.executor = executor_;
                options.clock = clock_;
                worker_ = InProcessTransport::make_worker(fabric_, options);
                const TransportResult worker_started = worker_->start(worker_events_);
                ASSERT_TRUE(worker_started.is_ok()) << worker_started.message;

                ASSERT_TRUE(await(master_events_, EventKind::Connected, 1));
                ASSERT_TRUE(await(worker_events_, EventKind::Connected, 1));
                worker_peer_ = master_events_.first_peer();
                master_peer_ = worker_events_.first_peer();
                ASSERT_TRUE(worker_peer_.valid());
                ASSERT_TRUE(master_peer_.valid());
            }

            /** @brief Makes the master endpoint with the rig's executor and clock, and starts nothing. */
            [[nodiscard]] std::shared_ptr<InProcessTransport> make_master_endpoint()
            {
                InProcessMasterOptions options;
                options.executor = executor_;
                options.clock = clock_;
                return InProcessTransport::make_master(fabric_, options);
            }

            /**
             * @brief Waits until a recorder holds @p count events of one kind.
             *
             * A threadless rig runs every posted round first and answers from what was recorded.
             */
            [[nodiscard]] bool await(EventRecorder& recorder, EventKind kind, std::size_t count)
            {
                if (executor_ == nullptr)
                    return recorder.wait_for_count(kind, count);
                executor_->run_all();
                return recorder.has_count(kind, count);
            }

            /** @brief Stops both endpoints, so no event outlives the recorders. */
            void TearDown() override
            {
                if (worker_ != nullptr)
                    worker_->stop();
                if (master_ != nullptr)
                    master_->stop();
            }

            /// @brief Runs the pump rounds of a threadless rig; null for a threaded one.
            std::shared_ptr<SeededExecutor> executor_;

            /// @brief Times the endpoints of a threadless rig; no test advances it.
            std::shared_ptr<const Clock> clock_ = std::make_shared<ManualClock>();

            /// @brief The fabric both endpoints attach to.
            InProcessFabric fabric_;

            /// @brief Records the master's events; declared before the transports it outlives.
            EventRecorder master_events_;

            /// @brief Records the worker's events.
            EventRecorder worker_events_;

            /// @brief The master endpoint.
            std::shared_ptr<InProcessTransport> master_;

            /// @brief The worker endpoint.
            std::shared_ptr<InProcessTransport> worker_;

            /// @brief The id the master uses for the worker.
            PeerID worker_peer_;

            /// @brief The id the worker uses for the master.
            PeerID master_peer_;
    };
} // namespace

/** @brief Runs the contract on threadless endpoints that share one seeded executor. */
class Small_TransportContract : public TransportRig
{
    protected:
        /** @brief Makes the rig over a seeded executor. */
        Small_TransportContract() : TransportRig(std::make_shared<SeededExecutor>(K_EXECUTOR_SEED))
        {
        }
};

/** @brief Runs the one promise that needs a pump thread: the join of stop() against a sender. */
class Medium_TransportContract : public TransportRig
{
    protected:
        /** @brief Makes the rig with a pump thread per endpoint. */
        Medium_TransportContract() : TransportRig(nullptr)
        {
        }
};

/** @brief Checks that control frames from the worker arrive at the master in the order queued. */
TEST_F(Small_TransportContract, DeliversWorkerFramesInOrder)
{
    connect(false);
    expect_indexed_frames_queued(*worker_, master_peer_, MessageType::Heartbeat, K_ORDERED_FRAMES);

    ASSERT_TRUE(await(master_events_, EventKind::Frame, K_ORDERED_FRAMES));

    expect_frames_in_order(master_events_, K_ORDERED_FRAMES);
}

/** @brief Checks that control frames from the master arrive at the worker in the order queued. */
TEST_F(Small_TransportContract, DeliversMasterFramesInOrder)
{
    connect(false);
    expect_indexed_frames_queued(*master_, worker_peer_, MessageType::Ping, K_ORDERED_FRAMES);

    ASSERT_TRUE(await(worker_events_, EventKind::Frame, K_ORDERED_FRAMES));

    expect_frames_in_order(worker_events_, K_ORDERED_FRAMES);
}

/** @brief Checks that the statistics count the header and the payload of each frame. */
TEST_F(Small_TransportContract, StatisticsCountEveryFrameWithItsHeader)
{
    connect(false);
    expect_indexed_frames_queued(*worker_, master_peer_, MessageType::Heartbeat, K_ORDERED_FRAMES);
    ASSERT_TRUE(await(master_events_, EventKind::Frame, K_ORDERED_FRAMES));
    const std::uint64_t frame_bytes = Wire::K_FRAME_HEADER_BYTES + sizeof(std::uint32_t);

    EXPECT_GE(worker_->statistics().bytes_sent, K_ORDERED_FRAMES * frame_bytes);
    EXPECT_GE(master_->statistics().bytes_received, K_ORDERED_FRAMES * frame_bytes);
}

/** @brief Checks that the frames queued before close_peer() arrive before the loss. */
TEST_F(Small_TransportContract, QueuedFramesArriveBeforeTheLossOfTheirPeer)
{
    constexpr std::uint32_t K_FRAMES = 100;
    connect(false);
    expect_indexed_frames_queued(*worker_, master_peer_, MessageType::Heartbeat, K_FRAMES);

    worker_->close_peer(master_peer_);

    ASSERT_TRUE(await(master_events_, EventKind::Lost, 1));
    const std::vector<Event> seen = master_events_.events();
    ASSERT_EQ(seen.size(), K_FRAMES + 2);
    EXPECT_EQ(seen.front().kind, EventKind::Connected);
    EXPECT_EQ(seen.back().kind, EventKind::Lost);
    EXPECT_EQ(seen.back().cause, PeerLossCause::ClosedByPeer);
    EXPECT_EQ(seen.back().peer, worker_peer_);
}

/** @brief Checks that the side that closes a peer hears ClosedLocally once, then gives up. */
TEST_F(Small_TransportContract, TheClosingSideHearsOneLocalLossThenAbandons)
{
    connect(false);

    worker_->close_peer(master_peer_);

    ASSERT_TRUE(await(worker_events_, EventKind::Lost, 1));
    ASSERT_TRUE(await(worker_events_, EventKind::Abandoned, 1));
    const std::vector<Event> seen = worker_events_.events();
    EXPECT_EQ(EventRecorder::count_of(seen, EventKind::Lost), 1u);
    ASSERT_GE(seen.size(), 2u);
    EXPECT_EQ(seen[seen.size() - 2].cause, PeerLossCause::ClosedLocally);
    EXPECT_EQ(seen.back().kind, EventKind::Abandoned);
}

/** @brief Checks that a send to a peer that was closed returns UnknownPeer. */
TEST_F(Small_TransportContract, ASendToAClosedPeerReturnsUnknownPeer)
{
    connect(false);
    worker_->close_peer(master_peer_);
    ASSERT_TRUE(await(master_events_, EventKind::Lost, 1));

    EXPECT_EQ(worker_->send(master_peer_, MessageType::Heartbeat, {}), SendStatus::UnknownPeer);
    EXPECT_EQ(master_->send(worker_peer_, MessageType::Ping, {}), SendStatus::UnknownPeer);
}

/** @brief Checks that stop() reports no loss. */
TEST_F(Small_TransportContract, StopReportsNoLoss)
{
    connect(false);

    master_->stop();

    EXPECT_EQ(EventRecorder::count_of(master_events_.events(), EventKind::Lost), 0u);
}

/** @brief Checks that the stopped transport makes no event once stop() has returned. */
TEST_F(Medium_TransportContract, NoEventIsMadeAfterStopReturns)
{
    constexpr std::size_t K_FRAMES_BEFORE_STOP = 50;
    connect(false);
    const FloodingSender flood(*worker_, master_peer_);
    ASSERT_TRUE(await(master_events_, EventKind::Frame, K_FRAMES_BEFORE_STOP));

    master_->stop();
    const std::size_t events_at_stop = master_events_.size();
    count_stopped_sends(*worker_, master_peer_, 200);

    EXPECT_EQ(master_events_.size(), events_at_stop);
}

/** @brief Checks that an event may answer each frame it receives from inside its callback. */
TEST_F(Small_TransportContract, AnEventMayAnswerTheFramesItReceives)
{
    constexpr std::uint32_t K_FRAMES = 64;
    std::atomic<std::uint32_t> refused_sends{0};
    master_events_.set_reaction(make_echoing_reaction(master_, refused_sends));
    worker_events_.set_reaction(make_closing_reaction(worker_, K_FRAMES));
    connect(false);

    expect_indexed_frames_queued(*worker_, master_peer_, MessageType::Heartbeat, K_FRAMES);

    ASSERT_TRUE(await(worker_events_, EventKind::Lost, 1));
    ASSERT_TRUE(await(master_events_, EventKind::Lost, 1));
    EXPECT_EQ(refused_sends.load(), 0u);
    EXPECT_EQ(pong_indices(worker_events_), counting_to(K_FRAMES));
}

/** @brief Checks that an event may send to a peer from the callback that announces it. */
TEST_F(Small_TransportContract, AnEventMaySendFromTheConnectionCallback)
{
    std::atomic<std::uint32_t> refused_sends{0};
    master_events_.set_reaction(make_echoing_reaction(master_, refused_sends));
    connect(false);

    ASSERT_TRUE(await(worker_events_, EventKind::Frame, 1));

    const std::vector<Event> seen = worker_events_.events();
    EXPECT_EQ(seen.back().type, MessageType::Ping);
    EXPECT_EQ(refused_sends.load(), 0u);
}

/** @brief Checks that a master whose control queue holds no frame refuses a send to a connected worker. */
TEST_F(Small_TransportContract, AControlQueueOfNoFramesRefusesEverySend)
{
    InProcessMasterOptions no_queue;
    no_queue.control_queue_frames = 0;
    connect(false, no_queue);

    EXPECT_EQ(master_->send(worker_peer_, MessageType::Ping, {}), SendStatus::QueueFull);
}

/** @brief Checks that a transport that was never started refuses a send as Stopped. */
TEST_F(Small_TransportContract, ASendBeforeStartReturnsStopped)
{
    master_ = make_master_endpoint();

    EXPECT_EQ(master_->send(PeerID{1}, MessageType::Ping, {}), SendStatus::Stopped);
}

/** @brief Checks that a started transport refuses a send to a peer it never saw. */
TEST_F(Small_TransportContract, ASendToAnUnknownPeerReturnsUnknownPeer)
{
    master_ = make_master_endpoint();
    ASSERT_TRUE(master_->start(master_events_).is_ok());

    EXPECT_EQ(master_->send(PeerID{1}, MessageType::Ping, {}), SendStatus::UnknownPeer);
}

/** @brief Checks that a second start is refused as AlreadyStarted with a message. */
TEST_F(Small_TransportContract, ASecondStartReturnsAlreadyStarted)
{
    master_ = make_master_endpoint();
    ASSERT_TRUE(master_->start(master_events_).is_ok());

    const TransportResult second = master_->start(master_events_);

    EXPECT_EQ(second.status, TransportStatus::AlreadyStarted);
    EXPECT_FALSE(second.message.empty());
}

/** @brief Checks that a stopped transport refuses a send as Stopped, however often it stopped. */
TEST_F(Small_TransportContract, ASendAfterStopReturnsStopped)
{
    master_ = make_master_endpoint();
    ASSERT_TRUE(master_->start(master_events_).is_ok());
    master_->stop();
    master_->stop();

    EXPECT_EQ(master_->send(PeerID{1}, MessageType::Ping, {}), SendStatus::Stopped);
}

/** @brief Checks that a stopped transport cannot start again and makes no event. */
TEST_F(Small_TransportContract, AStoppedTransportCannotStartAgain)
{
    master_ = make_master_endpoint();
    ASSERT_TRUE(master_->start(master_events_).is_ok());
    master_->stop();

    EXPECT_EQ(master_->start(master_events_).status, TransportStatus::AlreadyStarted);
    EXPECT_EQ(master_events_.size(), 0u);
}

/** @brief Checks that a stream offered to a stopped transport is refused as Stopped. */
TEST_F(Small_TransportContract, AStreamOfferedAfterStopReturnsStopped)
{
    std::vector<std::byte> storage(16);
    BufferRegistry registry;
    BufferExchange exchange(registry, 4096);
    const BufferHandle handle = registry.register_buffer(storage.data(), storage.size());
    master_ = make_master_endpoint();
    ASSERT_TRUE(master_->start(master_events_).is_ok());
    master_->stop();

    EXPECT_EQ(master_->send_stream(PeerID{1}, exchange.serve_pull(handle.id, handle.version)),
              SendStatus::Stopped);
}
