/**************************************************************************/
/* test_transport_conformance.cpp                                         */
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
 * @file test_transport_conformance.cpp
 * @brief Tests the ITransport contract on both transports, and Outbox alone.
 * @see modules/extension/distributed/tests/README.md
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

#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>

#include "outbox.hpp"
#include "wire_frame.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Longest a test waits for an event that a healthy transport delivers at once. */
    constexpr std::chrono::milliseconds K_EVENT_LIMIT{10000};

    /** @brief Number of frames the ordering tests send each way. */
    constexpr std::uint32_t K_ORDERED_FRAMES = 500;

    /** @brief Number of frames the gated stream of ControlFrameOvertakesStream yields. */
    constexpr std::uint32_t K_STREAM_FRAMES = 8;

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
        EventKind kind = EventKind::Frame;                  /**< What was called. */
        PeerId peer;                                        /**< The peer it was called for. */
        MessageType type = MessageType::Hello;              /**< The type of a frame. */
        std::uint8_t flags = 0;                             /**< The flags of a frame. */
        std::vector<std::byte> payload;                     /**< The payload of a frame. */
        PeerLossCause cause = PeerLossCause::ClosedByPeer;  /**< The cause of a loss. */
    };

    /** @brief Encodes a counter as the four-byte payload the ordering tests send. */
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
            void on_peer_connected(PeerId peer, const PeerInfo&) override
            {
                Event event;
                event.kind = EventKind::Connected;
                event.peer = peer;
                record(std::move(event));
            }

            /** @brief Records the frame and copies its payload. */
            void on_frame(PeerId peer, MessageType type, std::uint8_t flags,
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
            void on_peer_lost(PeerId peer, const PeerLoss& loss) override
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
             * @brief Waits until the recorded events satisfy a condition.
             * @return False when K_EVENT_LIMIT passed first.
             */
            [[nodiscard]] bool wait_until(
                const std::function<bool(const std::vector<Event>&)>& is_met)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_EVENT_LIMIT, [&] { return is_met(events_); });
            }

            /** @brief Waits until at least @p count events of one kind were recorded. */
            [[nodiscard]] bool wait_for_count(EventKind kind, std::size_t count)
            {
                return wait_until([&](const std::vector<Event>& events)
                                  {
                                      return count_of(events, kind) >= count;
                                  });
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

            /** @brief Returns the number of recorded events of one kind. */
            [[nodiscard]] std::size_t count(EventKind kind) const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return count_of(events_, kind);
            }

            /** @brief Returns the peer of the first recorded connection, or no peer. */
            [[nodiscard]] PeerId first_peer() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const Event& event : events_)
                    if (event.kind == EventKind::Connected)
                        return event.peer;
                return PeerId{};
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

            /// @brief Wakes wait_until() on every event.
            std::condition_variable changed_;

            /// @brief The events in the order they were made.
            std::vector<Event> events_;

            /// @brief Runs inside each event, or is empty.
            Reaction reaction_;
    };

    /** @brief Builds a master and a worker endpoint on one in-process fabric. */
    class InProcessRig final
    {
        public:
            /** @brief Returns the name the typed suite gives this rig. */
            static std::string name()
            {
                return "InProcess";
            }

            /** @brief Creates the master endpoint. */
            std::shared_ptr<ITransport> make_master()
            {
                return InProcessTransport::make_master(fabric_);
            }

            /** @brief Creates a worker endpoint. */
            std::shared_ptr<ITransport> make_worker(bool reconnect)
            {
                InProcessWorkerOptions options;
                options.reconnect = reconnect;
                return InProcessTransport::make_worker(fabric_, options);
            }

        private:
            /// @brief The fabric both endpoints attach to.
            InProcessFabric fabric_;
    };

    /** @brief Builds a TCP master on a free loopback port and a worker that connects to it. */
    class TcpLoopbackRig final
    {
        public:
            /** @brief Returns the name the typed suite gives this rig. */
            static std::string name()
            {
                return "TcpLoopback";
            }

            /** @brief Creates the master endpoint on 127.0.0.1, port 0. */
            std::shared_ptr<ITransport> make_master()
            {
                master_ = TcpTransport::make_master();
                return master_;
            }

            /**
             * @brief Creates a worker endpoint for the port the master bound.
             * @pre The master made by make_master() was started.
             */
            std::shared_ptr<ITransport> make_worker(bool reconnect)
            {
                TcpWorkerOptions options;
                options.reconnect = reconnect;
                options.backoff_initial = std::chrono::milliseconds(10);
                return TcpTransport::make_worker(master_->local_endpoint(), options);
            }

        private:
            /// @brief The master, kept to read its port.
            std::shared_ptr<TcpTransport> master_;
    };

    /** @brief Names each instantiation of the typed suite after its rig. */
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

    /** @brief Holds what a GatedSource and its test share. */
    struct Gate
    {
        std::mutex mutex;                /**< Guards the two flags. */
        std::condition_variable changed; /**< Wakes the test and the source. */
        bool is_first_pulled = false;    /**< Set when the source wrote its first frame. */
        bool is_open = false;            /**< Set by the test to let the second frame out. */
    };

    /**
     * @brief Yields K_STREAM_FRAMES chunks and holds the second until the test opens the gate.
     *
     * The wait stands in for a writer that is busy with a chunk while a control frame is
     * queued; a production source never waits.
     */
    class GatedSource final : public FrameSource
    {
        public:
            /** @param gate Shared with the test, so a failed test leaves no dangling wait. */
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
            /// @brief What the source shares with the test.
            std::shared_ptr<Gate> gate_;

            /// @brief Index of the chunk the next call writes.
            std::uint32_t next_ = 0;
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
} // namespace

/**
 * @brief Starts a master and a worker of one rig and records the events of both.
 * @tparam Rig InProcessRig or TcpLoopbackRig.
 */
template <typename Rig>
class Unit_TransportConformanceTest : public ::testing::Test
{
    protected:
        /**
         * @brief Starts both endpoints and waits until each has seen the other connect.
         * @param reconnect Whether the worker connects again after a loss.
         */
        void connect(bool reconnect)
        {
            master_ = rig_.make_master();
            const TransportResult master_started = master_->start(master_events_);
            ASSERT_TRUE(master_started.is_ok()) << master_started.message;

            worker_ = rig_.make_worker(reconnect);
            const TransportResult worker_started = worker_->start(worker_events_);
            ASSERT_TRUE(worker_started.is_ok()) << worker_started.message;

            ASSERT_TRUE(master_events_.wait_for_count(EventKind::Connected, 1));
            ASSERT_TRUE(worker_events_.wait_for_count(EventKind::Connected, 1));
            worker_peer_ = master_events_.first_peer();
            master_peer_ = worker_events_.first_peer();
            ASSERT_TRUE(worker_peer_.valid());
            ASSERT_TRUE(master_peer_.valid());
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
        std::shared_ptr<ITransport> master_;

        /// @brief The worker endpoint.
        std::shared_ptr<ITransport> worker_;

        /// @brief The id the master uses for the worker.
        PeerId worker_peer_;

        /// @brief The id the worker uses for the master.
        PeerId master_peer_;
};

/** @brief Lists the rigs the conformance suite runs on. */
using RigTypes = ::testing::Types<InProcessRig, TcpLoopbackRig>;
TYPED_TEST_SUITE(Unit_TransportConformanceTest, RigTypes, RigNames);

/**
 * @brief Checks that control frames arrive in the order they were queued, both ways.
 */
TYPED_TEST(Unit_TransportConformanceTest, DeliversInOrderPerPeer)
{
    this->connect(false);

    for (std::uint32_t index = 0; index < K_ORDERED_FRAMES; ++index)
    {
        const std::vector<std::byte> payload = encode_index(index);
        ASSERT_EQ(this->worker_->send(this->master_peer_, MessageType::Heartbeat,
                                      {payload.data(), payload.size()}),
                  SendStatus::Queued);
        ASSERT_EQ(this->master_->send(this->worker_peer_, MessageType::Ping,
                                      {payload.data(), payload.size()}),
                  SendStatus::Queued);
    }
    ASSERT_TRUE(this->master_events_.wait_for_count(EventKind::Frame, K_ORDERED_FRAMES));
    ASSERT_TRUE(this->worker_events_.wait_for_count(EventKind::Frame, K_ORDERED_FRAMES));

    for (EventRecorder* recorder : {&this->master_events_, &this->worker_events_})
    {
        std::uint32_t expected = 0;
        for (const Event& event : recorder->events())
        {
            if (event.kind != EventKind::Frame)
                continue;
            EXPECT_EQ(event.flags, 0);
            ASSERT_EQ(decode_index(event.payload), expected);
            ++expected;
        }
        EXPECT_EQ(expected, K_ORDERED_FRAMES);
    }

    const std::uint64_t frame_bytes = Wire::K_FRAME_HEADER_BYTES + sizeof(std::uint32_t);
    EXPECT_GE(this->worker_->statistics().bytes_sent, K_ORDERED_FRAMES * frame_bytes);
    EXPECT_GE(this->master_->statistics().bytes_received, K_ORDERED_FRAMES * frame_bytes);
}

/**
 * @brief Checks that a closed peer's queued frames arrive first and its loss is reported once.
 */
TYPED_TEST(Unit_TransportConformanceTest, PeerLostOnceAfterLastFrame)
{
    constexpr std::uint32_t K_FRAMES = 100;
    this->connect(false);

    for (std::uint32_t index = 0; index < K_FRAMES; ++index)
    {
        const std::vector<std::byte> payload = encode_index(index);
        ASSERT_EQ(this->worker_->send(this->master_peer_, MessageType::Heartbeat,
                                      {payload.data(), payload.size()}),
                  SendStatus::Queued);
    }
    this->worker_->close_peer(this->master_peer_);
    EXPECT_EQ(this->worker_->send(this->master_peer_, MessageType::Heartbeat, {}),
              SendStatus::UnknownPeer);

    ASSERT_TRUE(this->master_events_.wait_for_count(EventKind::Lost, 1));
    ASSERT_TRUE(this->worker_events_.wait_for_count(EventKind::Lost, 1));
    ASSERT_TRUE(this->worker_events_.wait_for_count(EventKind::Abandoned, 1));
    EXPECT_EQ(this->master_->send(this->worker_peer_, MessageType::Ping, {}),
              SendStatus::UnknownPeer);

    // stop() joins every transport thread, so the counts below are final.
    this->worker_->stop();
    this->master_->stop();

    const std::vector<Event> seen = this->master_events_.events();
    ASSERT_EQ(seen.size(), K_FRAMES + 2);
    EXPECT_EQ(seen.front().kind, EventKind::Connected);
    for (std::uint32_t index = 0; index < K_FRAMES; ++index)
    {
        ASSERT_EQ(seen[index + 1].kind, EventKind::Frame);
        ASSERT_EQ(decode_index(seen[index + 1].payload), index);
    }
    EXPECT_EQ(seen.back().kind, EventKind::Lost);
    EXPECT_EQ(seen.back().cause, PeerLossCause::ClosedByPeer);
    EXPECT_EQ(seen.back().peer, this->worker_peer_);

    const std::vector<Event> worker_seen = this->worker_events_.events();
    EXPECT_EQ(EventRecorder::count_of(worker_seen, EventKind::Lost), 1u);
    EXPECT_EQ(EventRecorder::count_of(worker_seen, EventKind::Abandoned), 1u);
    ASSERT_GE(worker_seen.size(), 2u);
    EXPECT_EQ(worker_seen[worker_seen.size() - 2].cause, PeerLossCause::ClosedLocally);
    EXPECT_EQ(worker_seen.back().kind, EventKind::Abandoned);
}

/**
 * @brief Checks that stop() reports no loss and that no event is made once it has returned.
 */
TYPED_TEST(Unit_TransportConformanceTest, NoCallbackAfterStop)
{
    constexpr std::size_t K_FRAMES_BEFORE_STOP = 50;
    constexpr std::uint32_t K_FRAMES_AFTER_STOP = 200;
    this->connect(false);

    std::atomic<bool> is_flooding{true};
    std::thread flood(
        [&]
        {
            while (is_flooding.load())
            {
                const SendStatus status =
                    this->worker_->send(this->master_peer_, MessageType::Heartbeat, {});
                if (status != SendStatus::Queued)
                    std::this_thread::yield();
            }
        });

    const bool has_traffic =
        this->master_events_.wait_for_count(EventKind::Frame, K_FRAMES_BEFORE_STOP);
    this->master_->stop();
    const std::size_t events_at_stop = this->master_events_.size();

    for (std::uint32_t index = 0; index < K_FRAMES_AFTER_STOP; ++index)
    {
        const SendStatus status =
            this->worker_->send(this->master_peer_, MessageType::Heartbeat, {});
        EXPECT_NE(status, SendStatus::Stopped);
    }
    is_flooding.store(false);
    flood.join();
    this->worker_->stop();

    ASSERT_TRUE(has_traffic);
    EXPECT_EQ(this->master_events_.size(), events_at_stop);
    EXPECT_EQ(this->master_events_.count(EventKind::Lost), 0u);
}

/**
 * @brief Checks that an event may send to its own peer and close it.
 */
TYPED_TEST(Unit_TransportConformanceTest, SendFromCallbackAllowed)
{
    constexpr std::uint32_t K_FRAMES = 64;
    std::atomic<std::uint32_t> refused_sends{0};

    this->master_events_.set_reaction(
        [&](const Event& event)
        {
            SendStatus status = SendStatus::Queued;
            if (event.kind == EventKind::Connected)
                status = this->master_->send(event.peer, MessageType::Ping, {});
            else if (event.kind == EventKind::Frame)
                status = this->master_->send(event.peer, MessageType::Pong,
                                             {event.payload.data(), event.payload.size()});
            if (status != SendStatus::Queued)
                refused_sends.fetch_add(1);
        });
    this->worker_events_.set_reaction(
        [&](const Event& event)
        {
            const bool is_last_echo = event.kind == EventKind::Frame &&
                                      event.type == MessageType::Pong &&
                                      decode_index(event.payload) + 1 == K_FRAMES;
            if (is_last_echo)
                this->worker_->close_peer(event.peer);
        });
    this->connect(false);

    for (std::uint32_t index = 0; index < K_FRAMES; ++index)
    {
        const std::vector<std::byte> payload = encode_index(index);
        ASSERT_EQ(this->worker_->send(this->master_peer_, MessageType::Heartbeat,
                                      {payload.data(), payload.size()}),
                  SendStatus::Queued);
    }
    ASSERT_TRUE(this->worker_events_.wait_for_count(EventKind::Lost, 1));
    ASSERT_TRUE(this->master_events_.wait_for_count(EventKind::Lost, 1));
    EXPECT_EQ(refused_sends.load(), 0u);

    std::uint32_t expected = 0;
    bool has_greeting = false;
    for (const Event& event : this->worker_events_.events())
    {
        if (event.kind != EventKind::Frame)
            continue;
        if (event.type == MessageType::Ping)
        {
            EXPECT_EQ(expected, 0u);
            has_greeting = true;
            continue;
        }
        ASSERT_EQ(event.type, MessageType::Pong);
        ASSERT_EQ(decode_index(event.payload), expected);
        ++expected;
    }
    EXPECT_TRUE(has_greeting);
    EXPECT_EQ(expected, K_FRAMES);
}

/**
 * @brief Checks the statuses outside a transport's running life: Stopped and AlreadyStarted.
 */
TYPED_TEST(Unit_TransportConformanceTest, SendAfterStopReturnsStopped)
{
    this->master_ = this->rig_.make_master();
    const PeerId nobody{1};
    EXPECT_EQ(this->master_->send(nobody, MessageType::Ping, {}), SendStatus::Stopped);

    ASSERT_TRUE(this->master_->start(this->master_events_).is_ok());
    EXPECT_EQ(this->master_->send(nobody, MessageType::Ping, {}), SendStatus::UnknownPeer);
    const TransportResult second = this->master_->start(this->master_events_);
    EXPECT_EQ(second.status, TransportStatus::AlreadyStarted);
    EXPECT_FALSE(second.message.empty());

    this->master_->stop();
    this->master_->stop();
    EXPECT_EQ(this->master_->send(nobody, MessageType::Ping, {}), SendStatus::Stopped);

    std::atomic<bool> is_destroyed{false};
    EXPECT_EQ(this->master_->send_stream(
                  nobody, std::make_unique<ScriptedSource>(std::uint8_t{1}, 2u, is_destroyed)),
              SendStatus::Stopped);
    EXPECT_TRUE(is_destroyed.load());
    this->master_->close_peer(nobody);

    EXPECT_EQ(this->master_->start(this->master_events_).status,
              TransportStatus::AlreadyStarted);
    EXPECT_EQ(this->master_events_.size(), 0u);
}

/**
 * @brief Checks that a control frame queued behind a running stream waits one chunk at most.
 */
TYPED_TEST(Unit_TransportConformanceTest, ControlFrameOvertakesStream)
{
    this->connect(false);

    const std::shared_ptr<Gate> gate = std::make_shared<Gate>();
    ASSERT_EQ(this->worker_->send_stream(this->master_peer_,
                                         std::make_unique<GatedSource>(gate)),
              SendStatus::Queued);
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        ASSERT_TRUE(gate->changed.wait_for(lock, K_EVENT_LIMIT,
                                           [&] { return gate->is_first_pulled; }));
    }
    const SendStatus control =
        this->worker_->send(this->master_peer_, MessageType::Heartbeat, {});
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        gate->is_open = true;
    }
    gate->changed.notify_all();
    ASSERT_EQ(control, SendStatus::Queued);

    ASSERT_TRUE(this->master_events_.wait_for_count(EventKind::Frame, K_STREAM_FRAMES + 1));

    std::uint32_t chunks = 0;
    std::uint32_t chunks_before_control = K_STREAM_FRAMES + 1;
    for (const Event& event : this->master_events_.events())
    {
        if (event.kind != EventKind::Frame)
            continue;
        if (event.type == MessageType::Heartbeat)
        {
            chunks_before_control = chunks;
            continue;
        }
        ASSERT_EQ(event.type, MessageType::BufferChunk);
        ASSERT_EQ(decode_index(event.payload), chunks);
        const bool is_last = chunks + 1 == K_STREAM_FRAMES;
        EXPECT_EQ(event.flags, is_last ? Wire::K_FRAME_FLAG_LAST_CHUNK : std::uint8_t{0});
        ++chunks;
    }
    EXPECT_EQ(chunks, K_STREAM_FRAMES);

    // The control frame was queued while chunk 1 was being produced, so chunk 2 follows it.
    EXPECT_GE(chunks_before_control, 1u);
    EXPECT_LE(chunks_before_control, 2u);
}

/**
 * @brief Checks that pop() returns every control frame before any frame of a stream.
 */
TEST(Unit_OutboxTest, ControlComesBeforeStream)
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
TEST(Unit_OutboxTest, StreamsTakeTurns)
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
TEST(Unit_OutboxTest, QueueFullAtCapacity)
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
TEST(Unit_OutboxTest, CloseKeepsControlAndDropsStreams)
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
