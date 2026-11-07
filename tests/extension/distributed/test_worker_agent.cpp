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
 * @file test_worker_agent.cpp
 * @brief Tests WorkerAgent over an in-process transport: handshake, dispatch, cache and liveness.
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
#include <SushiRuntime/in_process_transport/in_process_transport.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/recording_transport_events.hpp>
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

    /** @brief Holds one frame as the traffic log keeps it: who sent it and its type. */
    struct TrafficEntry
    {
        FrameDirection direction = FrameDirection::MasterToWorker; /**< Who sent the frame. */
        MessageType type = MessageType::Hello;                     /**< The frame's type. */
    };

    /** @brief Yields one frame, with the flag byte a control frame cannot carry, as a stream. */
    class SingleFrame final : public FrameSource
    {
        public:
            /** @brief Takes the frame the stream yields. */
            explicit SingleFrame(OutboundFrame frame) noexcept : frame_(std::move(frame))
            {

            }

            /** @brief Hands over the frame on the first call and nothing after. */
            [[nodiscard]] FrameSourceStatus next(OutboundFrame& frame) noexcept override
            {
                if (is_sent_)
                    return FrameSourceStatus::Finished;

                is_sent_ = true;
                frame.type = frame_.type;
                frame.flags = frame_.flags;
                frame.payload.swap(frame_.payload);
                return FrameSourceStatus::FinalFrame;
            }

        private:
            /// @brief The frame to hand over.
            OutboundFrame frame_;

            /// @brief Whether next() handed it over.
            bool is_sent_ = false;
    };

    /**
     * @brief Plays the master of a worker agent over an in-process fabric.
     *
     * It owns the fabric and the master endpoint, records what the worker sends, and can park
     * the worker's output frames so that a test sees them leave the worker before they arrive.
     * The worker connects when connect() starts the master endpoint.
     */
    class MasterSide final
    {
        public:
            /** @brief Builds the fabric and the master endpoint, which is not started yet. */
            MasterSide() : master_(InProcessTransport::make_master(fabric_))
            {
                fabric_.set_fault_policy([this](const FaultedFrame& frame)
                                         {
                                             return judge(frame);
                                         });
            }

            /** @brief Stops the master endpoint before the policy and the recorder go. */
            ~MasterSide()
            {
                master_->stop();
            }

            MasterSide(const MasterSide&) = delete;
            MasterSide& operator=(const MasterSide&) = delete;
            MasterSide(MasterSide&&) = delete;
            MasterSide& operator=(MasterSide&&) = delete;

            /** @brief Returns the fabric the worker endpoint joins. */
            InProcessFabric& fabric() noexcept
            {
                return fabric_;
            }

            /**
             * @brief Waits for the worker's next connection, starting the master when it is the
             *        first, and checks that the master names it @p peer.
             */
            void connect(PeerID peer)
            {
                if (!is_started_)
                {
                    ASSERT_TRUE(master_->start(events_).is_ok());
                    is_started_ = true;
                }
                ++connections_;
                ASSERT_TRUE(events_.wait_for_connections(connections_, K_WAIT));
                EXPECT_EQ(events_.connected()[connections_ - 1], peer);
            }

            /** @brief Waits until the worker has made @p count connections in all. */
            [[nodiscard]] bool wait_for_connections(std::size_t count)
            {
                return events_.wait_for_connections(count, K_WAIT);
            }

            /** @brief Encodes a message and sends it to the worker as a frame of the master. */
            template <typename Message>
            void deliver(PeerID peer, const Message& message, std::uint8_t flags = 0)
            {
                std::vector<std::byte> payload;
                ASSERT_EQ(message.encode(payload), Wire::CodecStatus::Ok);
                if (flags == 0)
                {
                    EXPECT_EQ(master_->send(peer, Message::K_TYPE,
                                            span<const std::byte>(payload.data(), payload.size())),
                              SendStatus::Queued);
                    return;
                }

                OutboundFrame frame;
                frame.type = Message::K_TYPE;
                frame.flags = flags;
                frame.payload = std::move(payload);
                EXPECT_EQ(master_->send_stream(
                              peer, std::make_unique<SingleFrame>(std::move(frame))),
                          SendStatus::Queued);
            }

            /** @brief Closes the connection from the master's end; the worker hears it end. */
            void lose(PeerID peer)
            {
                master_->close_peer(peer);
            }

            /** @brief Parks the worker's output frames from now on, or lets them through again. */
            void hold_results(bool value)
            {
                is_holding_.store(value);
            }

            /** @brief Waits until @p count output frames are parked. */
            [[nodiscard]] bool wait_for_held(std::size_t count)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT, [this, count]
                                         {
                                             return held_ >= count;
                                         });
            }

            /** @brief Lets output frames through again and delivers the parked ones in order. */
            void release_held()
            {
                is_holding_.store(false);
                fabric_.flush_held();
            }

            /** @brief Waits until a number of frames of a type arrived from the worker. */
            [[nodiscard]] bool wait_for(MessageType type, std::size_t count)
            {
                return events_.wait_for_frames(type, count, K_WAIT);
            }

            /** @brief Waits until @p count frames of a type and direction entered the fabric. */
            [[nodiscard]] bool wait_for_traffic(FrameDirection direction, MessageType type,
                                                std::size_t count)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT, [this, direction, type, count]
                                         {
                                             return traffic_count_locked(direction, type) >= count;
                                         });
            }

            /** @brief Waits until the worker closed a peer. */
            [[nodiscard]] bool wait_for_close(PeerID peer)
            {
                for (std::size_t losses = 1; !is_closed(peer); ++losses)
                {
                    if (!events_.wait_for_losses(losses, K_WAIT))
                        return false;
                }
                return true;
            }

            /** @brief Returns whether the worker closed a peer. */
            [[nodiscard]] bool is_closed(PeerID peer) const
            {
                const std::vector<RecordedLoss> losses = events_.losses();
                return std::any_of(losses.begin(), losses.end(), [peer](const RecordedLoss& loss)
                                   {
                                       return loss.peer == peer;
                                   });
            }

            /** @brief Returns the number of frames of a type that arrived from the worker. */
            [[nodiscard]] std::size_t count(MessageType type) const
            {
                return events_.frame_count(type);
            }

            /** @brief Returns the frames of a type in the order they arrived. */
            [[nodiscard]] std::vector<RecordedFrame> frames_of(MessageType type) const
            {
                return events_.frames_of(type);
            }

            /** @brief Returns every frame in the order it arrived. */
            [[nodiscard]] std::vector<RecordedFrame> frames() const
            {
                return events_.frames();
            }

            /** @brief Returns how often a frame of a type and direction entered the fabric. */
            [[nodiscard]] std::size_t traffic_count(FrameDirection direction,
                                                    MessageType type) const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return traffic_count_locked(direction, type);
            }

            /** @brief Returns every frame that entered the fabric, in the order it entered. */
            [[nodiscard]] std::vector<TrafficEntry> traffic() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return traffic_;
            }

        private:
            /** @brief Logs the frame, and parks the worker's output frames while holding. */
            FaultAction judge(const FaultedFrame& frame)
            {
                const bool is_output = frame.direction == FrameDirection::WorkerToMaster &&
                                       (frame.type == MessageType::BufferChunk ||
                                        frame.type == MessageType::TaskResult);
                const bool is_parked = is_output && is_holding_.load();
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    traffic_.push_back(TrafficEntry{frame.direction, frame.type});
                    if (is_parked)
                        ++held_;
                }
                changed_.notify_all();
                return is_parked ? FaultAction::Hold : FaultAction::Deliver;
            }

            /** @brief Returns how often a frame of a type and direction entered; mutex held. */
            [[nodiscard]] std::size_t traffic_count_locked(FrameDirection direction,
                                                           MessageType type) const
            {
                return static_cast<std::size_t>(std::count_if(
                    traffic_.begin(), traffic_.end(), [direction, type](const TrafficEntry& entry)
                    {
                        return entry.direction == direction && entry.type == type;
                    }));
            }

            /// @brief The fabric the master and the worker share.
            InProcessFabric fabric_;

            /// @brief What the master endpoint receives; it outlives the endpoint.
            RecordingTransportEvents events_;

            /// @brief The master endpoint.
            std::shared_ptr<InProcessTransport> master_;

            /// @brief Whether the master endpoint was started.
            bool is_started_ = false;

            /// @brief The connections the test has asked for so far.
            std::size_t connections_ = 0;

            /// @brief Whether the worker's output frames are parked.
            std::atomic<bool> is_holding_{false};

            /// @brief Guards the traffic log and the parked count.
            mutable std::mutex mutex_;

            /// @brief Wakes the waits on the traffic log and the parked count.
            std::condition_variable changed_;

            /// @brief Every frame that entered the fabric, in order.
            std::vector<TrafficEntry> traffic_;

            /// @brief The frames parked since the last release.
            std::size_t held_ = 0;
    };

    /** @brief Returns whether @p transport was never started, then stops it. */
    bool was_never_started(Transport& transport)
    {
        RecordingTransportEvents probe;
        const bool is_fresh = transport.start(probe).status == TransportStatus::Ok;
        transport.stop();
        return is_fresh;
    }

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
    Message decode(const RecordedFrame& frame)
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
     * @brief Holds a worker agent over a worker endpoint of an in-process fabric, and plays its
     *        master.
     *
     * A test registers its kernels and sets the options, then calls start().
     */
    class AgentRig final
    {
        public:
            /** @brief Builds the rig; the agent is built by start(). */
            AgentRig()
                : worker(InProcessTransport::make_worker(master.fabric())),
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

            /** @brief Stops the agent, which stops its endpoint. */
            ~AgentRig()
            {
                if (agent_.has_value())
                    agent_->stop();
            }

            AgentRig(const AgentRig&) = delete;
            AgentRig& operator=(const AgentRig&) = delete;
            AgentRig(AgentRig&&) = delete;
            AgentRig& operator=(AgentRig&&) = delete;

            /** @brief Builds and starts the agent with the options and kernels set so far. */
            void start()
            {
                agent_.emplace(runtime_, worker, kernels, std::move(options));
                const TransportResult started = agent_->start();
                ASSERT_TRUE(started.is_ok()) << started.message;
            }

            /** @brief Returns the agent; start() was called. */
            WorkerAgent& agent()
            {
                return *agent_;
            }

            /** @brief Returns whether the agent stopped its endpoint, which then sends nothing. */
            [[nodiscard]] bool is_worker_stopped() const
            {
                return worker->send(PeerID{0}, MessageType::Ping, span<const std::byte>()) ==
                       SendStatus::Stopped;
            }

            /** @brief Returns the HELLO the agent sent last. */
            Wire::HelloMessage last_hello() const
            {
                const std::vector<RecordedFrame> hellos = master.frames_of(MessageType::Hello);
                if (hellos.empty())
                {
                    ADD_FAILURE() << "no HELLO was sent";
                    return Wire::HelloMessage{};
                }
                return decode<Wire::HelloMessage>(hellos.back());
            }

            /**
             * @brief Runs a whole handshake on a new connection.
             * @param peer The master's name for the connection.
             */
            void register_master(PeerID peer, std::uint64_t session_id,
                                 std::uint32_t liveness_timeout_ms = 0,
                                 std::uint32_t chunk_bytes = 4096)
            {
                const std::size_t hellos = master.count(MessageType::Hello);
                const std::size_t proofs = master.count(MessageType::Proof);
                master.connect(peer);
                ASSERT_TRUE(master.wait_for(MessageType::Hello, hellos + 1));
                master.deliver(peer, challenge_of(secret_of(K_SECRET_BYTE), session_id,
                                                  nonce_in(last_hello())));
                ASSERT_TRUE(master.wait_for(MessageType::Proof, proofs + 1));
                const std::uint64_t registrations = agent_->statistics().registrations;
                master.deliver(peer, welcome_of(peer, liveness_timeout_ms, chunk_bytes));
                ASSERT_TRUE(eventually([&]
                                       {
                                           return agent_->statistics().registrations >
                                                  registrations;
                                       }));
            }

            /** @brief Returns the n-th TASK_RESULT sent, counted from 0. */
            [[nodiscard]] Wire::TaskResultMessage result(std::size_t index) const
            {
                const std::vector<RecordedFrame> frames = master.frames_of(MessageType::TaskResult);
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
                master.deliver(peer, dispatch);
                ASSERT_TRUE(master.wait_for(MessageType::TaskResult, results));
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

            /** @brief Plays the master: owns the fabric, sends frames and records the replies. */
            MasterSide master;

            /** @brief The worker endpoint the agent runs on. */
            std::shared_ptr<InProcessTransport> worker;

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

    rig.master.deliver(K_MASTER, release_of(1));
    ASSERT_TRUE(eventually([&] { return rig.agent().statistics().cache_bytes == 0; }));
    EXPECT_EQ(rig.agent().statistics().cache_evictions, 1u);

    // An output that waits to be sent holds its own copy, not the cached buffer.
    rig.master.hold_results(true);
    rig.master.deliver(K_MASTER, dispatch_of(2, op, {}, {handle_of(2, 1, 64)}));
    ASSERT_TRUE(rig.master.wait_for_held(2));
    rig.master.deliver(K_MASTER, release_of(2));
    ASSERT_TRUE(eventually([&] { return rig.agent().statistics().cache_bytes == 0; }))
        << "a waiting stream kept its output in the cache";
    EXPECT_EQ(rig.agent().statistics().cache_evictions, 2u);

    rig.master.release_held();
    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskResult, 2));
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
    EXPECT_EQ(rig.master.count(MessageType::BufferRequest), 0u)
        << "a cached input was pulled";

    rig.run(K_MASTER, dispatch_of(5, op, {}, {handle_of(4, 1, 100)}), 5);
    EXPECT_EQ(rig.agent().statistics().cache_evictions, 1u);
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 300u);

    rig.run(K_MASTER, dispatch_of(6, op, {handle_of(1, 1, 100)}, {}), 6);
    EXPECT_EQ(rig.master.count(MessageType::BufferRequest), 0u)
        << "the buffer used last was evicted";

    const std::vector<std::byte> bytes = counting_bytes(100, 1);
    const BufferHandle second = handle_of(2, 1, 100);
    rig.master.deliver(K_MASTER, dispatch_of(7, op, {second}, {}));
    ASSERT_TRUE(rig.master.wait_for(MessageType::BufferRequest, 1));
    EXPECT_EQ(decode<Wire::BufferRequestMessage>(
                  rig.master.frames_of(MessageType::BufferRequest)[0])
                  .id,
              second.id);
    rig.master.deliver(K_MASTER, pull_chunk(second, 0, 100, bytes),
                           Wire::K_FRAME_FLAG_LAST_CHUNK);
    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskResult, 7));
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
    rig.master.deliver(K_MASTER, dispatch_of(2, op, {input}, {handle_of(3, 1, 100)}));
    ASSERT_TRUE(rig.master.wait_for(MessageType::BufferRequest, 1));
    rig.master.deliver(K_MASTER, dispatch_of(3, op, {}, {handle_of(4, 1, 100)}));
    rig.master.deliver(K_MASTER, dispatch_of(4, op, {}, {handle_of(5, 1, 100)}));
    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskAccepted, 4));
    const std::vector<std::byte> bytes = counting_bytes(100, 1);
    rig.master.deliver(K_MASTER, pull_chunk(input, 0, 100, bytes),
                           Wire::K_FRAME_FLAG_LAST_CHUNK);
    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskResult, 4));

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

    rig.master.deliver(K_MASTER, dispatch_of(1, op, {handle}, {}));
    ASSERT_TRUE(rig.master.wait_for(MessageType::BufferRequest, 1));
    rig.master.deliver(K_MASTER, pull_chunk(handle, 0, 4, bytes));
    rig.master.deliver(K_MASTER, pull_chunk(handle, 8, 4, bytes),
                           Wire::K_FRAME_FLAG_LAST_CHUNK);
    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskResult, 1));
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::InputUnavailable);
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 0u) << "a discarded transfer kept its memory";

    rig.master.deliver(K_MASTER, dispatch_of(2, op, {handle}, {}));
    ASSERT_TRUE(rig.master.wait_for(MessageType::BufferRequest, 2));
    rig.master.deliver(K_MASTER, pull_chunk(handle, 0, 4, bytes));
    rig.master.deliver(K_MASTER, pull_chunk(handle, 4, 4, bytes));
    rig.master.deliver(K_MASTER, pull_chunk(handle, 8, 4, bytes),
                           Wire::K_FRAME_FLAG_LAST_CHUNK);
    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskResult, 2));
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
    EXPECT_EQ(rig.master.count(MessageType::BufferChunk), 0u);

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
    rig.master.deliver(K_MASTER, dispatch_of(1, op, {input}, {output}));

    ASSERT_TRUE(rig.master.wait_for(MessageType::BufferRequest, 1));
    const Wire::BufferRequestMessage request = decode<Wire::BufferRequestMessage>(
        rig.master.frames_of(MessageType::BufferRequest)[0]);
    EXPECT_EQ(request.id, input.id);
    EXPECT_EQ(request.version, input.version);

    rig.master.deliver(K_MASTER, pull_chunk(input, 0, 8, bytes));
    rig.master.deliver(K_MASTER, pull_chunk(input, 8, 4, bytes),
                           Wire::K_FRAME_FLAG_LAST_CHUNK);

    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskResult, 1));
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(input_size.load(), 12u);
    EXPECT_EQ(output_size.load(), 20u);

    const std::vector<RecordedFrame> chunks = rig.master.frames_of(MessageType::BufferChunk);
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
    EXPECT_EQ(rig.master.count(MessageType::BufferChunk), 0u);

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
    EXPECT_EQ(rig.master.count(MessageType::BufferChunk), 0u);
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

    const std::vector<RecordedFrame> chunks = rig.master.frames_of(MessageType::BufferChunk);
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
    EXPECT_EQ(rig.master.count(MessageType::BufferChunk), 0u);

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

    rig.master.hold_results(true);
    const BufferHandle output = handle_of(5, 2, 10);
    rig.master.deliver(K_MASTER, dispatch_of(9, op, {}, {output}));
    // Three chunks and the result are parked, so none of them has reached the master yet.
    ASSERT_TRUE(rig.master.wait_for_held(4));

    EXPECT_EQ(rig.master.count(MessageType::TaskResult), 0u);
    // The stream sends its own copy, so a release frees the cached output.
    rig.master.deliver(K_MASTER, release_of(5));
    ASSERT_TRUE(eventually([&] { return rig.agent().statistics().cache_bytes == 0; }))
        << "the stream kept the cached output pinned";

    rig.master.release_held();
    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskResult, 1));

    std::vector<RecordedFrame> frames;
    for (const RecordedFrame& frame : rig.master.frames())
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
        const RecordedFrame& frame = frames[index + 1];
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
    const std::size_t requests = rig.master.count(MessageType::BufferRequest);
    const std::size_t accepted = rig.master.count(MessageType::TaskAccepted);
    rig.master.deliver(K_MASTER, first);
    ASSERT_TRUE(rig.master.wait_for(MessageType::BufferRequest, requests + 1));
    rig.master.deliver(K_MASTER, second);
    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskAccepted, accepted + 2));
    rig.master.deliver(K_MASTER,
                           pull_chunk(input, 0, static_cast<std::size_t>(input.size_bytes), bytes),
                           Wire::K_FRAME_FLAG_LAST_CHUNK);
}

/** @brief Returns the data of the first output chunk a task sent. */
std::vector<std::byte> first_output_of(const AgentRig& rig, std::uint64_t task_id)
{
    for (const RecordedFrame& frame : rig.master.frames_of(MessageType::BufferChunk))
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

    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskResult, 2));
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

    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskResult, 2));
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

    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskResult, 2));
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.master.count(MessageType::BufferRequest), 1u);
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

    ASSERT_TRUE(rig.master.wait_for(MessageType::TaskResult, 2));
    EXPECT_EQ(rig.result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(rig.master.count(MessageType::BufferRequest), 1u)
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

    rig.master.deliver(K_MASTER, dispatch_of(1, op, {handle_of(3, 5, 12)}, {}));
    ASSERT_TRUE(rig.master.wait_for(MessageType::BufferRequest, 1));

    rig.master.lose(K_MASTER);

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
    const std::size_t results = rig.master.count(MessageType::TaskResult);
    rig.run(next, dispatch_of(2, op, {}, {}), results + 1);
    EXPECT_EQ(rig.result(results).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(calls.load(), 1);
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
    const std::size_t before = rig.master.count(MessageType::Heartbeat);
    ASSERT_TRUE(rig.master.wait_for(MessageType::Heartbeat, before + 1));

    const Wire::HeartbeatMessage heartbeat = decode<Wire::HeartbeatMessage>(
        rig.master.frames_of(MessageType::Heartbeat).back());
    EXPECT_EQ(heartbeat.oldest_held_task_id, 42u);
    EXPECT_EQ(heartbeat.queue_depth, 0u);
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

    rig.master.lose(PeerID{1});
    rig.register_master(PeerID{2}, 7);
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 32u)
        << "the cache was cleared for the session it was filled in";
    rig.master.deliver(PeerID{2}, dispatch_of(5, op, {}, {handle_of(1, 2, 32)}));
    rig.run(PeerID{2}, dispatch_of(6, op, {}, {}), 2);
    EXPECT_EQ(calls.load(), 2) << "a dispatch repeated in one session ran twice";

    rig.master.lose(PeerID{2});
    rig.register_master(PeerID{3}, 8);
    EXPECT_EQ(rig.agent().statistics().cache_bytes, 0u);
    rig.run(PeerID{3}, dispatch_of(5, op, {}, {}), 3);
    EXPECT_EQ(calls.load(), 3) << "a new session kept the ids of the old one";
    EXPECT_EQ(rig.agent().statistics().reconnects, 2u);
}

/** @brief Tests that a worker name HELLO cannot carry is refused before the transport starts. */
TEST(Medium_WorkerAgentTest, Regression_ANameAbove255BytesIsRefusedBeforeConnecting)
{
    Runtime runtime = Runtime::create().value();
    InProcessFabric fabric;
    const std::shared_ptr<InProcessTransport> transport = InProcessTransport::make_worker(fabric);
    WorkerAgentOptions options;
    options.name = std::string(256, 'n');
    WorkerAgent agent(runtime, transport, make_sushi<KernelRegistry>(), std::move(options));

    const TransportResult started = agent.start();
    EXPECT_EQ(started.status, TransportStatus::InvalidArgument);
    EXPECT_NE(started.message.find("worker name"), std::string::npos) << started.message;
    EXPECT_NE(started.message.find("255"), std::string::npos) << started.message;
    EXPECT_TRUE(was_never_started(*transport)) << "the transport was started before the refusal";
}

/** @brief Tests that a label value HELLO cannot carry is refused, naming the label. */
TEST(Medium_WorkerAgentTest, Regression_ALabelValueAbove255BytesIsRefusedBeforeConnecting)
{
    RuntimeConfig config;
    config.labels = {NodeLabel{"zone", std::string(300, 'v')}};
    Runtime runtime = Runtime::create(config).value();
    InProcessFabric fabric;
    const std::shared_ptr<InProcessTransport> transport = InProcessTransport::make_worker(fabric);
    WorkerAgentOptions options;
    options.name = "worker-a";
    WorkerAgent agent(runtime, transport, make_sushi<KernelRegistry>(), std::move(options));

    const TransportResult started = agent.start();
    EXPECT_EQ(started.status, TransportStatus::InvalidArgument);
    EXPECT_NE(started.message.find("label 'zone'"), std::string::npos) << started.message;
    EXPECT_NE(started.message.find("300 bytes"), std::string::npos) << started.message;
    EXPECT_TRUE(was_never_started(*transport)) << "the transport was started before the refusal";
}
