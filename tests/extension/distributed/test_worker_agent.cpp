/**************************************************************************/
/* test_worker_agent.cpp                                                  */
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
 * @file test_worker_agent.cpp
 * @brief Tests WorkerAgent through its transport: handshake, dispatch, cache and liveness.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/link_security/handshake.hpp>
#include <SushiRuntime/link_security/sha256.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Bounds every wait on a condition, so a broken agent fails a test and hangs none. */
    constexpr std::chrono::seconds K_WAIT{30};

    /** @brief Holds the connection most tests register the master on. */
    constexpr PeerID K_MASTER{1};

    /** @brief Holds the byte every key byte of these tests holds. */
    constexpr std::uint8_t K_SECRET_BYTE = 0x42;

    /** @brief Holds one frame the agent handed to the transport. */
    struct SentFrame
    {
        PeerID peer;                    /**< The connection it was sent to. */
        MessageType type;               /**< Its type. */
        std::uint8_t flags = 0;         /**< Its flag byte. */
        std::vector<std::byte> payload; /**< Its payload. */
    };

    /**
     * @brief Records what the agent sends and closes, and hands it the master's frames.
     *
     * A stream is pulled dry on the calling thread, or kept until the test releases it.
     */
    class FakeTransport final : public Transport
    {
        public:
            [[nodiscard]] TransportResult start(TransportEvents& events) override
            {
                events_ = &events;
                return TransportResult{};
            }

            /** @brief Returns whether start() reached this transport. */
            [[nodiscard]] bool was_started() const noexcept
            {
                return events_ != nullptr;
            }

            void stop() noexcept override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    ++stops_;
                }
                changed_.notify_all();
            }

            [[nodiscard]] SendStatus send(PeerID peer, MessageType type,
                                          span<const std::byte> payload) override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    frames_.push_back(SentFrame{
                        peer, type, 0, std::vector<std::byte>(payload.begin(), payload.end())});
                }
                changed_.notify_all();
                return SendStatus::Queued;
            }

            [[nodiscard]] SendStatus send_stream(PeerID peer,
                                                 std::unique_ptr<FrameSource> source) override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    if (keep_streams_)
                    {
                        streams_.emplace_back(peer, std::move(source));
                        source.reset();
                    }
                }

                if (source)
                    pull_dry(peer, std::move(source));
                changed_.notify_all();
                return SendStatus::Queued;
            }

            void close_peer(PeerID peer) noexcept override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    closed_.push_back(peer);
                }
                changed_.notify_all();
            }

            [[nodiscard]] TransportExposure exposure() const noexcept override
            {
                return TransportExposure::Loopback;
            }

            [[nodiscard]] TransportStatistics statistics() const noexcept override
            {
                return TransportStatistics{};
            }

            /** @brief Opens a connection to the master, as a worker transport does. */
            void connect(PeerID peer)
            {
                events_->on_peer_connected(peer, PeerDescription{});
            }

            /** @brief Reports a connection lost for a cause. */
            void lose(PeerID peer, PeerLossCause cause)
            {
                PeerLoss loss;
                loss.cause = cause;
                events_->on_peer_lost(peer, loss);
            }

            /** @brief Encodes a message and hands it to the agent as a frame of the master. */
            template <typename Message>
            void deliver(PeerID peer, const Message& message, std::uint8_t flags = 0)
            {
                std::vector<std::byte> payload;
                ASSERT_EQ(message.encode(payload), Wire::CodecStatus::Ok);
                events_->on_frame(peer, Message::K_TYPE, flags,
                                  span<const std::byte>(payload.data(), payload.size()));
            }

            /** @brief Makes send_stream() keep later streams, or pull them dry again. */
            void keep_streams(bool value)
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                keep_streams_ = value;
            }

            /** @brief Pulls every kept stream dry and destroys it, as a writer thread would. */
            void release_streams()
            {
                std::vector<std::pair<PeerID, std::unique_ptr<FrameSource>>> streams;
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    streams.swap(streams_);
                }
                for (auto& [peer, source] : streams)
                    pull_dry(peer, std::move(source));
                changed_.notify_all();
            }

            /** @brief Destroys every kept stream unpulled, as a closed peer would. */
            void drop_streams()
            {
                std::vector<std::pair<PeerID, std::unique_ptr<FrameSource>>> streams;
                const std::lock_guard<std::mutex> lock(mutex_);
                streams.swap(streams_);
            }

            /** @brief Waits until a number of frames of a type were sent. */
            [[nodiscard]] bool wait_for(MessageType type, std::size_t count)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT, [this, type, count]
                                         {
                                             return count_locked(type) >= count;
                                         });
            }

            /** @brief Waits until a number of streams are kept. */
            [[nodiscard]] bool wait_for_streams(std::size_t count)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT, [this, count]
                                         {
                                             return streams_.size() >= count;
                                         });
            }

            /** @brief Waits until a peer was closed. */
            [[nodiscard]] bool wait_for_close(PeerID peer)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT, [this, peer]
                                         {
                                             return is_closed_locked(peer);
                                         });
            }

            /** @brief Waits until stop() was called. */
            [[nodiscard]] bool wait_for_stop()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT, [this] { return stops_ > 0; });
            }

            /** @brief Returns whether a peer was closed. */
            [[nodiscard]] bool is_closed(PeerID peer) const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return is_closed_locked(peer);
            }

            /** @brief Returns the number of frames of a type sent so far. */
            [[nodiscard]] std::size_t count(MessageType type) const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return count_locked(type);
            }

            /** @brief Returns the frames of a type in the order they were sent. */
            [[nodiscard]] std::vector<SentFrame> frames_of(MessageType type) const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                std::vector<SentFrame> frames;
                for (const SentFrame& frame : frames_)
                {
                    if (frame.type == type)
                        frames.push_back(frame);
                }
                return frames;
            }

            /** @brief Returns every frame in the order it was sent. */
            [[nodiscard]] std::vector<SentFrame> frames() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return frames_;
            }

        private:
            /** @brief Records every frame of a stream, then destroys the stream. */
            void pull_dry(PeerID peer, std::unique_ptr<FrameSource> source)
            {
                OutboundFrame frame;
                for (;;)
                {
                    const FrameSourceStatus status = source->next(frame);
                    if (status == FrameSourceStatus::Finished)
                        break;

                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        frames_.push_back(SentFrame{peer, frame.type, frame.flags, frame.payload});
                    }
                    if (status == FrameSourceStatus::FinalFrame)
                        break;
                }
            }

            /** @brief Returns the number of frames of a type; mutex held. */
            [[nodiscard]] std::size_t count_locked(MessageType type) const
            {
                return static_cast<std::size_t>(
                    std::count_if(frames_.begin(), frames_.end(), [type](const SentFrame& frame)
                                  {
                                      return frame.type == type;
                                  }));
            }

            /** @brief Returns whether a peer was closed; mutex held. */
            [[nodiscard]] bool is_closed_locked(PeerID peer) const
            {
                return std::find(closed_.begin(), closed_.end(), peer) != closed_.end();
            }

            /// @brief The agent; set by start().
            TransportEvents* events_ = nullptr;

            /// @brief Guards every member below.
            mutable std::mutex mutex_;

            /// @brief Wakes the waits on a frame, a kept stream, a close and a stop.
            std::condition_variable changed_;

            /// @brief Every frame sent.
            std::vector<SentFrame> frames_;

            /// @brief Streams kept unpulled.
            std::vector<std::pair<PeerID, std::unique_ptr<FrameSource>>> streams_;

            /// @brief Peers closed.
            std::vector<PeerID> closed_;

            /// @brief Whether send_stream() keeps a stream.
            bool keep_streams_ = false;

            /// @brief Calls of stop().
            std::size_t stops_ = 0;
    };

    /** @brief Returns a handle. */
    BufferHandle handle_of(std::uint64_t id, std::uint32_t version, std::uint64_t size_bytes)
    {
        BufferHandle handle;
        handle.id = GlobalBufferID{id};
        handle.version = version;
        handle.size_bytes = size_bytes;
        return handle;
    }

    /** @brief Returns @p count bytes that count up from @p first. */
    std::vector<std::byte> counting_bytes(std::size_t count, std::uint8_t first)
    {
        std::vector<std::byte> bytes(count);
        for (std::size_t index = 0; index < count; ++index)
            bytes[index] = static_cast<std::byte>(first + index);
        return bytes;
    }

    /** @brief Returns a pull-reply chunk that carries part of @p bytes. */
    Wire::BufferChunkMessage pull_chunk(const BufferHandle& handle, std::uint64_t offset,
                                        std::size_t length, const std::vector<std::byte>& bytes)
    {
        Wire::BufferChunkMessage chunk;
        chunk.purpose = Wire::ChunkPurpose::PullReply;
        chunk.handle = handle;
        chunk.offset = offset;
        chunk.data = span<const std::byte>(bytes.data() + offset, length);
        return chunk;
    }

    /** @brief Decodes the payload of a recorded frame; the frame outlives the message. */
    template <typename Message>
    Message decode(const SentFrame& frame)
    {
        Message message;
        const Wire::DecodeResult result = Message::decode(
            span<const std::byte>(frame.payload.data(), frame.payload.size()), message);
        EXPECT_TRUE(result.ok()) << "payload failed to decode at offset " << result.offset;
        return message;
    }

    /** @brief Returns a dispatch of one task. */
    Wire::TaskDispatchMessage dispatch_of(std::uint64_t task_id, OperationID op,
                                          std::vector<BufferHandle> inputs,
                                          std::vector<BufferHandle> outputs)
    {
        Wire::TaskDispatchMessage dispatch;
        dispatch.task_id = task_id;
        dispatch.operation = op;
        dispatch.inputs = std::move(inputs);
        dispatch.outputs = std::move(outputs);
        return dispatch;
    }

    /** @brief Returns a BUFFER_RELEASE of one buffer. */
    Wire::BufferReleaseMessage release_of(std::uint64_t id)
    {
        Wire::BufferReleaseMessage release;
        release.id = GlobalBufferID{id};
        return release;
    }

    /** @brief Returns a nonce whose bytes all hold @p value. */
    HandshakeNonce nonce_of(std::uint8_t value)
    {
        HandshakeNonce nonce{};
        nonce.bytes.fill(value);
        return nonce;
    }

    /** @brief Holds the nonce the master of these tests answers with. */
    const HandshakeNonce K_MASTER_NONCE = nonce_of(0x22);

    /** @brief Returns 32 key bytes that all hold @p value. */
    std::vector<std::uint8_t> secret_of(std::uint8_t value)
    {
        return std::vector<std::uint8_t>(32, value);
    }

    /** @brief Returns the worker nonce a HELLO carries. */
    HandshakeNonce nonce_in(const Wire::HelloMessage& hello)
    {
        HandshakeNonce nonce{};
        std::memcpy(nonce.bytes.data(), hello.worker_nonce.data(), nonce.bytes.size());
        return nonce;
    }

    /** @brief Returns the CHALLENGE a master holding @p secret sends for a worker's nonce. */
    Wire::ChallengeMessage challenge_of(const std::vector<std::uint8_t>& secret,
                                        std::uint64_t session_id,
                                        const HandshakeNonce& worker_nonce)
    {
        const Sha256Digest proof =
            compute_proof(HandshakeRole::Master,
                          span<const std::uint8_t>(secret.data(), secret.size()),
                          worker_nonce, K_MASTER_NONCE);

        Wire::ChallengeMessage challenge;
        std::memcpy(challenge.master_nonce.data(), K_MASTER_NONCE.bytes.data(),
                    challenge.master_nonce.size());
        std::memcpy(challenge.master_proof.data(), proof.data(), challenge.master_proof.size());
        challenge.session_id = session_id;
        return challenge;
    }

    /** @brief Returns a WELCOME with a liveness timeout and a chunk size. */
    Wire::WelcomeMessage welcome_of(PeerID peer, std::uint32_t liveness_timeout_ms,
                                    std::uint32_t chunk_bytes)
    {
        Wire::WelcomeMessage welcome;
        welcome.worker_id = peer.value;
        welcome.liveness_timeout_ms = liveness_timeout_ms;
        welcome.ping_interval_ms = 250;
        welcome.chunk_bytes = chunk_bytes;
        return welcome;
    }

    /** @brief Returns a kernel that counts its runs and writes nothing. */
    KernelRegistry::Kernel counting_kernel(std::atomic<int>& calls)
    {
        return [&calls](sycl::queue&, span<const BufferView>, span<const BufferView>,
                        const NodeOptions&)
        {
            ++calls;
            return sycl::event{};
        };
    }

    /** @brief Returns whether @p condition held before K_WAIT passed. */
    template <typename Condition>
    bool eventually(Condition condition)
    {
        const auto deadline = std::chrono::steady_clock::now() + K_WAIT;
        while (!condition())
        {
            if (std::chrono::steady_clock::now() > deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    /**
     * @brief Lets kernels enter, wait a bounded time for partners, and leave, and keeps the
     *        most that were inside at once.
     */
    class Rendezvous final
    {
        public:
            /**
             * @brief Enters, waits until @p count kernels have entered or @p limit passed,
             *        then leaves.
             */
            void meet(std::size_t count, std::chrono::milliseconds limit)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ++entered_;
                ++inside_;
                most_ = std::max(most_, inside_);
                changed_.notify_all();
                changed_.wait_for(lock, limit, [this, count] { return entered_ >= count; });
                --inside_;
            }

            /** @brief Returns the most kernels that were inside at once. */
            [[nodiscard]] std::size_t most() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return most_;
            }

        private:
            /// @brief Guards the counts.
            mutable std::mutex mutex_;

            /// @brief Wakes the waiters when a kernel enters.
            std::condition_variable changed_;

            /// @brief Kernels that entered.
            std::size_t entered_ = 0;

            /// @brief Kernels inside now.
            std::size_t inside_ = 0;

            /// @brief The most kernels inside at once.
            std::size_t most_ = 0;
    };

    /**
     * @brief Holds a worker agent over a fake transport, and plays its master.
     *
     * A test registers its kernels and sets the options, then calls start().
     */
    class AgentRig final
    {
        public:
            /** @brief Builds the rig; the agent is built by start(). */
            AgentRig()
                : transport(std::make_shared<FakeTransport>()),
                  kernels(make_sushi<KernelRegistry>())
            {
                options.name = "worker-a";
                options.max_jobs = 16;
                options.cache_bytes = 1024 * 1024;
                options.heartbeat_interval = std::chrono::milliseconds(60000);
                const std::vector<std::uint8_t> secret = secret_of(K_SECRET_BYTE);
                options.secret =
                    SharedSecret::from_bytes(span<const std::uint8_t>(secret.data(), secret.size()))
                        .value();
            }

            /** @brief Stops the agent, then drops the kept streams while its cache lives. */
            ~AgentRig()
            {
                if (agent_.has_value())
                    agent_->stop();
                transport->drop_streams();
            }

            AgentRig(const AgentRig&) = delete;
            AgentRig& operator=(const AgentRig&) = delete;
            AgentRig(AgentRig&&) = delete;
            AgentRig& operator=(AgentRig&&) = delete;

            /** @brief Builds and starts the agent with the options and kernels set so far. */
            void start()
            {
                agent_.emplace(runtime_, transport, kernels, std::move(options));
                const TransportResult started = agent_->start();
                ASSERT_TRUE(started.is_ok()) << started.message;
            }

            /** @brief Returns the agent; start() was called. */
            WorkerAgent& agent()
            {
                return *agent_;
            }

            /** @brief Returns the HELLO the agent sent last. */
            Wire::HelloMessage last_hello() const
            {
                const std::vector<SentFrame> hellos = transport->frames_of(MessageType::Hello);
                if (hellos.empty())
                {
                    ADD_FAILURE() << "no HELLO was sent";
                    return Wire::HelloMessage{};
                }
                return decode<Wire::HelloMessage>(hellos.back());
            }

            /**
             * @brief Runs a whole handshake on a new connection.
             * @param secret_byte The byte of the key the master proves.
             */
            void register_master(PeerID peer, std::uint64_t session_id,
                                 std::uint32_t liveness_timeout_ms = 0,
                                 std::uint32_t chunk_bytes = 4096)
            {
                const std::size_t hellos = transport->count(MessageType::Hello);
                const std::size_t proofs = transport->count(MessageType::Proof);
                transport->connect(peer);
                ASSERT_TRUE(transport->wait_for(MessageType::Hello, hellos + 1));
                transport->deliver(peer, challenge_of(secret_of(K_SECRET_BYTE), session_id,
                                                      nonce_in(last_hello())));
                ASSERT_TRUE(transport->wait_for(MessageType::Proof, proofs + 1));
                const std::uint64_t registrations = agent_->statistics().registrations;
                transport->deliver(peer, welcome_of(peer, liveness_timeout_ms, chunk_bytes));
                ASSERT_TRUE(eventually([&]
                                       {
                                           return agent_->statistics().registrations >
                                                  registrations;
                                       }));
            }

            /** @brief Returns the n-th TASK_RESULT sent, counted from 0. */
            [[nodiscard]] Wire::TaskResultMessage result(std::size_t index) const
            {
                const std::vector<SentFrame> frames =
                    transport->frames_of(MessageType::TaskResult);
                if (index >= frames.size())
                {
                    ADD_FAILURE() << "no TASK_RESULT number " << index;
                    return Wire::TaskResultMessage{};
                }
                return decode<Wire::TaskResultMessage>(frames[index]);
            }

            /** @brief Dispatches a task and waits for the result number @p results. */
            void run(PeerID peer, const Wire::TaskDispatchMessage& dispatch, std::size_t results)
            {
                transport->deliver(peer, dispatch);
                ASSERT_TRUE(transport->wait_for(MessageType::TaskResult, results));
            }

            /** @brief Returns the number of devices the worker's runtime has. */
            [[nodiscard]] std::size_t device_count() const
            {
                return runtime_.topology().devices.size();
            }

            /**
             * @brief Returns the first device after device 0 whose context is, or is not, device
             *        0's, where the worker's buffers live.
             * @param shared True for a device in device 0's context, false for one outside it.
             */
            [[nodiscard]] std::optional<std::uint32_t> other_device(bool shared) const
            {
                const AdvancedControls advanced = runtime_.advanced();
                const sycl::context cache_context = advanced.queue(DeviceIndex{0}).get_context();
                for (std::size_t device = 1; device < advanced.device_count(); ++device)
                    if ((advanced.queue(DeviceIndex{device}).get_context() == cache_context) ==
                        shared)
                        return static_cast<std::uint32_t>(device);
                return std::nullopt;
            }

            /** @brief Records what the agent sends; shared with the agent. */
            std::shared_ptr<FakeTransport> transport;

            /** @brief The kernels a test registers before start(). */
            sushi_ptr<KernelRegistry> kernels;

            /** @brief The options start() moves into the agent. */
            WorkerAgentOptions options;

        private:
            /// @brief The runtime the kernels run on.
            Runtime runtime_ = Runtime::create().value();

            /// @brief The agent under test; built by start().
            std::optional<WorkerAgent> agent_;
    };
} // namespace

/** @brief Tests that a BUFFER_RELEASE frees an unpinned buffer and waits for a pinned one. */
TEST(Medium_WorkerBufferCacheTest, ReleaseEvicts)
{
    std::atomic<int> calls{0};
    AgentRig rig;
    const OperationID op(31);
    rig.kernels->register_kernel(op, counting_kernel(calls));
    rig.start();
    rig.register_master(K_MASTER, 7);

    rig.run(K_MASTER, dispatch_of(1, op, {}, {handle_of(1, 1, 64)}), 1);
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 64u);

    rig.transport->deliver(K_MASTER, release_of(1));
    ASSERT_TRUE(eventually([&] { return rig.agent().statistics().cache_bytes == 0; }));
    EXPECT_EQ(rig.agent().statistics().cache_evictions, 1u);

    // A stream that waits to be sent holds its own copy, not the cached buffer.
    rig.transport->keep_streams(true);
    rig.transport->deliver(K_MASTER, dispatch_of(2, op, {}, {handle_of(2, 1, 64)}));
    ASSERT_TRUE(rig.transport->wait_for_streams(1));
    rig.transport->deliver(K_MASTER, release_of(2));
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 0u)
        << "a waiting stream kept its output in the cache";
    EXPECT_EQ(rig.agent().statistics().cache_evictions, 2u);

    rig.transport->keep_streams(false);
    rig.transport->release_streams();
    ASSERT_TRUE(rig.transport->wait_for(MessageType::TaskResult, 2));
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::Succeeded);
}

/** @brief Tests that a full cache evicts the buffer used longest ago, and keeps the one read. */
TEST(Medium_WorkerBufferCacheTest, BudgetEvictsLeastRecentlyUsedUnpinned)
{
    std::atomic<int> calls{0};
    AgentRig rig;
    const OperationID op(32);
    rig.kernels->register_kernel(op, counting_kernel(calls));
    rig.options.cache_bytes = 300;
    rig.start();
    rig.register_master(K_MASTER, 7);

    rig.run(K_MASTER, dispatch_of(1, op, {}, {handle_of(1, 1, 100)}), 1);
    rig.run(K_MASTER, dispatch_of(2, op, {}, {handle_of(2, 1, 100)}), 2);
    rig.run(K_MASTER, dispatch_of(3, op, {}, {handle_of(3, 1, 100)}), 3);
    rig.run(K_MASTER, dispatch_of(4, op, {handle_of(1, 1, 100)}, {}), 4);
    EXPECT_EQ(rig.transport->count(MessageType::BufferRequest), 0u)
        << "a cached input was pulled";

    rig.run(K_MASTER, dispatch_of(5, op, {}, {handle_of(4, 1, 100)}), 5);
    EXPECT_EQ(rig.agent().statistics().cache_evictions, 1u);
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 300u);

    rig.run(K_MASTER, dispatch_of(6, op, {handle_of(1, 1, 100)}, {}), 6);
    EXPECT_EQ(rig.transport->count(MessageType::BufferRequest), 0u)
        << "the buffer used last was evicted";

    const std::vector<std::byte> bytes = counting_bytes(100, 1);
    const BufferHandle second = handle_of(2, 1, 100);
    rig.transport->deliver(K_MASTER, dispatch_of(7, op, {second}, {}));
    ASSERT_TRUE(rig.transport->wait_for(MessageType::BufferRequest, 1));
    EXPECT_EQ(decode<Wire::BufferRequestMessage>(
                  rig.transport->frames_of(MessageType::BufferRequest)[0])
                  .id,
              second.id);
    rig.transport->deliver(K_MASTER, pull_chunk(second, 0, 100, bytes),
                           Wire::K_FRAME_FLAG_LAST_CHUNK);
    ASSERT_TRUE(rig.transport->wait_for(MessageType::TaskResult, 7));
}

/** @brief Tests that a buffer a batch holds pinned is never evicted to make room. */
TEST(Medium_WorkerBufferCacheTest, PinnedEntryIsNotEvicted)
{
    std::atomic<int> calls{0};
    AgentRig rig;
    const OperationID op(33);
    rig.kernels->register_kernel(op, counting_kernel(calls));
    rig.options.cache_bytes = 300;
    rig.start();
    rig.register_master(K_MASTER, 7);

    rig.run(K_MASTER, dispatch_of(1, op, {}, {handle_of(1, 1, 100)}), 1);

    // Task 2 pins its output and its input while the pull is open; tasks 3 and 4 join its batch.
    const BufferHandle input = handle_of(2, 1, 100);
    rig.transport->deliver(K_MASTER, dispatch_of(2, op, {input}, {handle_of(3, 1, 100)}));
    ASSERT_TRUE(rig.transport->wait_for(MessageType::BufferRequest, 1));
    rig.transport->deliver(K_MASTER, dispatch_of(3, op, {}, {handle_of(4, 1, 100)}));
    rig.transport->deliver(K_MASTER, dispatch_of(4, op, {}, {handle_of(5, 1, 100)}));
    ASSERT_TRUE(rig.transport->wait_for(MessageType::TaskAccepted, 4));
    const std::vector<std::byte> bytes = counting_bytes(100, 1);
    rig.transport->deliver(K_MASTER, pull_chunk(input, 0, 100, bytes),
                           Wire::K_FRAME_FLAG_LAST_CHUNK);
    ASSERT_TRUE(rig.transport->wait_for(MessageType::TaskResult, 4));

    // Task 3's output took the room of the one unpinned buffer; task 4 found none.
    EXPECT_EQ(rig.result(1).task_id, 4u);
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::OutputAllocationFailed);
    EXPECT_EQ(rig.result(2).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.result(3).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.agent().statistics().cache_evictions, 1u)
        << "a pinned buffer was evicted to make room";
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 300u);
    EXPECT_EQ(calls.load(), 3);
}

/** @brief Tests that a pulled buffer with a gap is discarded, and a whole pull is kept. */
TEST(Medium_WorkerBufferCacheTest, GapInAPulledBufferDiscardsIt)
{
    std::atomic<bool> is_equal{false};
    const BufferHandle handle = handle_of(9, 3, 12);
    const std::vector<std::byte> bytes = counting_bytes(12, 40);
    AgentRig rig;
    const OperationID op(34);
    rig.kernels->register_kernel(
        op, KernelRegistry::Kernel([&is_equal, &bytes](sycl::queue&,
                                                       span<const BufferView> inputs,
                                                       span<const BufferView>,
                                                       const NodeOptions&)
                                   {
                                       is_equal = inputs[0].size_bytes == bytes.size() &&
                                                  std::memcmp(inputs[0].data, bytes.data(),
                                                              bytes.size()) == 0;
                                       return sycl::event{};
                                   }));
    rig.start();
    rig.register_master(K_MASTER, 7);

    rig.transport->deliver(K_MASTER, dispatch_of(1, op, {handle}, {}));
    ASSERT_TRUE(rig.transport->wait_for(MessageType::BufferRequest, 1));
    rig.transport->deliver(K_MASTER, pull_chunk(handle, 0, 4, bytes));
    rig.transport->deliver(K_MASTER, pull_chunk(handle, 8, 4, bytes),
                           Wire::K_FRAME_FLAG_LAST_CHUNK);
    ASSERT_TRUE(rig.transport->wait_for(MessageType::TaskResult, 1));
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::InputUnavailable);
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 0u) << "a discarded transfer kept its memory";

    rig.transport->deliver(K_MASTER, dispatch_of(2, op, {handle}, {}));
    ASSERT_TRUE(rig.transport->wait_for(MessageType::BufferRequest, 2));
    rig.transport->deliver(K_MASTER, pull_chunk(handle, 0, 4, bytes));
    rig.transport->deliver(K_MASTER, pull_chunk(handle, 4, 4, bytes));
    rig.transport->deliver(K_MASTER, pull_chunk(handle, 8, 4, bytes),
                           Wire::K_FRAME_FLAG_LAST_CHUNK);
    ASSERT_TRUE(rig.transport->wait_for(MessageType::TaskResult, 2));
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_TRUE(is_equal.load()) << "the kernel read other bytes than the master sent";
}

/** @brief Tests that an output the cache cannot hold fails the task, and the next one runs. */
TEST(Medium_DispatchExecutorTest, OutputAllocationFailureFailsTheTask)
{
    std::atomic<int> calls{0};
    AgentRig rig;
    const OperationID op(12);
    rig.kernels->register_kernel(op, counting_kernel(calls));
    rig.options.cache_bytes = 32;
    rig.start();
    rig.register_master(K_MASTER, 7);

    rig.run(K_MASTER, dispatch_of(1, op, {}, {handle_of(7, 1, 64)}), 1);
    EXPECT_EQ(rig.result(0).task_id, 1u);
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::OutputAllocationFailed);
    EXPECT_EQ(calls.load(), 0);
    EXPECT_EQ(rig.transport->count(MessageType::BufferChunk), 0u);

    rig.run(K_MASTER, dispatch_of(2, op, {}, {handle_of(7, 2, 16)}), 2);
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::Succeeded);

    const WorkerStatistics statistics = rig.agent().statistics();
    EXPECT_EQ(statistics.tasks_failed_output_allocation, 1u);
    EXPECT_EQ(statistics.tasks_run, 1u);
}

/** @brief Tests that a kernel sees each buffer's size and its output leaves in chunks. */
TEST(Medium_DispatchExecutorTest, KernelSeesBufferSizes)
{
    std::atomic<std::size_t> input_size{0};
    std::atomic<std::size_t> output_size{0};
    AgentRig rig;
    const OperationID op(13);
    rig.kernels->register_kernel(
        op, KernelRegistry::Kernel(
                [&input_size, &output_size](sycl::queue&, span<const BufferView> inputs,
                                            span<const BufferView> outputs,
                                            const NodeOptions&)
                {
                    input_size = inputs[0].size_bytes;
                    output_size = outputs[0].size_bytes;
                    std::memset(outputs[0].data, 0, outputs[0].size_bytes);
                    std::memcpy(outputs[0].data, inputs[0].data, inputs[0].size_bytes);
                    return sycl::event{};
                }));
    rig.start();
    rig.register_master(K_MASTER, 7, 0, 8);

    const BufferHandle input = handle_of(3, 5, 12);
    const BufferHandle output = handle_of(4, 1, 20);
    const std::vector<std::byte> bytes = counting_bytes(12, 1);
    rig.transport->deliver(K_MASTER, dispatch_of(1, op, {input}, {output}));

    ASSERT_TRUE(rig.transport->wait_for(MessageType::BufferRequest, 1));
    const Wire::BufferRequestMessage request = decode<Wire::BufferRequestMessage>(
        rig.transport->frames_of(MessageType::BufferRequest)[0]);
    EXPECT_EQ(request.id, input.id);
    EXPECT_EQ(request.version, input.version);

    rig.transport->deliver(K_MASTER, pull_chunk(input, 0, 8, bytes));
    rig.transport->deliver(K_MASTER, pull_chunk(input, 8, 4, bytes),
                           Wire::K_FRAME_FLAG_LAST_CHUNK);

    ASSERT_TRUE(rig.transport->wait_for(MessageType::TaskResult, 1));
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(input_size.load(), 12u);
    EXPECT_EQ(output_size.load(), 20u);

    const std::vector<SentFrame> chunks = rig.transport->frames_of(MessageType::BufferChunk);
    ASSERT_EQ(chunks.size(), 3u);
    const Wire::BufferChunkMessage first = decode<Wire::BufferChunkMessage>(chunks[0]);
    EXPECT_EQ(first.purpose, Wire::ChunkPurpose::TaskOutput);
    EXPECT_EQ(first.handle.size_bytes, 20u);
    ASSERT_EQ(first.data.size(), 8u);
    EXPECT_EQ(std::memcmp(first.data.data(), bytes.data(), 8), 0);
}

/** @brief Tests that two dispatches that name two devices run on those two devices. */
TEST(Medium_DispatchExecutorTest, DispatchRunsOnTheDeviceItNames)
{
    AgentRig rig;
    const std::optional<std::uint32_t> second_device = rig.other_device(true);
    if (!second_device)
        GTEST_SKIP() << "No second device shares device 0's context.";

    std::mutex mutex;
    std::vector<sycl::device> ran_on;
    const OperationID op(16);
    rig.kernels->register_kernel(
        op, KernelRegistry::Kernel(
                [&mutex, &ran_on](sycl::queue& queue, span<const BufferView>,
                                  span<const BufferView> outputs, const NodeOptions&)
                {
                    std::memset(outputs[0].data, 0, outputs[0].size_bytes);
                    const std::lock_guard<std::mutex> lock(mutex);
                    ran_on.push_back(queue.get_device());
                    return sycl::event{};
                }));
    rig.start();
    rig.register_master(K_MASTER, 7);

    Wire::TaskDispatchMessage first = dispatch_of(1, op, {}, {handle_of(1, 1, 16)});
    first.device_index = 0;
    rig.run(K_MASTER, first, 1);
    Wire::TaskDispatchMessage second = dispatch_of(2, op, {}, {handle_of(2, 1, 16)});
    second.device_index = *second_device;
    rig.run(K_MASTER, second, 2);

    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::Succeeded);
    const std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(ran_on.size(), 2u);
    EXPECT_NE(ran_on[0], ran_on[1]);
}

/** @brief Tests that a dispatch naming a device the worker lacks fails as KernelFailed. */
TEST(Medium_DispatchExecutorTest, DispatchNamingAMissingDeviceFailsTheTask)
{
    std::atomic<int> calls{0};
    AgentRig rig;
    const OperationID op(17);
    rig.kernels->register_kernel(op, counting_kernel(calls));
    rig.start();
    rig.register_master(K_MASTER, 7);

    Wire::TaskDispatchMessage missing = dispatch_of(1, op, {}, {handle_of(1, 1, 16)});
    missing.device_index = static_cast<std::uint32_t>(rig.device_count());
    rig.run(K_MASTER, missing, 1);
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::KernelFailed);
    EXPECT_EQ(calls.load(), 0);
    EXPECT_EQ(rig.transport->count(MessageType::BufferChunk), 0u);

    rig.run(K_MASTER, dispatch_of(2, op, {}, {handle_of(1, 2, 16)}), 2);
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(calls.load(), 1);
}

/** @brief Tests that a dispatch naming a device outside device 0's context fails the task. */
TEST(Medium_DispatchExecutorTest, DispatchOnADeviceOutsideTheCacheContextFailsTheTask)
{
    std::atomic<int> calls{0};
    AgentRig rig;
    const std::optional<std::uint32_t> outside = rig.other_device(false);
    if (!outside)
        GTEST_SKIP() << "No device lies outside device 0's context.";
    const OperationID op(20);
    rig.kernels->register_kernel(op, counting_kernel(calls));
    rig.start();
    rig.register_master(K_MASTER, 7);

    Wire::TaskDispatchMessage dispatch = dispatch_of(1, op, {}, {handle_of(1, 1, 16)});
    dispatch.device_index = *outside;
    rig.run(K_MASTER, dispatch, 1);
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::KernelFailed);
    EXPECT_EQ(calls.load(), 0);
    EXPECT_EQ(rig.transport->count(MessageType::BufferChunk), 0u);
}

/** @brief Tests that a generic lambda registers as a kernel and runs. */
TEST(Medium_DispatchExecutorTest, GenericLambdaRegistersAsAKernel)
{
    AgentRig rig;
    const OperationID op(14);
    rig.kernels->register_kernel(
        op, [](auto& /*queue*/, auto /*inputs*/, auto outputs, const auto& /*metadata*/)
            {
                std::memset(outputs[0].data, 0x5a, outputs[0].size_bytes);
                return sycl::event{};
            });
    rig.start();
    rig.register_master(K_MASTER, 7);

    rig.run(K_MASTER, dispatch_of(1, op, {}, {handle_of(8, 1, 4)}), 1);
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::Succeeded);

    const std::vector<SentFrame> chunks = rig.transport->frames_of(MessageType::BufferChunk);
    ASSERT_EQ(chunks.size(), 1u);
    const Wire::BufferChunkMessage chunk = decode<Wire::BufferChunkMessage>(chunks[0]);
    ASSERT_EQ(chunk.data.size(), 4u);
    for (const std::byte byte : chunk.data)
        EXPECT_EQ(byte, std::byte{0x5a});
}

/** @brief Tests that a kernel that throws fails its task and the next task still runs. */
TEST(Medium_DispatchExecutorTest, KernelThatThrowsFailsTheTaskAndTheWorkerLives)
{
    std::atomic<int> calls{0};
    AgentRig rig;
    const OperationID op(15);
    rig.kernels->register_kernel(
        op, KernelRegistry::Kernel([&calls](sycl::queue&, span<const BufferView>,
                                            span<const BufferView>, const NodeOptions&)
                                   {
                                       if (++calls == 1)
                                           throw std::runtime_error("kernel refused");
                                       return sycl::event{};
                                   }));
    rig.start();
    rig.register_master(K_MASTER, 7);

    rig.run(K_MASTER, dispatch_of(1, op, {}, {handle_of(5, 1, 16)}), 1);
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::KernelFailed);
    EXPECT_TRUE(rig.result(0).outputs.empty());
    EXPECT_EQ(rig.transport->count(MessageType::BufferChunk), 0u);

    rig.run(K_MASTER, dispatch_of(2, op, {}, {handle_of(5, 2, 16)}), 2);
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::Succeeded);

    const WorkerStatistics statistics = rig.agent().statistics();
    EXPECT_EQ(statistics.tasks_failed_kernel, 1u);
    EXPECT_EQ(statistics.tasks_run, 1u);
}

/** @brief Tests that an output's chunks come before its TASK_RESULT, which ends the stream. */
TEST(Medium_DispatchExecutorTest, OutputStreamEndsWithTheResult)
{
    AgentRig rig;
    const OperationID op(16);
    const std::vector<std::byte> bytes = counting_bytes(10, 100);
    rig.kernels->register_kernel(
        op, KernelRegistry::Kernel([&bytes](sycl::queue&, span<const BufferView>,
                                            span<const BufferView> outputs,
                                            const NodeOptions&)
                                   {
                                       std::memcpy(outputs[0].data, bytes.data(), bytes.size());
                                       return sycl::event{};
                                   }));
    rig.start();
    rig.register_master(K_MASTER, 7, 0, 4);

    rig.transport->keep_streams(true);
    const BufferHandle output = handle_of(5, 2, 10);
    rig.transport->deliver(K_MASTER, dispatch_of(9, op, {}, {output}));
    ASSERT_TRUE(rig.transport->wait_for_streams(1));

    EXPECT_EQ(rig.transport->count(MessageType::TaskResult), 0u);
    // The stream sends its own copy, so a release frees the cached output at once.
    rig.transport->deliver(K_MASTER, release_of(5));
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 0u)
        << "the stream kept the cached output pinned";

    rig.transport->keep_streams(false);
    rig.transport->release_streams();

    std::vector<SentFrame> frames;
    for (const SentFrame& frame : rig.transport->frames())
    {
        if (frame.type == MessageType::TaskAccepted || frame.type == MessageType::BufferChunk ||
            frame.type == MessageType::TaskResult)
            frames.push_back(frame);
    }
    ASSERT_EQ(frames.size(), 5u);
    EXPECT_EQ(frames[0].type, MessageType::TaskAccepted);
    const std::size_t lengths[] = {4, 4, 2};
    std::uint64_t offset = 0;
    for (std::size_t index = 0; index < 3; ++index)
    {
        const SentFrame& frame = frames[index + 1];
        ASSERT_EQ(frame.type, MessageType::BufferChunk);
        const Wire::BufferChunkMessage chunk = decode<Wire::BufferChunkMessage>(frame);
        EXPECT_EQ(chunk.task_id, 9u);
        EXPECT_EQ(chunk.handle.version, output.version);
        EXPECT_EQ(chunk.offset, offset);
        ASSERT_EQ(chunk.data.size(), lengths[index]);
        EXPECT_EQ(std::memcmp(chunk.data.data(), bytes.data() + offset, lengths[index]), 0);
        EXPECT_EQ(frame.flags, index == 2 ? Wire::K_FRAME_FLAG_LAST_CHUNK : 0);
        offset += lengths[index];
    }

    ASSERT_EQ(frames[4].type, MessageType::TaskResult);
    const Wire::TaskResultMessage result = decode<Wire::TaskResultMessage>(frames[4]);
    EXPECT_EQ(result.status, Wire::TaskResultStatus::Succeeded);
    ASSERT_EQ(result.outputs.size(), 1u);
    EXPECT_EQ(result.outputs[0].id, output.id);

    EXPECT_EQ(rig.agent().statistics().cache_bytes, 0u);
}

namespace
{
/**
 * @brief Delivers two dispatches so that the worker records them in one run: the first waits
 *        for its input while the second is queued, then the input arrives.
 * @param input The first dispatch's only input; @p bytes holds its size_bytes.
 */
void deliver_as_one_batch(AgentRig& rig, const Wire::TaskDispatchMessage& first,
                          const Wire::TaskDispatchMessage& second, const BufferHandle& input,
                          const std::vector<std::byte>& bytes)
{
    const std::size_t requests = rig.transport->count(MessageType::BufferRequest);
    const std::size_t accepted = rig.transport->count(MessageType::TaskAccepted);
    rig.transport->deliver(K_MASTER, first);
    ASSERT_TRUE(rig.transport->wait_for(MessageType::BufferRequest, requests + 1));
    rig.transport->deliver(K_MASTER, second);
    ASSERT_TRUE(rig.transport->wait_for(MessageType::TaskAccepted, accepted + 2));
    rig.transport->deliver(K_MASTER,
                           pull_chunk(input, 0, static_cast<std::size_t>(input.size_bytes), bytes),
                           Wire::K_FRAME_FLAG_LAST_CHUNK);
}

/** @brief Returns the data of the first output chunk a task sent. */
std::vector<std::byte> first_output_of(const AgentRig& rig, std::uint64_t task_id)
{
    for (const SentFrame& frame : rig.transport->frames_of(MessageType::BufferChunk))
    {
        const Wire::BufferChunkMessage chunk = decode<Wire::BufferChunkMessage>(frame);
        if (chunk.task_id == task_id)
            return std::vector<std::byte>(chunk.data.begin(), chunk.data.end());
    }
    ADD_FAILURE() << "task " << task_id << " sent no output chunk";
    return {};
}
} // namespace

/** @brief Tests that two dispatches over disjoint buffers have their kernels in flight at once. */
TEST(Medium_DispatchExecutorTest, DispatchesOverDisjointBuffersAreInFlightTogether)
{
    AgentRig rig;
    Rendezvous rendezvous;
    std::mutex mutex;
    std::vector<sycl::device> ran_on;
    const OperationID op(19);
    rig.kernels->register_kernel(
        op, KernelRegistry::Kernel(
                [&](sycl::queue& queue, span<const BufferView>, span<const BufferView> outputs,
                    const NodeOptions&)
                {
                    {
                        const std::lock_guard<std::mutex> lock(mutex);
                        ran_on.push_back(queue.get_device());
                    }
                    rendezvous.meet(
                        2, std::chrono::duration_cast<std::chrono::milliseconds>(K_WAIT));
                    std::memset(outputs[0].data, 0, outputs[0].size_bytes);
                    return sycl::event{};
                }));
    rig.start();
    rig.register_master(K_MASTER, 7);

    // A second device of device 0's context when the worker has one; otherwise device 0 twice.
    const std::uint32_t second_device = rig.other_device(true).value_or(0u);
    const BufferHandle input = handle_of(3, 1, 8);
    Wire::TaskDispatchMessage first = dispatch_of(1, op, {input}, {handle_of(4, 1, 16)});
    first.device_index = 0;
    Wire::TaskDispatchMessage second = dispatch_of(2, op, {}, {handle_of(5, 1, 16)});
    second.device_index = second_device;
    deliver_as_one_batch(rig, first, second, input, counting_bytes(8, 1));

    ASSERT_TRUE(rig.transport->wait_for(MessageType::TaskResult, 2));
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rendezvous.most(), 2u) << "the second kernel entered only after the first left";
    if (second_device != 0u)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        ASSERT_EQ(ran_on.size(), 2u);
        EXPECT_NE(ran_on[0], ran_on[1]);
    }
}

/** @brief Tests that two dispatches writing one buffer run in order, each sending its own bytes. */
TEST(Medium_DispatchExecutorTest, DispatchesWritingOneBufferRunOneAfterTheOther)
{
    AgentRig rig;
    Rendezvous rendezvous;
    std::mutex mutex;
    std::vector<std::size_t> order;
    const OperationID op(20);
    rig.kernels->register_kernel(
        op, KernelRegistry::Kernel(
                [&](sycl::queue&, span<const BufferView> inputs, span<const BufferView> outputs,
                    const NodeOptions&)
                {
                    // A partner that could overlap would enter within this bound.
                    rendezvous.meet(2, std::chrono::milliseconds(250));
                    const std::byte fill = inputs.size() == 1 ? std::byte{0x11} : std::byte{0x22};
                    std::memset(outputs[0].data, static_cast<int>(fill), outputs[0].size_bytes);
                    const std::lock_guard<std::mutex> lock(mutex);
                    order.push_back(inputs.size());
                    return sycl::event{};
                }));
    rig.start();
    rig.register_master(K_MASTER, 7);

    const BufferHandle input = handle_of(3, 1, 8);
    const Wire::TaskDispatchMessage first = dispatch_of(1, op, {input}, {handle_of(6, 1, 16)});
    const Wire::TaskDispatchMessage second = dispatch_of(2, op, {}, {handle_of(6, 2, 16)});
    deliver_as_one_batch(rig, first, second, input, counting_bytes(8, 1));

    ASSERT_TRUE(rig.transport->wait_for(MessageType::TaskResult, 2));
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rendezvous.most(), 1u) << "the two writers of buffer 6 overlapped";
    {
        const std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(order, (std::vector<std::size_t>{1, 0})) << "the writers ran out of order";
    }

    EXPECT_EQ(first_output_of(rig, 1), std::vector<std::byte>(16, std::byte{0x11}))
        << "the first task sent bytes the second wrote";
    EXPECT_EQ(first_output_of(rig, 2), std::vector<std::byte>(16, std::byte{0x22}));
}

/** @brief Tests that two dispatches of one run that read one input version pull it once. */
TEST(Medium_DispatchExecutorTest, OneRunPullsAnInputTwoDispatchesReadOnce)
{
    std::atomic<int> calls{0};
    AgentRig rig;
    const OperationID op(21);
    rig.kernels->register_kernel(op, counting_kernel(calls));
    rig.start();
    rig.register_master(K_MASTER, 7);

    const BufferHandle input = handle_of(3, 1, 8);
    deliver_as_one_batch(rig, dispatch_of(1, op, {input}, {}), dispatch_of(2, op, {input}, {}),
                         input, counting_bytes(8, 1));

    ASSERT_TRUE(rig.transport->wait_for(MessageType::TaskResult, 2));
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.transport->count(MessageType::BufferRequest), 1u);
    EXPECT_EQ(calls.load(), 2);
}

/** @brief Tests that a dispatch reading an output of a queued one waits for it, not the master. */
TEST(Medium_DispatchExecutorTest, ADispatchReadingAQueuedOutputRunsAfterIt)
{
    AgentRig rig;
    const OperationID write(22);
    const OperationID copy(23);
    rig.kernels->register_kernel(
        write, KernelRegistry::Kernel([](sycl::queue&, span<const BufferView>,
                                         span<const BufferView> outputs, const NodeOptions&)
                                      {
                                          std::memset(outputs[0].data, 0x33, outputs[0].size_bytes);
                                          return sycl::event{};
                                      }));
    rig.kernels->register_kernel(
        copy, KernelRegistry::Kernel([](sycl::queue&, span<const BufferView> inputs,
                                        span<const BufferView> outputs, const NodeOptions&)
                                     {
                                         std::memcpy(outputs[0].data, inputs[0].data,
                                                     outputs[0].size_bytes);
                                         return sycl::event{};
                                     }));
    rig.start();
    rig.register_master(K_MASTER, 7);

    const BufferHandle input = handle_of(3, 1, 8);
    const BufferHandle produced = handle_of(7, 1, 16);
    deliver_as_one_batch(rig, dispatch_of(1, write, {input}, {produced}),
                         dispatch_of(2, copy, {produced}, {handle_of(8, 1, 16)}), input,
                         counting_bytes(8, 1));

    ASSERT_TRUE(rig.transport->wait_for(MessageType::TaskResult, 2));
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.transport->count(MessageType::BufferRequest), 1u)
        << "the reader asked the master for an output this worker makes";
    EXPECT_EQ(first_output_of(rig, 2), std::vector<std::byte>(16, std::byte{0x33}));
}

/** @brief Tests that losing the master ends a pull that has no idle timeout. */
TEST(Medium_DispatchExecutorTest, DisconnectEndsAnOpenInputPull)
{
    std::atomic<int> calls{0};
    AgentRig rig;
    const OperationID op(18);
    rig.kernels->register_kernel(op, counting_kernel(calls));
    // With no idle timeout the pull ends only when something ends it.
    rig.options.pull_idle_timeout = std::chrono::milliseconds(0);
    rig.start();
    rig.register_master(K_MASTER, 7);

    rig.transport->deliver(K_MASTER, dispatch_of(1, op, {handle_of(3, 5, 12)}, {}));
    ASSERT_TRUE(rig.transport->wait_for(MessageType::BufferRequest, 1));

    rig.transport->lose(K_MASTER, PeerLossCause::ClosedByPeer);

    ASSERT_TRUE(eventually([&]
                           {
                               return rig.agent().statistics().tasks_failed_input_unavailable == 1;
                           }))
        << "the exec thread still waits for an input no master will send";
    EXPECT_EQ(calls.load(), 0);
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 0u) << "the abandoned pull kept its allocation";

    // The exec thread is free again: a task of the next connection runs.
    const PeerID next{2};
    rig.register_master(next, 7);
    const std::size_t results = rig.transport->count(MessageType::TaskResult);
    rig.run(next, dispatch_of(2, op, {}, {}), results + 1);
    EXPECT_EQ(rig.result(results).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(calls.load(), 1);
}

/** @brief Tests that the worker checks the master's proof before it sends its own. */
TEST(Medium_MasterLinkTest, HandshakeChecksTheMasterBeforeProving)
{
    std::atomic<int> calls{0};
    AgentRig rig;
    rig.kernels->register_kernel(OperationID(7), counting_kernel(calls));
    rig.kernels->register_kernel(OperationID(9), counting_kernel(calls));
    rig.options.cache_bytes = 4096;
    rig.start();

    rig.transport->connect(K_MASTER);
    ASSERT_TRUE(rig.transport->wait_for(MessageType::Hello, 1));
    const Wire::HelloMessage hello = rig.last_hello();
    EXPECT_EQ(hello.node_name, "worker-a");
    EXPECT_FALSE(hello.build_id.empty());
    EXPECT_EQ(rig.transport->count(MessageType::Proof), 0u) << "PROOF left before the CHALLENGE";

    const std::vector<std::uint8_t> secret = secret_of(K_SECRET_BYTE);
    rig.transport->deliver(K_MASTER, challenge_of(secret, 7, nonce_in(hello)));
    ASSERT_TRUE(rig.transport->wait_for(MessageType::Proof, 1));
    const Wire::ProofMessage proof =
        decode<Wire::ProofMessage>(rig.transport->frames_of(MessageType::Proof)[0]);
    const Sha256Digest expected =
        compute_proof(HandshakeRole::Worker, span<const std::uint8_t>(secret.data(), secret.size()),
                      nonce_in(hello), K_MASTER_NONCE);
    EXPECT_EQ(std::memcmp(proof.worker_proof.data(), expected.data(), expected.size()), 0);
    EXPECT_EQ(proof.ops.size(), 2u);
    EXPECT_EQ(proof.max_jobs, 16u);
    EXPECT_EQ(proof.cache_budget, 4096u);
    EXPECT_EQ(rig.agent().statistics().registrations, 0u);

    rig.transport->deliver(K_MASTER, welcome_of(K_MASTER, 5000, 4096));
    ASSERT_TRUE(eventually([&] { return rig.agent().statistics().registrations == 1; }));
    EXPECT_EQ(rig.agent().wait_for_end(std::chrono::milliseconds(0)), WorkerEnd::Running);
}

/** @brief Tests that a master proving another key ends the worker before it proves anything. */
TEST(Medium_MasterLinkTest, WrongMasterProofEndsTheLink)
{
    AgentRig rig;
    rig.start();

    rig.transport->connect(K_MASTER);
    ASSERT_TRUE(rig.transport->wait_for(MessageType::Hello, 1));
    rig.transport->deliver(K_MASTER, challenge_of(secret_of(0x43), 7, nonce_in(rig.last_hello())));
    EXPECT_EQ(rig.agent().wait_for_end(K_WAIT), WorkerEnd::RejectedAuthentication);
    EXPECT_EQ(rig.transport->count(MessageType::Proof), 0u) << "PROOF went to an unproven master";
    EXPECT_TRUE(rig.transport->is_closed(K_MASTER));

    const PeerID next{2};
    rig.transport->connect(next);
    EXPECT_EQ(rig.transport->count(MessageType::Hello), 1u) << "an ended link sent HELLO";
    EXPECT_TRUE(rig.transport->is_closed(next));
}

/** @brief Tests that an ended link stops its transport, so it connects no more. */
TEST(Medium_MasterLinkTest, EndedLinkStopsItsTransport)
{
    AgentRig rig;
    rig.start();

    rig.transport->connect(K_MASTER);
    ASSERT_TRUE(rig.transport->wait_for(MessageType::Hello, 1));
    rig.transport->deliver(K_MASTER, challenge_of(secret_of(0x43), 7, nonce_in(rig.last_hello())));

    ASSERT_TRUE(rig.transport->wait_for_stop())
        << "the transport of a rejected link keeps connecting";
    EXPECT_EQ(rig.agent().wait_for_end(K_WAIT), WorkerEnd::RejectedAuthentication);
}

/** @brief Tests that a worker answers PING only once WELCOME registered it. */
TEST(Medium_MasterLinkTest, PingGetsNoPongBeforeRegistration)
{
    AgentRig rig;
    rig.start();
    Wire::PingMessage ping;
    ping.token = 99;

    // The master registered this worker on PROOF, but its WELCOME has not arrived.
    rig.transport->connect(K_MASTER);
    ASSERT_TRUE(rig.transport->wait_for(MessageType::Hello, 1));
    rig.transport->deliver(K_MASTER,
                           challenge_of(secret_of(K_SECRET_BYTE), 7, nonce_in(rig.last_hello())));
    ASSERT_TRUE(rig.transport->wait_for(MessageType::Proof, 1));
    rig.transport->deliver(K_MASTER, ping);
    EXPECT_EQ(rig.transport->count(MessageType::Pong), 0u)
        << "a link that is not registered told the master it is alive";

    rig.transport->deliver(K_MASTER, welcome_of(K_MASTER, 0, 4096));
    ASSERT_TRUE(eventually([&] { return rig.agent().statistics().registrations == 1; }));
    rig.transport->deliver(K_MASTER, ping);

    ASSERT_TRUE(rig.transport->wait_for(MessageType::Pong, 1));
    EXPECT_EQ(decode<Wire::PongMessage>(rig.transport->frames_of(MessageType::Pong)[0]).token,
              99u);
}

/** @brief Tests that a CHALLENGE for a HELLO the link forgot is ignored, not taken as wrong. */
TEST(Medium_MasterLinkTest, LateChallengeForAForgottenHelloIsIgnored)
{
    constexpr std::size_t K_HELLOS = 5;
    AgentRig rig;
    rig.options.heartbeat_interval = std::chrono::milliseconds(1);
    rig.start();

    // The link remembers four nonces; the fifth HELLO forgets the nonce of the first.
    rig.transport->connect(K_MASTER);
    ASSERT_TRUE(rig.transport->wait_for(MessageType::Hello, K_HELLOS));
    const HandshakeNonce first =
        nonce_in(decode<Wire::HelloMessage>(rig.transport->frames_of(MessageType::Hello)[0]));

    rig.transport->deliver(K_MASTER, challenge_of(secret_of(K_SECRET_BYTE), 7, first));
    EXPECT_EQ(rig.transport->count(MessageType::Proof), 0u);
    EXPECT_FALSE(rig.transport->is_closed(K_MASTER));
    EXPECT_EQ(rig.agent().wait_for_end(std::chrono::milliseconds(0)), WorkerEnd::Running)
        << "a CHALLENGE for a forgotten HELLO was taken for a wrong secret";
}

/** @brief Tests that an idle worker's HEARTBEAT names the task id it waits for next. */
TEST(Medium_MasterLinkTest, IdleHeartbeatCarriesTheNextTaskID)
{
    std::atomic<int> calls{0};
    AgentRig rig;
    const OperationID op(17);
    rig.kernels->register_kernel(op, counting_kernel(calls));
    rig.options.heartbeat_interval = std::chrono::milliseconds(5);
    rig.start();
    rig.register_master(K_MASTER, 7);

    rig.run(K_MASTER, dispatch_of(41, op, {}, {}), 1);
    const std::size_t before = rig.transport->count(MessageType::Heartbeat);
    ASSERT_TRUE(rig.transport->wait_for(MessageType::Heartbeat, before + 1));

    const Wire::HeartbeatMessage heartbeat = decode<Wire::HeartbeatMessage>(
        rig.transport->frames_of(MessageType::Heartbeat).back());
    EXPECT_EQ(heartbeat.oldest_held_task_id, 42u);
    EXPECT_EQ(heartbeat.queue_depth, 0u);
}

/** @brief Tests that a master silent past its liveness timeout is closed, and may come back. */
TEST(Medium_MasterLinkTest, SilentMasterIsLost)
{
    AgentRig rig;
    rig.start();

    rig.register_master(K_MASTER, 7, 40);
    ASSERT_TRUE(rig.transport->wait_for_close(K_MASTER)) << "a silent master was never closed";

    rig.transport->lose(K_MASTER, PeerLossCause::ClosedLocally);
    EXPECT_EQ(rig.agent().wait_for_end(std::chrono::milliseconds(0)), WorkerEnd::Running)
        << "a lost master ended a link that may reconnect";
}

/** @brief Tests that a new master session clears the cache and the ids seen, a reconnect not. */
TEST(Medium_MasterLinkTest, NewSessionClearsTheCache)
{
    std::atomic<int> calls{0};
    AgentRig rig;
    const OperationID op(35);
    rig.kernels->register_kernel(op, counting_kernel(calls));
    rig.start();

    rig.register_master(PeerID{1}, 7);
    rig.run(PeerID{1}, dispatch_of(5, op, {}, {handle_of(1, 1, 32)}), 1);
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 32u);

    rig.transport->lose(PeerID{1}, PeerLossCause::ClosedByPeer);
    rig.register_master(PeerID{2}, 7);
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 32u)
        << "the cache was cleared for the session it was filled in";
    rig.transport->deliver(PeerID{2}, dispatch_of(5, op, {}, {handle_of(1, 2, 32)}));
    rig.run(PeerID{2}, dispatch_of(6, op, {}, {}), 2);
    EXPECT_EQ(calls.load(), 2) << "a dispatch repeated in one session ran twice";

    rig.transport->lose(PeerID{2}, PeerLossCause::ClosedByPeer);
    rig.register_master(PeerID{3}, 8);
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 0u);
    rig.run(PeerID{3}, dispatch_of(5, op, {}, {}), 3);
    EXPECT_EQ(calls.load(), 3) << "a new session kept the ids of the old one";
    EXPECT_EQ(rig.agent().statistics().reconnects, 2u);
}

/** @brief Tests that the worker's own close after GOODBYE does not end it as MasterLost. */
TEST(Medium_MasterLinkTest, LossAfterGoodbyeIsTheWorkersOwn)
{
    AgentRig rig;
    rig.start();
    rig.register_master(K_MASTER, 7);

    rig.agent().stop();
    EXPECT_EQ(rig.transport->count(MessageType::Goodbye), 1u);

    rig.transport->lose(K_MASTER, PeerLossCause::ClosedLocally);
    EXPECT_EQ(rig.agent().wait_for_end(std::chrono::milliseconds(0)), WorkerEnd::Stopped)
        << "a leaving worker ended as MasterLost";
}

/** @brief Tests that a worker name HELLO cannot carry is refused before the transport starts. */
TEST(Medium_WorkerAgentTest, Regression_ANameAbove255BytesIsRefusedBeforeConnecting)
{
    Runtime runtime = Runtime::create().value();
    const auto transport = std::make_shared<FakeTransport>();
    WorkerAgentOptions options;
    options.name = std::string(256, 'n');
    WorkerAgent agent(runtime, transport, make_sushi<KernelRegistry>(), std::move(options));

    const TransportResult started = agent.start();
    EXPECT_EQ(started.status, TransportStatus::InvalidArgument);
    EXPECT_NE(started.message.find("worker name"), std::string::npos) << started.message;
    EXPECT_NE(started.message.find("255"), std::string::npos) << started.message;
    EXPECT_FALSE(transport->was_started());
}

/** @brief Tests that a label value HELLO cannot carry is refused, naming the label. */
TEST(Medium_WorkerAgentTest, Regression_ALabelValueAbove255BytesIsRefusedBeforeConnecting)
{
    RuntimeConfig config;
    config.labels = {NodeLabel{"zone", std::string(300, 'v')}};
    Runtime runtime = Runtime::create(config).value();
    const auto transport = std::make_shared<FakeTransport>();
    WorkerAgentOptions options;
    options.name = "worker-a";
    WorkerAgent agent(runtime, transport, make_sushi<KernelRegistry>(), std::move(options));

    const TransportResult started = agent.start();
    EXPECT_EQ(started.status, TransportStatus::InvalidArgument);
    EXPECT_NE(started.message.find("label 'zone'"), std::string::npos) << started.message;
    EXPECT_NE(started.message.find("300 bytes"), std::string::npos) << started.message;
    EXPECT_FALSE(transport->was_started());
}
