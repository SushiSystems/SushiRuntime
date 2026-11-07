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

#include "support/single_frame.hpp"
#include "support/worker_wire.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using namespace TestSupport::WorkerWire;

namespace
{
    /** @brief Bounds every wait on a condition, so a broken agent fails a test and hangs none. */
    constexpr std::chrono::seconds K_WAIT{30};

    /** @brief Holds the connection most tests register the master on. */
    constexpr PeerID K_MASTER{1};

    /**
     * @brief Plays the master of a worker agent over an in-process fabric.
     *
     * It owns the fabric and the master endpoint and records what the worker sends. The worker
     * connects when connect() starts the master endpoint.
     */
    class MasterSide final
    {
        public:
            /** @brief Builds the fabric and the master endpoint, which is not started yet. */
            MasterSide() : master_(InProcessTransport::make_master(fabric_))
            {

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
                              peer, std::make_unique<TestSupport::SingleFrame>(std::move(frame))),
                          SendStatus::Queued);
            }

            /** @brief Closes the connection from the master's end; the worker hears it end. */
            void lose(PeerID peer)
            {
                master_->close_peer(peer);
            }

            /** @brief Waits until a number of frames of a type arrived from the worker. */
            [[nodiscard]] bool wait_for(MessageType type, std::size_t count)
            {
                return events_.wait_for_frames(type, count, K_WAIT);
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

        private:
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
    };

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
