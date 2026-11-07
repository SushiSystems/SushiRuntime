/**************************************************************************/
/* transport_contract.hpp                                                 */
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
 * @file transport_contract.hpp
 * @brief Declares TransportContract, the typed suite every Transport adapter passes, and its helpers.
 * @see tests/README.md
 * @author Mustafa Garip
 */


#pragma once

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

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>

namespace TestSupport
{
    /** @brief Longest a test waits for an event that a healthy transport delivers at once. */
    inline constexpr std::chrono::milliseconds K_EVENT_LIMIT{10000};

    /** @brief Number of frames the ordering cases send each way. */
    inline constexpr std::uint32_t K_ORDERED_FRAMES = 500;

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
        EventKind kind = EventKind::Frame;                                       /**< What was called. */
        SushiRuntime::Distributed::PeerID peer;                                   /**< The peer it was called for. */
        SushiRuntime::Distributed::MessageType type =
            SushiRuntime::Distributed::MessageType::Hello;                        /**< The type of a frame. */
        std::uint8_t flags = 0;                                                  /**< The flags of a frame. */
        std::vector<std::byte> payload;                                          /**< The payload of a frame. */
        SushiRuntime::Distributed::PeerLossCause cause =
            SushiRuntime::Distributed::PeerLossCause::ClosedByPeer;               /**< The cause of a loss. */
    };

    /** @brief Encodes a counter as the four-byte payload the cases send. */
    inline std::vector<std::byte> encode_index(std::uint32_t index)
    {
        std::vector<std::byte> bytes(sizeof(index));
        for (std::size_t byte = 0; byte < bytes.size(); ++byte)
            bytes[byte] = static_cast<std::byte>((index >> (8 * byte)) & 0xFFu);
        return bytes;
    }

    /** @brief Decodes the first four bytes of a payload written by encode_index(). */
    inline std::uint32_t decode_index(const std::vector<std::byte>& bytes)
    {
        std::uint32_t index = 0;
        for (std::size_t byte = 0; byte < sizeof(index) && byte < bytes.size(); ++byte)
            index |= std::to_integer<std::uint32_t>(bytes[byte]) << (8 * byte);
        return index;
    }

    /**
     * @brief Records every event of one transport and lets a case wait for a condition.
     *
     * The reaction, when set, runs after the event is recorded and outside the recorder's
     * lock, on the transport thread or executor job that made the event.
     */
    class EventRecorder final : public SushiRuntime::Distributed::TransportEvents
    {
        public:
            /** @brief Names the callable a case runs inside an event. */
            using Reaction = std::function<void(const Event&)>;

            /** @brief Sets the reaction; called before the transport starts. */
            void set_reaction(Reaction reaction)
            {
                reaction_ = std::move(reaction);
            }

            /** @brief Records the connection. */
            void on_peer_connected(SushiRuntime::Distributed::PeerID peer,
                                   const SushiRuntime::Distributed::PeerDescription&) override
            {
                Event event;
                event.kind = EventKind::Connected;
                event.peer = peer;
                record(std::move(event));
            }

            /** @brief Records the frame and copies its payload. */
            void on_frame(SushiRuntime::Distributed::PeerID peer,
                          SushiRuntime::Distributed::MessageType type, std::uint8_t flags,
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
            void on_peer_lost(SushiRuntime::Distributed::PeerID peer,
                              const SushiRuntime::Distributed::PeerLoss& loss) override
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

            /**
             * @brief Waits until at least @p count events of one kind were recorded.
             * @return False when K_EVENT_LIMIT passed first.
             */
            [[nodiscard]] bool wait_for_count(EventKind kind, std::size_t count)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_EVENT_LIMIT,
                                         [&] { return count_of(events_, kind) >= count; });
            }

            /** @brief Returns whether at least @p count events of one kind are recorded now. */
            [[nodiscard]] bool has_count(EventKind kind, std::size_t count) const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return count_of(events_, kind) >= count;
            }

            /** @brief Returns a copy of every event recorded so far, in order. */
            [[nodiscard]] std::vector<Event> events() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return events_;
            }

            /** @brief Returns the number of events recorded so far. */
            [[nodiscard]] std::size_t size() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return events_.size();
            }

            /** @brief Returns the peer of the first recorded connection, or no peer. */
            [[nodiscard]] SushiRuntime::Distributed::PeerID first_peer() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                for (const Event& event : events_)
                    if (event.kind == EventKind::Connected)
                        return event.peer;
                return SushiRuntime::Distributed::PeerID{};
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
                    const std::lock_guard<std::mutex> lock(mutex_);
                    events_.push_back(std::move(event));
                }
                changed_.notify_all();
                if (reaction_)
                    reaction_(copy);
            }

            /// @brief Guards events_.
            mutable std::mutex mutex_;

            /// @brief Wakes wait_for_count() on every event.
            std::condition_variable changed_;

            /// @brief The events in the order they were made.
            std::vector<Event> events_;

            /// @brief Runs inside each event, or is empty.
            Reaction reaction_;
    };

    /** @brief Yields a fixed number of tagged frames and records its own destruction. */
    class ScriptedSource final : public SushiRuntime::Distributed::FrameSource
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
            [[nodiscard]] SushiRuntime::Distributed::FrameSourceStatus next(
                SushiRuntime::Distributed::OutboundFrame& frame) noexcept override
            {
                if (next_ >= frames_)
                    return SushiRuntime::Distributed::FrameSourceStatus::Finished;
                frame.type = SushiRuntime::Distributed::MessageType::BufferChunk;
                frame.flags = 0;
                frame.payload.assign(
                    {static_cast<std::byte>(tag_), static_cast<std::byte>(next_)});
                ++next_;
                return next_ == frames_ ? SushiRuntime::Distributed::FrameSourceStatus::FinalFrame
                                        : SushiRuntime::Distributed::FrameSourceStatus::Frame;
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

    /** @brief Names each instantiation of a typed transport suite after its rig. */
    class RigNames final
    {
        public:
            /** @brief Returns the rig's name. */
            template <typename Rig>
            static std::string GetName(int)
            {
                return Rig::name();
            }
    };

    /**
     * @brief Sends the indices 0 to count - 1 as frames of one type and expects each queued.
     * @param from The sending transport.
     * @param to The peer the frames go to.
     * @param type The type of every frame.
     * @param count Number of frames.
     */
    inline void expect_indexed_frames_queued(SushiRuntime::Distributed::Transport& from,
                                             SushiRuntime::Distributed::PeerID to,
                                             SushiRuntime::Distributed::MessageType type,
                                             std::uint32_t count)
    {
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const std::vector<std::byte> payload = encode_index(index);
            ASSERT_EQ(from.send(to, type, {payload.data(), payload.size()}),
                      SushiRuntime::Distributed::SendStatus::Queued);
        }
    }

    /** @brief Expects the frame events of a recorder to carry the indices 0 to count - 1 in order. */
    inline void expect_frames_in_order(const EventRecorder& recorder, std::uint32_t count)
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

    /** @brief Expects @p count heartbeats from a transport that is not stopped never to be refused as Stopped. */
    inline void expect_sends_not_stopped(SushiRuntime::Distributed::Transport& from,
                                         SushiRuntime::Distributed::PeerID peer,
                                         std::uint32_t count)
    {
        for (std::uint32_t index = 0; index < count; ++index)
            EXPECT_NE(from.send(peer, SushiRuntime::Distributed::MessageType::Heartbeat, {}),
                      SushiRuntime::Distributed::SendStatus::Stopped);
    }

    /** @brief Returns the indices the PONG frames in a recorder carry, in order. */
    inline std::vector<std::uint32_t> pong_indices(const EventRecorder& recorder)
    {
        std::vector<std::uint32_t> indices;
        for (const Event& event : recorder.events())
            if (event.kind == EventKind::Frame &&
                event.type == SushiRuntime::Distributed::MessageType::Pong)
                indices.push_back(decode_index(event.payload));
        return indices;
    }

    /** @brief Returns the indices 0 to count - 1. */
    inline std::vector<std::uint32_t> counting_to(std::uint32_t count)
    {
        std::vector<std::uint32_t> indices(count);
        for (std::uint32_t index = 0; index < count; ++index)
            indices[index] = index;
        return indices;
    }

    /** @brief Sends heartbeats until stopped, from a thread of its own. */
    class FloodingSender final
    {
        public:
            /** @brief Starts sending heartbeats with no payload to @p peer. */
            FloodingSender(SushiRuntime::Distributed::Transport& from,
                           SushiRuntime::Distributed::PeerID peer)
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
            FloodingSender(FloodingSender&&) = delete;
            FloodingSender& operator=(FloodingSender&&) = delete;

        private:
            /** @brief Sends until the flag clears. */
            void run(SushiRuntime::Distributed::Transport& from,
                     SushiRuntime::Distributed::PeerID peer)
            {
                while (is_running_.load())
                {
                    if (from.send(peer, SushiRuntime::Distributed::MessageType::Heartbeat, {}) !=
                        SushiRuntime::Distributed::SendStatus::Queued)
                        std::this_thread::yield();
                }
            }

            /// @brief Cleared by the destructor.
            std::atomic<bool> is_running_{true};

            /// @brief The sending thread; declared last so the flag exists before it runs.
            std::thread thread_;
    };

    /**
     * @brief Makes a reaction that answers a master's connection with a PING and each frame with a PONG.
     * @param master The master transport, held by reference so it may be set after the call.
     * @param refused Counts the sends the master's transport refused.
     */
    inline EventRecorder::Reaction make_echoing_reaction(
        const std::shared_ptr<SushiRuntime::Distributed::Transport>& master,
        std::atomic<std::uint32_t>& refused)
    {
        return [&master, &refused](const Event& event)
        {
            using SushiRuntime::Distributed::MessageType;
            using SushiRuntime::Distributed::SendStatus;
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
    inline EventRecorder::Reaction make_closing_reaction(
        const std::shared_ptr<SushiRuntime::Distributed::Transport>& worker,
        std::uint32_t frames)
    {
        return [&worker, frames](const Event& event)
        {
            const bool is_last_echo =
                event.kind == EventKind::Frame &&
                event.type == SushiRuntime::Distributed::MessageType::Pong &&
                decode_index(event.payload) + 1 == frames;
            if (is_last_echo)
                worker->close_peer(event.peer);
        };
    }

    /**
     * @brief Starts a master and a worker of one rig and records the events of both.
     * @tparam Rig A type with a static name(), make_master() returning a not started
     *         std::shared_ptr<Transport>, make_worker(bool reconnect) returning one for the master
     *         that was started, and await(recorder, kind, count) that returns whether the recorder
     *         holds that many events of the kind, after running or waiting for what produces them.
     */
    template <typename Rig>
    class TransportContract : public ::testing::Test
    {
        protected:
            /**
             * @brief Starts both endpoints and waits until each has seen the other connect.
             * @param reconnect Whether the worker connects again after a loss.
             */
            void connect(bool reconnect)
            {
                master_ = rig_.make_master();
                const SushiRuntime::Distributed::TransportResult master_started =
                    master_->start(master_events_);
                ASSERT_TRUE(master_started.is_ok()) << master_started.message;

                worker_ = rig_.make_worker(reconnect);
                const SushiRuntime::Distributed::TransportResult worker_started =
                    worker_->start(worker_events_);
                ASSERT_TRUE(worker_started.is_ok()) << worker_started.message;

                ASSERT_TRUE(await(master_events_, EventKind::Connected, 1));
                ASSERT_TRUE(await(worker_events_, EventKind::Connected, 1));
                worker_peer_ = master_events_.first_peer();
                master_peer_ = worker_events_.first_peer();
                ASSERT_TRUE(worker_peer_.valid());
                ASSERT_TRUE(master_peer_.valid());
            }

            /** @brief Makes the master endpoint and starts it, with no worker. */
            void start_master_alone()
            {
                master_ = rig_.make_master();
                ASSERT_TRUE(master_->start(master_events_).is_ok());
            }

            /** @brief Waits until a recorder holds @p count events of one kind, as the rig does it. */
            [[nodiscard]] bool await(EventRecorder& recorder, EventKind kind, std::size_t count)
            {
                return rig_.await(recorder, kind, count);
            }

            /** @brief Stops both endpoints, so no event outlives the recorders. */
            void TearDown() override
            {
                if (worker_ != nullptr)
                    worker_->stop();
                if (master_ != nullptr)
                    master_->stop();
            }

            /// @brief Builds the endpoints.
            Rig rig_;

            /// @brief Records the master's events; declared before the transports it outlives.
            EventRecorder master_events_;

            /// @brief Records the worker's events.
            EventRecorder worker_events_;

            /// @brief The master endpoint.
            std::shared_ptr<SushiRuntime::Distributed::Transport> master_;

            /// @brief The worker endpoint.
            std::shared_ptr<SushiRuntime::Distributed::Transport> worker_;

            /// @brief The id the master uses for the worker.
            SushiRuntime::Distributed::PeerID worker_peer_;

            /// @brief The id the worker uses for the master.
            SushiRuntime::Distributed::PeerID master_peer_;
    };

    TYPED_TEST_SUITE_P(TransportContract);

    /** @brief Tests that control frames from the worker arrive at the master in the order queued. */
    TYPED_TEST_P(TransportContract, DeliversWorkerFramesInOrder)
    {
        using SushiRuntime::Distributed::MessageType;
        this->connect(false);
        expect_indexed_frames_queued(*this->worker_, this->master_peer_, MessageType::Heartbeat,
                                     K_ORDERED_FRAMES);

        ASSERT_TRUE(this->await(this->master_events_, EventKind::Frame, K_ORDERED_FRAMES));

        expect_frames_in_order(this->master_events_, K_ORDERED_FRAMES);
    }

    /** @brief Tests that control frames from the master arrive at the worker in the order queued. */
    TYPED_TEST_P(TransportContract, DeliversMasterFramesInOrder)
    {
        using SushiRuntime::Distributed::MessageType;
        this->connect(false);
        expect_indexed_frames_queued(*this->master_, this->worker_peer_, MessageType::Ping,
                                     K_ORDERED_FRAMES);

        ASSERT_TRUE(this->await(this->worker_events_, EventKind::Frame, K_ORDERED_FRAMES));

        expect_frames_in_order(this->worker_events_, K_ORDERED_FRAMES);
    }

    /** @brief Tests that the statistics count the header and the payload of each frame. */
    TYPED_TEST_P(TransportContract, StatisticsCountEveryFrameWithItsHeader)
    {
        using SushiRuntime::Distributed::MessageType;
        this->connect(false);
        expect_indexed_frames_queued(*this->worker_, this->master_peer_, MessageType::Heartbeat,
                                     K_ORDERED_FRAMES);
        ASSERT_TRUE(this->await(this->master_events_, EventKind::Frame, K_ORDERED_FRAMES));
        const std::uint64_t frame_bytes =
            SushiRuntime::Distributed::Wire::K_FRAME_HEADER_BYTES + sizeof(std::uint32_t);

        EXPECT_GE(this->worker_->statistics().bytes_sent, K_ORDERED_FRAMES * frame_bytes);
        EXPECT_GE(this->master_->statistics().bytes_received, K_ORDERED_FRAMES * frame_bytes);
    }

    /** @brief Tests that the frames queued before close_peer() arrive before the loss. */
    TYPED_TEST_P(TransportContract, QueuedFramesArriveBeforeTheLossOfTheirPeer)
    {
        using SushiRuntime::Distributed::MessageType;
        using SushiRuntime::Distributed::PeerLossCause;
        constexpr std::uint32_t K_FRAMES = 100;
        this->connect(false);
        expect_indexed_frames_queued(*this->worker_, this->master_peer_, MessageType::Heartbeat,
                                     K_FRAMES);

        this->worker_->close_peer(this->master_peer_);

        ASSERT_TRUE(this->await(this->master_events_, EventKind::Lost, 1));
        const std::vector<Event> seen = this->master_events_.events();
        ASSERT_EQ(seen.size(), K_FRAMES + 2);
        EXPECT_EQ(seen.front().kind, EventKind::Connected);
        EXPECT_EQ(seen.back().kind, EventKind::Lost);
        EXPECT_EQ(seen.back().cause, PeerLossCause::ClosedByPeer);
        EXPECT_EQ(seen.back().peer, this->worker_peer_);
    }

    /** @brief Tests that the side that closes a peer hears ClosedLocally once, then gives up. */
    TYPED_TEST_P(TransportContract, TheClosingSideHearsOneLocalLossThenAbandons)
    {
        using SushiRuntime::Distributed::PeerLossCause;
        this->connect(false);

        this->worker_->close_peer(this->master_peer_);

        ASSERT_TRUE(this->await(this->worker_events_, EventKind::Lost, 1));
        ASSERT_TRUE(this->await(this->worker_events_, EventKind::Abandoned, 1));
        const std::vector<Event> seen = this->worker_events_.events();
        EXPECT_EQ(EventRecorder::count_of(seen, EventKind::Lost), 1u);
        ASSERT_GE(seen.size(), 2u);
        EXPECT_EQ(seen[seen.size() - 2].cause, PeerLossCause::ClosedLocally);
        EXPECT_EQ(seen.back().kind, EventKind::Abandoned);
    }

    /** @brief Tests that a send to a peer that was closed returns UnknownPeer. */
    TYPED_TEST_P(TransportContract, ASendToAClosedPeerReturnsUnknownPeer)
    {
        using SushiRuntime::Distributed::MessageType;
        using SushiRuntime::Distributed::SendStatus;
        this->connect(false);
        this->worker_->close_peer(this->master_peer_);
        ASSERT_TRUE(this->await(this->master_events_, EventKind::Lost, 1));

        EXPECT_EQ(this->worker_->send(this->master_peer_, MessageType::Heartbeat, {}),
                  SendStatus::UnknownPeer);
        EXPECT_EQ(this->master_->send(this->worker_peer_, MessageType::Ping, {}),
                  SendStatus::UnknownPeer);
    }

    /** @brief Tests that stop() reports no loss. */
    TYPED_TEST_P(TransportContract, StopReportsNoLoss)
    {
        this->connect(false);

        this->master_->stop();

        EXPECT_EQ(EventRecorder::count_of(this->master_events_.events(), EventKind::Lost), 0u);
    }

    /** @brief Tests that an event may answer each frame it receives from inside its callback. */
    TYPED_TEST_P(TransportContract, AnEventMayAnswerTheFramesItReceives)
    {
        using SushiRuntime::Distributed::MessageType;
        constexpr std::uint32_t K_FRAMES = 64;
        std::atomic<std::uint32_t> refused_sends{0};
        this->master_events_.set_reaction(make_echoing_reaction(this->master_, refused_sends));
        this->worker_events_.set_reaction(make_closing_reaction(this->worker_, K_FRAMES));
        this->connect(false);

        expect_indexed_frames_queued(*this->worker_, this->master_peer_, MessageType::Heartbeat,
                                     K_FRAMES);

        ASSERT_TRUE(this->await(this->worker_events_, EventKind::Lost, 1));
        ASSERT_TRUE(this->await(this->master_events_, EventKind::Lost, 1));
        EXPECT_EQ(refused_sends.load(), 0u);
        EXPECT_EQ(pong_indices(this->worker_events_), counting_to(K_FRAMES));
    }

    /** @brief Tests that an event may send to a peer from the callback that announces it. */
    TYPED_TEST_P(TransportContract, AnEventMaySendFromTheConnectionCallback)
    {
        using SushiRuntime::Distributed::MessageType;
        std::atomic<std::uint32_t> refused_sends{0};
        this->master_events_.set_reaction(make_echoing_reaction(this->master_, refused_sends));
        this->connect(false);

        ASSERT_TRUE(this->await(this->worker_events_, EventKind::Frame, 1));

        const std::vector<Event> seen = this->worker_events_.events();
        EXPECT_EQ(seen.back().type, MessageType::Ping);
        EXPECT_EQ(refused_sends.load(), 0u);
    }

    /** @brief Tests that a transport that was never started refuses a send as Stopped. */
    TYPED_TEST_P(TransportContract, ASendBeforeStartReturnsStopped)
    {
        this->master_ = this->rig_.make_master();

        EXPECT_EQ(this->master_->send(SushiRuntime::Distributed::PeerID{1},
                                      SushiRuntime::Distributed::MessageType::Ping, {}),
                  SushiRuntime::Distributed::SendStatus::Stopped);
    }

    /** @brief Tests that a started transport refuses a send to a peer it never saw. */
    TYPED_TEST_P(TransportContract, ASendToAnUnknownPeerReturnsUnknownPeer)
    {
        this->start_master_alone();

        EXPECT_EQ(this->master_->send(SushiRuntime::Distributed::PeerID{1},
                                      SushiRuntime::Distributed::MessageType::Ping, {}),
                  SushiRuntime::Distributed::SendStatus::UnknownPeer);
    }

    /** @brief Tests that a second start is refused as AlreadyStarted with a message. */
    TYPED_TEST_P(TransportContract, ASecondStartReturnsAlreadyStarted)
    {
        this->start_master_alone();

        const SushiRuntime::Distributed::TransportResult second =
            this->master_->start(this->master_events_);

        EXPECT_EQ(second.status, SushiRuntime::Distributed::TransportStatus::AlreadyStarted);
        EXPECT_FALSE(second.message.empty());
    }

    /** @brief Tests that a stopped transport refuses a send as Stopped, however often it stopped. */
    TYPED_TEST_P(TransportContract, ASendAfterStopReturnsStopped)
    {
        this->start_master_alone();
        this->master_->stop();
        this->master_->stop();

        EXPECT_EQ(this->master_->send(SushiRuntime::Distributed::PeerID{1},
                                      SushiRuntime::Distributed::MessageType::Ping, {}),
                  SushiRuntime::Distributed::SendStatus::Stopped);
    }

    /** @brief Tests that a stopped transport cannot start again and makes no event. */
    TYPED_TEST_P(TransportContract, AStoppedTransportCannotStartAgain)
    {
        this->start_master_alone();
        this->master_->stop();

        EXPECT_EQ(this->master_->start(this->master_events_).status,
                  SushiRuntime::Distributed::TransportStatus::AlreadyStarted);
        EXPECT_EQ(this->master_events_.size(), 0u);
    }

    /** @brief Tests that a stream offered to a stopped transport is refused as Stopped and destroyed. */
    TYPED_TEST_P(TransportContract, AStreamOfferedAfterStopReturnsStoppedAndIsDestroyed)
    {
        std::atomic<bool> is_destroyed{false};
        this->start_master_alone();
        this->master_->stop();

        EXPECT_EQ(this->master_->send_stream(
                      SushiRuntime::Distributed::PeerID{1},
                      std::make_unique<ScriptedSource>(std::uint8_t{1}, 2u, is_destroyed)),
                  SushiRuntime::Distributed::SendStatus::Stopped);
        EXPECT_TRUE(is_destroyed.load());
    }

    REGISTER_TYPED_TEST_SUITE_P(
        TransportContract, DeliversWorkerFramesInOrder, DeliversMasterFramesInOrder,
        StatisticsCountEveryFrameWithItsHeader, QueuedFramesArriveBeforeTheLossOfTheirPeer,
        TheClosingSideHearsOneLocalLossThenAbandons, ASendToAClosedPeerReturnsUnknownPeer,
        StopReportsNoLoss, AnEventMayAnswerTheFramesItReceives,
        AnEventMaySendFromTheConnectionCallback, ASendBeforeStartReturnsStopped,
        ASendToAnUnknownPeerReturnsUnknownPeer, ASecondStartReturnsAlreadyStarted,
        ASendAfterStopReturnsStopped, AStoppedTransportCannotStartAgain,
        AStreamOfferedAfterStopReturnsStoppedAndIsDestroyed);

    /**
     * @brief The fixture of the promises that need a rig whose endpoints run threads of their own.
     * @tparam Rig As for TransportContract, with endpoints that start no executor job.
     */
    template <typename Rig>
    class ThreadedTransportContract : public TransportContract<Rig>
    {

    };

    TYPED_TEST_SUITE_P(ThreadedTransportContract);

    /** @brief Tests that a stopped transport makes no event once stop() has returned. */
    TYPED_TEST_P(ThreadedTransportContract, NoEventIsMadeAfterStopReturns)
    {
        constexpr std::size_t K_FRAMES_BEFORE_STOP = 50;
        constexpr std::uint32_t K_SENDS_AFTER_STOP = 200;
        this->connect(false);
        const FloodingSender flood(*this->worker_, this->master_peer_);
        ASSERT_TRUE(this->await(this->master_events_, EventKind::Frame, K_FRAMES_BEFORE_STOP));

        this->master_->stop();
        const std::size_t events_at_stop = this->master_events_.size();
        expect_sends_not_stopped(*this->worker_, this->master_peer_, K_SENDS_AFTER_STOP);

        EXPECT_EQ(this->master_events_.size(), events_at_stop);
    }

    REGISTER_TYPED_TEST_SUITE_P(ThreadedTransportContract, NoEventIsMadeAfterStopReturns);
} // namespace TestSupport
