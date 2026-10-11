/**************************************************************************/
/* test_distributed_e2e.cpp                                               */
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
 * @file test_distributed_e2e.cpp
 * @brief Tests kernel offload from a master to workers, end to end, on a SYCL device.
 * @see modules/extension/distributed/tests/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/api/fluent.hpp>
#include <SushiRuntime/distributed/buffer_handle.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/distributed/offload_policy.hpp>
#include <SushiRuntime/distributed/transport.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/registered_task.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>

#include "test_helpers.hpp"

#include "frame_sending.hpp"
#include "handshake.hpp"
#include "offload_dispatcher.hpp"
#include "wire_frame.hpp"
#include "wire_messages.hpp"
#include "test_context.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Execution;
using namespace SushiRuntime::Internal::Graph;
using namespace SushiRuntime::Literals;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Names the vector-add operation: out[i] = a[i] + b[i]. */
    constexpr auto K_VEC_ADD = "vec.add"_op;

    /** @brief Names the State-step operation: next[i] = cur[i] + (i % 7). */
    constexpr auto K_STATE_STEP = "state.step"_op;

    /** @brief Holds how long a test waits for a condition before it fails. */
    constexpr std::chrono::seconds K_WAIT{10};

    /** @brief Holds the tolerance of every float comparison; see README.md. */
    constexpr float K_TOLERANCE = 0.0f;

    /** @brief Holds the value an output holds before any task wrote it. */
    constexpr float K_UNWRITTEN = -1.0f;

    /** @brief Returns whether a condition became true within K_WAIT, yielding between checks. */
    template <typename Predicate>
    [[nodiscard]] bool eventually(Predicate is_met)
    {
        const auto deadline = std::chrono::steady_clock::now() + K_WAIT;
        while (!is_met())
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::yield();
        }
        return true;
    }

    /**
     * @brief Owns one shared USM allocation of floats on a queue.
     */
    class USMFloats final
    {
        public:
            /**
             * @brief Allocates @p count floats and sets each to @p value.
             * @throws std::runtime_error When the device gives no memory.
             */
            USMFloats(sycl::queue& queue, std::size_t count, float value)
                : queue_(queue), data_(sycl::malloc_shared<float>(count, queue)), count_(count)
            {
                if (data_ == nullptr)
                    throw std::runtime_error("sycl::malloc_shared returned null for a test buffer");
                std::fill_n(data_, count_, value);
            }

            /** @brief Frees the allocation. */
            ~USMFloats()
            {
                sycl::free(data_, queue_);
            }

            USMFloats(const USMFloats&) = delete;
            USMFloats& operator=(const USMFloats&) = delete;
            USMFloats(USMFloats&&) = delete;
            USMFloats& operator=(USMFloats&&) = delete;

            /** @brief Sets element i to @p step times i. */
            void ramp(float step)
            {
                for (std::size_t index = 0; index < count_; ++index)
                    data_[index] = step * static_cast<float>(index);
            }

            /** @brief Sets every element to @p value. */
            void fill(float value)
            {
                std::fill_n(data_, count_, value);
            }

            /** @brief Returns the base address. */
            [[nodiscard]] float* data() const noexcept
            {
                return data_;
            }

            /** @brief Returns the number of elements. */
            [[nodiscard]] std::size_t count() const noexcept
            {
                return count_;
            }

            /** @brief Returns the size of the allocation in bytes. */
            [[nodiscard]] std::size_t size_bytes() const noexcept
            {
                return count_ * sizeof(float);
            }

            /** @brief Returns the allocation as the view a kernel receives. */
            [[nodiscard]] BufferView view() const noexcept
            {
                return BufferView{data_, size_bytes()};
            }

        private:
            /// @brief The queue the allocation belongs to.
            sycl::queue& queue_;

            /// @brief The base address.
            float* data_;

            /// @brief The number of elements.
            std::size_t count_;
    };

    /**
     * @brief Expects every element to equal what @p expected returns for its index, and
     *        reports only the first one that does not.
     */
    template <typename Element, typename Expected>
    void expect_elements(const Element* actual, std::size_t count, Expected expected)
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            const Element wanted = static_cast<Element>(expected(index));
            if (std::abs(actual[index] - wanted) <= static_cast<Element>(K_TOLERANCE))
                continue;

            ADD_FAILURE() << "element " << index << " is " << actual[index] << ", expected "
                          << wanted;
            return;
        }
    }

    /** @brief Expects out[i] == a[i] + b[i] for every element. */
    void expect_sum(const USMFloats& out, const USMFloats& a, const USMFloats& b)
    {
        const float* left = a.data();
        const float* right = b.data();
        expect_elements(out.data(), out.count(),
                        [left, right](std::size_t index) { return left[index] + right[index]; });
    }

    /** @brief Returns the vector-add kernel; it counts each run in @p runs. */
    KernelRegistry::Kernel vec_add_kernel(std::atomic<int>& runs)
    {
        return [&runs](sycl::queue& queue, span<const BufferView> inputs,
                       span<const BufferView> outputs, const TaskMetadata&) -> sycl::event
        {
            const float* left = inputs[0].as<const float>();
            const float* right = inputs[1].as<const float>();
            float* sum = outputs[0].as<float>();
            const std::size_t count = outputs[0].size_bytes / sizeof(float);
            std::atomic<int>* counter = &runs;

            return queue.submit([&](sycl::handler& handler)
            {
                SushiRuntime::Detail::enqueue_host_task(handler, [=]
                {
                    for (std::size_t index = 0; index < count; ++index)
                        sum[index] = left[index] + right[index];
                    counter->fetch_add(1, std::memory_order_relaxed);
                });
            });
        };
    }

    /**
     * @brief Returns a registry that holds the vector-add kernel.
     * @param runs Counts the kernel's runs; outlives the registry.
     */
    sushi_ptr<KernelRegistry> vec_add_kernels(std::atomic<int>& runs)
    {
        sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
        kernels->register_kernel(K_VEC_ADD, vec_add_kernel(runs));
        return kernels;
    }

    /**
     * @brief Returns a registry that holds the State-step kernel.
     * @param runs Counts the kernel's runs; outlives the registry.
     */
    sushi_ptr<KernelRegistry> state_step_kernels(std::atomic<int>& runs)
    {
        sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
        kernels->register_kernel(
            K_STATE_STEP,
            [&runs](sycl::queue& queue, span<const BufferView> inputs,
                    span<const BufferView> outputs, const TaskMetadata&) -> sycl::event
            {
                const int* current = inputs[0].as<const int>();
                int* next = outputs[0].as<int>();
                const std::size_t count = outputs[0].size_bytes / sizeof(int);
                std::atomic<int>* counter = &runs;

                return queue.submit([&](sycl::handler& handler)
                {
                    SushiRuntime::Detail::enqueue_host_task(handler, [=]
                    {
                        for (std::size_t index = 0; index < count; ++index)
                            next[index] = current[index] + static_cast<int>(index % 7);
                        counter->fetch_add(1, std::memory_order_relaxed);
                    });
                });
            });
        return kernels;
    }

    /** @brief Returns options whose policy sends a task of any size to a worker. */
    ClusterOptions eager_options()
    {
        ClusterOptions options;
        options.policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
        return options;
    }

    /** @brief Shares a buffer with the workers of the master installed on @p context. */
    void share(RuntimeContext& context, const USMFloats& buffer)
    {
        context.offload()->share(buffer.data(), buffer.size_bytes());
    }

    /**
     * @brief Adds an op-tagged task out = a + b whose local fallback computes the same sum.
     * @return The node added; valid while the graph or its compiled plan lives.
     */
    Internal::Graph::Node& add_vec_add(TaskGraph& graph, const char* name, const USMFloats& a,
                             const USMFloats& b, const USMFloats& out)
    {
        TaskMetadata metadata{};
        metadata.name = name;
        metadata.op_id = K_VEC_ADD;

        float* left = a.data();
        float* right = b.data();
        float* sum = out.data();
        const std::size_t count = out.count();
        return graph.add_task(
            metadata, {left, right}, {sum},
            [left, right, sum, count](sycl::queue& queue,
                                      const std::vector<sycl::event>&) -> sycl::event
            {
                return queue.submit([&](sycl::handler& handler)
                {
                    SushiRuntime::Detail::enqueue_host_task(handler, [=]
                    {
                        for (std::size_t index = 0; index < count; ++index)
                            sum[index] = left[index] + right[index];
                    });
                });
            });
    }

    /**
     * @brief Runs a compiled graph on its own thread, so a run that never returns fails a
     *        test by timeout.
     */
    class BackgroundRun final
    {
        public:
            /** @brief Starts the run; @p context and @p graph outlive this object. */
            BackgroundRun(RuntimeContext& context, CompiledGraph& graph)
            {
                auto done = std::make_shared<std::promise<void>>();
                finished_ = done->get_future();
                thread_ = std::thread([&context, &graph, done]
                {
                    context.execution_engine().execute(graph).value();
                    done->set_value();
                });
            }

            /** @brief Joins a run that returned and detaches one that did not. */
            ~BackgroundRun()
            {
                if (!thread_.joinable())
                    return;

                if (finished_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    thread_.join();
                else
                    thread_.detach();
            }

            BackgroundRun(const BackgroundRun&) = delete;
            BackgroundRun& operator=(const BackgroundRun&) = delete;
            BackgroundRun(BackgroundRun&&) = delete;
            BackgroundRun& operator=(BackgroundRun&&) = delete;

            /** @brief Returns whether the run returned within K_WAIT, and joins it if so. */
            [[nodiscard]] bool wait()
            {
                if (finished_.wait_for(K_WAIT) != std::future_status::ready)
                    return false;

                if (thread_.joinable())
                    thread_.join();
                return true;
            }

        private:
            /// @brief Becomes ready when execute() returned.
            std::future<void> finished_;

            /// @brief The thread that calls execute().
            std::thread thread_;
    };

    /**
     * @brief Runs one real worker: its own context and the agent that serves a transport.
     */
    class LocalWorker final
    {
        public:
            /** @brief Builds the worker; nothing runs until start(). */
            LocalWorker(std::shared_ptr<Transport> transport, sushi_ptr<KernelRegistry> kernels,
                        WorkerAgentOptions options = WorkerAgentOptions())
                : agent_(context_, std::move(transport), std::move(kernels), std::move(options))
            {

            }

            /** @brief Starts the agent and returns whether its transport started. */
            [[nodiscard]] bool start()
            {
                return agent_.start().is_ok();
            }

            /** @brief Stops the agent. */
            void stop() noexcept
            {
                agent_.stop();
            }

            /** @brief Returns the agent's counters. */
            [[nodiscard]] WorkerStatistics statistics() const noexcept
            {
                return agent_.statistics();
            }

        private:
            /// @brief The worker's own runtime; declared first, destroyed last.
            const std::shared_ptr<RuntimeContext> context_owner_ =
                SushiRuntime::Test::make_context();

            /// @brief The context context_owner_ holds.
            RuntimeContext& context_ = *context_owner_;

            /// @brief The agent under test.
            WorkerAgent agent_;
    };

    /** @brief Returns handshake octets as the bytes a wire message carries. */
    std::array<std::byte, Wire::K_NONCE_BYTES> to_wire(
        const std::array<std::uint8_t, Wire::K_NONCE_BYTES>& octets)
    {
        std::array<std::byte, Wire::K_NONCE_BYTES> bytes{};
        std::transform(octets.begin(), octets.end(), bytes.begin(),
                       [](std::uint8_t octet) { return std::byte{octet}; });
        return bytes;
    }

    /** @brief Returns the bytes of a wire nonce as handshake octets. */
    HandshakeNonce to_nonce(const std::array<std::byte, Wire::K_NONCE_BYTES>& bytes)
    {
        HandshakeNonce nonce{};
        std::transform(bytes.begin(), bytes.end(), nonce.bytes.begin(),
                       [](std::byte value) { return static_cast<std::uint8_t>(value); });
        return nonce;
    }

    /** @brief Returns the bytes of a float vector as a chunk carries them. */
    span<const std::byte> bytes_of(const std::vector<float>& values)
    {
        return span<const std::byte>(reinterpret_cast<const std::byte*>(values.data()),
                                     values.size() * sizeof(float));
    }

    /** @brief Returns the frame of one output chunk that carries a whole buffer. */
    OutboundFrame output_chunk_frame(std::uint64_t task_id, const BufferHandle& handle,
                                     span<const std::byte> bytes)
    {
        Wire::BufferChunkMessage chunk;
        chunk.purpose = Wire::ChunkPurpose::TaskOutput;
        chunk.task_id = task_id;
        chunk.handle = handle;
        chunk.data = bytes;

        OutboundFrame frame;
        frame.type = MessageType::BufferChunk;
        frame.flags = Wire::K_FRAME_FLAG_LAST_CHUNK;
        EXPECT_EQ(chunk.encode(frame.payload), Wire::CodecStatus::Ok);
        return frame;
    }

    /** @brief Returns the frame of a result that reports a task as succeeded. */
    OutboundFrame success_frame(const Wire::TaskDispatchMessage& dispatch)
    {
        Wire::TaskResultMessage result;
        result.task_id = dispatch.task_id;
        result.status = Wire::TaskResultStatus::Succeeded;
        result.outputs = dispatch.outputs;

        OutboundFrame frame;
        frame.type = MessageType::TaskResult;
        EXPECT_EQ(result.encode(frame.payload), Wire::CodecStatus::Ok);
        return frame;
    }

    /**
     * @brief Yields a fixed list of frames as one stream, in order.
     */
    class ScriptedFrames final : public FrameSource
    {
        public:
            /** @brief Takes the frames the stream yields. */
            explicit ScriptedFrames(std::vector<OutboundFrame> frames) noexcept
                : frames_(std::move(frames))
            {

            }

            /** @brief Hands over the next frame without copying its payload. */
            [[nodiscard]] FrameSourceStatus next(OutboundFrame& frame) noexcept override
            {
                if (next_ == frames_.size())
                    return FrameSourceStatus::Finished;

                frame.type = frames_[next_].type;
                frame.flags = frames_[next_].flags;
                frame.payload.swap(frames_[next_].payload);
                ++next_;
                return next_ == frames_.size() ? FrameSourceStatus::FinalFrame
                                               : FrameSourceStatus::Frame;
            }

        private:
            /// @brief The frames, in the order they leave.
            std::vector<OutboundFrame> frames_;

            /// @brief The index of the frame the next call hands over.
            std::size_t next_ = 0;
    };

    /** @brief Names what a scripted worker does with a dispatch. */
    enum class DispatchAnswer : std::uint8_t
    {
        Accept,     /**< Confirms the dispatch and holds the task until the test answers. */
        Fail,       /**< Confirms the dispatch, then reports that the kernel failed. */
        FallSilent, /**< Sends nothing from the dispatch on, not even PONG. */
    };

    /**
     * @brief Registers with a master as a worker and answers dispatches as a test scripts.
     *
     * It speaks the wire protocol itself, with no WorkerAgent, and runs no kernel.
     */
    class ScriptedWorker final : public TransportEvents
    {
        public:
            /** @brief Builds a worker that claims a kernel for @p operation. */
            ScriptedWorker(InProcessFabric& fabric, OperationID operation, DispatchAnswer answer)
                : transport_(InProcessTransport::make_worker(fabric, single_connection())),
                  operation_(operation), answer_(answer)
            {
                worker_nonce_.bytes.fill(0x11);
            }

            /** @brief Stops the transport, after which no event reaches this object. */
            ~ScriptedWorker()
            {
                transport_->stop();
            }

            ScriptedWorker(const ScriptedWorker&) = delete;
            ScriptedWorker& operator=(const ScriptedWorker&) = delete;
            ScriptedWorker(ScriptedWorker&&) = delete;
            ScriptedWorker& operator=(ScriptedWorker&&) = delete;

            /** @brief Starts the transport and returns whether it started. */
            [[nodiscard]] bool start()
            {
                return transport_->start(*this).is_ok();
            }

            /** @brief Waits until @p count dispatches arrived, or K_WAIT. */
            [[nodiscard]] bool wait_for_dispatches(std::size_t count)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT,
                                         [&] { return dispatches_.size() >= count; });
            }

            /** @brief Returns the number of dispatches that arrived. */
            [[nodiscard]] std::size_t dispatch_count() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return dispatches_.size();
            }

            /**
             * @brief Returns a copy of one dispatch.
             * @pre @p index is below dispatch_count().
             */
            [[nodiscard]] Wire::TaskDispatchMessage dispatch(std::size_t index) const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return dispatches_.at(index);
            }

            /** @brief Returns the number of frames the transport did not queue. */
            [[nodiscard]] int refused_sends() const noexcept
            {
                return refused_sends_.load(std::memory_order_relaxed);
            }

            /** @brief Hands the master a stream of the given frames. */
            [[nodiscard]] SendStatus send_stream(std::vector<OutboundFrame> frames)
            {
                PeerID master;
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    master = master_;
                }
                return transport_->send_stream(
                    master, std::make_unique<ScriptedFrames>(std::move(frames)));
            }

            /** @brief Opens the handshake on a new connection. */
            void on_peer_connected(PeerID peer, const PeerDescription& /*info*/) override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    master_ = peer;
                }

                Wire::HelloMessage hello;
                hello.worker_nonce = to_wire(worker_nonce_.bytes);
                hello.build_id = "scripted";
                hello.node_name = "scripted-worker";
                send(peer, hello);
            }

            /** @brief Answers CHALLENGE, PING and TASK_DISPATCH until the worker falls silent. */
            void on_frame(PeerID peer, MessageType type, std::uint8_t /*flags*/,
                          span<const std::byte> payload) override
            {
                if (is_silent())
                    return;

                if (type == MessageType::Challenge)
                    answer_challenge(peer, payload);
                else if (type == MessageType::Ping)
                    answer_ping(peer, payload);
                else if (type == MessageType::TaskDispatch)
                    take_dispatch(peer, payload);
            }

            /** @brief Ignores a loss; the transport does not reconnect. */
            void on_peer_lost(PeerID /*peer*/, const PeerLoss& /*loss*/) override
            {

            }

            /** @brief Ignores the end of connecting, which follows every loss here. */
            void on_connect_abandoned(const std::string& /*cause*/) override
            {

            }

        private:
            /** @brief Returns options under which a lost connection is not opened again. */
            [[nodiscard]] static InProcessWorkerOptions single_connection()
            {
                InProcessWorkerOptions options;
                options.reconnect = false;
                return options;
            }

            /** @brief Returns whether a FallSilent worker has taken its dispatch. */
            [[nodiscard]] bool is_silent() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return silent_;
            }

            /** @brief Queues one control frame and counts it when the transport refuses. */
            template <typename Message>
            void send(PeerID peer, const Message& message)
            {
                if (send_message(*transport_, peer, message) != SendStatus::Queued)
                    refused_sends_.fetch_add(1, std::memory_order_relaxed);
            }

            /** @brief Decodes a payload, or records a test failure and returns false. */
            template <typename Message>
            [[nodiscard]] static bool decode(span<const std::byte> payload, Message& message)
            {
                if (Message::decode(payload, message).ok())
                    return true;

                ADD_FAILURE() << "the master sent a " << message_type_name(Message::K_TYPE)
                              << " frame that does not decode";
                return false;
            }

            /** @brief Proves the empty secret and names the operation the worker claims. */
            void answer_challenge(PeerID peer, span<const std::byte> payload)
            {
                Wire::ChallengeMessage challenge;
                if (!decode(payload, challenge))
                    return;

                Wire::ProofMessage proof;
                proof.worker_proof = to_wire(
                    compute_proof(HandshakeRole::Worker, span<const std::uint8_t>(),
                                  worker_nonce_, to_nonce(challenge.master_nonce)));
                proof.ops = {operation_};
                proof.max_jobs = 16;
                send(peer, proof);
            }

            /** @brief Echoes the token of a PING. */
            void answer_ping(PeerID peer, span<const std::byte> payload)
            {
                Wire::PingMessage ping;
                if (!decode(payload, ping))
                    return;

                Wire::PongMessage pong;
                pong.token = ping.token;
                send(peer, pong);
            }

            /** @brief Records a dispatch, wakes the test and answers as scripted. */
            void take_dispatch(PeerID peer, span<const std::byte> payload)
            {
                Wire::TaskDispatchMessage dispatch;
                if (!decode(payload, dispatch))
                    return;

                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    silent_ = answer_ == DispatchAnswer::FallSilent;
                    dispatches_.push_back(dispatch);
                }
                changed_.notify_all();

                if (answer_ == DispatchAnswer::FallSilent)
                    return;

                Wire::TaskAcceptedMessage accepted;
                accepted.task_id = dispatch.task_id;
                send(peer, accepted);

                if (answer_ != DispatchAnswer::Fail)
                    return;

                Wire::TaskResultMessage result;
                result.task_id = dispatch.task_id;
                result.status = Wire::TaskResultStatus::KernelFailed;
                result.outputs = dispatch.outputs;
                send(peer, result);
            }

            /// @brief The worker endpoint; this object is its only event receiver.
            std::shared_ptr<InProcessTransport> transport_;

            /// @brief The operation the worker says it has a kernel for.
            OperationID operation_;

            /// @brief What the worker does with a dispatch.
            DispatchAnswer answer_;

            /// @brief The nonce the worker sends in HELLO.
            HandshakeNonce worker_nonce_{};

            /// @brief Guards master_, silent_ and dispatches_.
            mutable std::mutex mutex_;

            /// @brief Wakes wait_for_dispatches() on every dispatch.
            std::condition_variable changed_;

            /// @brief The master's peer id on the open connection.
            PeerID master_;

            /// @brief Set when a FallSilent worker took its dispatch.
            bool silent_ = false;

            /// @brief The dispatches that arrived, in order.
            std::vector<Wire::TaskDispatchMessage> dispatches_;

            /// @brief Frames the transport did not queue.
            std::atomic<int> refused_sends_{0};
    };

    /**
     * @brief Answers start() with a fixed result and reports a fixed exposure; carries no frame.
     */
    class StubTransport final : public Transport
    {
        public:
            /** @brief Sets what exposure() and start() return. */
            StubTransport(TransportExposure exposure, TransportResult start_result)
                : exposure_(exposure), start_result_(std::move(start_result))
            {

            }

            /** @brief Counts the call and returns the fixed result. */
            [[nodiscard]] TransportResult start(TransportEvents& /*events*/) override
            {
                start_calls_.fetch_add(1, std::memory_order_relaxed);
                return start_result_;
            }

            /** @brief Does nothing; the transport owns no thread. */
            void stop() noexcept override
            {

            }

            /** @brief Refuses every frame; the transport has no peer. */
            [[nodiscard]] SendStatus send(PeerID /*peer*/, MessageType /*type*/,
                                          span<const std::byte> /*payload*/) override
            {
                return SendStatus::UnknownPeer;
            }

            /** @brief Refuses every stream; the source is destroyed with the argument. */
            [[nodiscard]] SendStatus send_stream(PeerID /*peer*/,
                                                 std::unique_ptr<FrameSource> /*source*/) override
            {
                return SendStatus::UnknownPeer;
            }

            /** @brief Does nothing; the transport holds no connection. */
            void close_peer(PeerID /*peer*/) noexcept override
            {

            }

            /** @brief Returns the fixed exposure. */
            [[nodiscard]] TransportExposure exposure() const noexcept override
            {
                return exposure_;
            }

            /** @brief Reports that no byte moved. */
            [[nodiscard]] TransportStatistics statistics() const noexcept override
            {
                return TransportStatistics();
            }

            /** @brief Returns how often start() was called. */
            [[nodiscard]] int start_calls() const noexcept
            {
                return start_calls_.load(std::memory_order_relaxed);
            }

        private:
            /// @brief What exposure() returns.
            TransportExposure exposure_;

            /// @brief What start() returns.
            TransportResult start_result_;

            /// @brief Calls to start().
            std::atomic<int> start_calls_{0};
    };

    /**
     * @brief Evolves a distributed State<int> for @p steps steps on a cluster with one
     *        worker and returns its final field.
     * @param offloaded Receives the master's count of offloaded dispatches.
     */
    std::vector<int> evolve_distributed_state(std::size_t count, std::size_t steps,
                                              std::uint64_t& offloaded)
    {
        std::atomic<int> runs{0};
        InProcessFabric fabric;
        Runtime runtime = Runtime::create().value();
        LocalWorker worker(InProcessTransport::make_worker(fabric), state_step_kernels(runs));

        Cluster cluster =
            Cluster::attach(runtime, InProcessTransport::make_master(fabric), eager_options()).value();
        EXPECT_TRUE(worker.start());
        if (!cluster.wait_for_workers(1, K_WAIT))
            throw std::runtime_error("the worker did not register with the cluster");

        auto field = runtime.state<int>(count).value().distributed();
        for (std::size_t index = 0; index < count; ++index)
        {
            field.current()[index] = static_cast<int>(index);
            field.next()[index] = static_cast<int>(index);
        }

        const std::uint64_t parameters[] = {static_cast<std::uint64_t>(count)};
        NodeOptions step;
        step.operation = K_STATE_STEP;
        step.parameters = parameters;

        auto graph = runtime.graph();
        graph.add_rule(field,
                       [](std::size_t index, const int* current, int* next)
                       { next[index] = current[index] + static_cast<int>(index % 7); },
                       step);
        graph.run(steps).value();

        std::vector<int> result(count);
        for (std::size_t index = 0; index < count; ++index)
            result[index] = field[index];

        offloaded = cluster.offloaded();
        worker.stop();
        return result;
    }

    /** @brief Holds what one run of the registered vector-add left behind. */
    struct RegisteredSum
    {
        std::vector<float> values;   /**< The output buffer after the run. */
        int local_runs = 0;          /**< Runs of the master's own kernel. */
        int worker_runs = 0;         /**< Runs of the worker's kernel. */
        std::uint64_t offloaded = 0; /**< Dispatches the master sent to a worker. */
    };

    /**
     * @brief Runs out = a + b as a task added by add_registered_task(), with no lambda.
     * @param with_worker Attaches a cluster with one worker and shares the buffers.
     */
    RegisteredSum run_registered_sum(std::size_t count, bool with_worker)
    {
        std::atomic<int> local_runs{0};
        std::atomic<int> worker_runs{0};

        const auto master_context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& master_context = *master_context_owner;
        sycl::queue& queue = master_context.devices().queue(0);
        USMFloats a(queue, count, 0.0f);
        USMFloats b(queue, count, 0.0f);
        USMFloats out(queue, count, K_UNWRITTEN);
        a.ramp(1.0f);
        b.ramp(2.0f);

        InProcessFabric fabric;
        std::optional<LocalWorker> worker;
        std::optional<Cluster> cluster;
        if (with_worker)
        {
            worker.emplace(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
            cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric),
                                      eager_options()).value();
            EXPECT_TRUE(worker->start());
            EXPECT_TRUE(cluster->wait_for_workers(1, K_WAIT));
            share(master_context, a);
            share(master_context, b);
            share(master_context, out);
        }

        const sushi_ptr<KernelRegistry> kernels = vec_add_kernels(local_runs);
        TaskMetadata metadata{};
        metadata.name = "registered_sum";
        metadata.op_id = K_VEC_ADD;
        const BufferView inputs[] = {a.view(), b.view()};
        const BufferView outputs[] = {out.view()};

        TaskGraph graph(master_context.offload());
        add_registered_task(graph, *kernels, metadata, span<const BufferView>(inputs, 2),
                            span<const BufferView>(outputs, 1));
        master_context.execution_engine().execute(graph.compile().value()).value();

        RegisteredSum sum;
        sum.values.assign(out.data(), out.data() + out.count());
        sum.local_runs = local_runs.load(std::memory_order_relaxed);
        sum.worker_runs = worker_runs.load(std::memory_order_relaxed);
        sum.offloaded = cluster.has_value() ? cluster->offloaded() : 0;
        if (worker.has_value())
            worker->stop();
        return sum;
    }
} // namespace

/** @brief Tests that inputs reach a worker, its kernel runs there and the output returns. */
TEST(Integration_DistributedE2ETest, OffloadVectorAddRoundTrip)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 1024;
    std::atomic<int> worker_runs{0};
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    sycl::queue& queue = master_context.devices().queue(0);
    USMFloats a(queue, K_COUNT, 0.0f);
    USMFloats b(queue, K_COUNT, 0.0f);
    USMFloats out(queue, K_COUNT, K_UNWRITTEN);
    a.ramp(1.0f);
    b.ramp(2.0f);

    InProcessFabric fabric;
    LocalWorker worker(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
    Cluster cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric),
                                      eager_options()).value();
    ASSERT_TRUE(worker.start());
    ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));
    EXPECT_TRUE(cluster.supports(K_VEC_ADD));

    share(master_context, a);
    share(master_context, b);
    share(master_context, out);

    TaskGraph graph(master_context.offload());
    add_vec_add(graph, "vec_add", a, b, out);
    master_context.execution_engine().execute(graph.compile().value()).value();

    EXPECT_EQ(worker_runs.load(), 1) << "the task ran on the local fallback, not on the worker";
    EXPECT_EQ(cluster.offloaded(), 1u);
    expect_sum(out, a, b);

    worker.stop();
}

/** @brief Tests that two independent tasks both run on the workers of a two-worker cluster. */
TEST(Integration_DistributedE2ETest, FanOutAcrossTwoWorkers)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 256;
    std::atomic<int> first_runs{0};
    std::atomic<int> second_runs{0};
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    sycl::queue& queue = master_context.devices().queue(0);
    USMFloats in(queue, K_COUNT, 0.0f);
    USMFloats first_out(queue, K_COUNT, K_UNWRITTEN);
    USMFloats second_out(queue, K_COUNT, K_UNWRITTEN);
    in.ramp(1.0f);

    InProcessFabric fabric;
    LocalWorker first(InProcessTransport::make_worker(fabric), vec_add_kernels(first_runs));
    LocalWorker second(InProcessTransport::make_worker(fabric), vec_add_kernels(second_runs));
    Cluster cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric),
                                      eager_options()).value();
    ASSERT_TRUE(first.start());
    ASSERT_TRUE(second.start());
    ASSERT_TRUE(cluster.wait_for_workers(2, K_WAIT));
    EXPECT_EQ(cluster.alive_workers(), 2u);

    share(master_context, in);
    share(master_context, first_out);
    share(master_context, second_out);

    TaskGraph graph(master_context.offload());
    add_vec_add(graph, "fan_out_first", in, in, first_out);
    add_vec_add(graph, "fan_out_second", in, in, second_out);
    master_context.execution_engine().execute(graph.compile().value()).value();

    EXPECT_EQ(cluster.offloaded(), 2u);
    EXPECT_EQ(first_runs.load() + second_runs.load(), 2);
    expect_sum(first_out, in, in);
    expect_sum(second_out, in, in);

    first.stop();
    second.stop();
}

/** @brief Tests that a local write raises a shared buffer's version and the worker reads it. */
TEST(Integration_DistributedE2ETest, AutoVersionBumpAcrossLocalWriteThenOffloadedRead)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 512;
    std::atomic<int> worker_runs{0};
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    sycl::queue& queue = master_context.devices().queue(0);
    USMFloats x(queue, K_COUNT, -7.0f);
    USMFloats y(queue, K_COUNT, K_UNWRITTEN);

    InProcessFabric fabric;
    LocalWorker worker(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
    Cluster cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric),
                                      eager_options()).value();
    ASSERT_TRUE(worker.start());
    ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

    share(master_context, x);
    share(master_context, y);
    const std::optional<BufferHandle> before = cluster.buffer_handle(x.data());
    ASSERT_TRUE(before.has_value());

    TaskGraph graph(master_context.offload());
    TaskMetadata fill_metadata{};
    fill_metadata.name = "local_fill";
    float* values = x.data();
    Internal::Graph::Node& writer = graph.add_task(
        fill_metadata, {}, {values},
        [values](sycl::queue& local_queue, const std::vector<sycl::event>&) -> sycl::event
        {
            return local_queue.submit([&](sycl::handler& handler)
            {
                SushiRuntime::Detail::enqueue_host_task(handler, [=]
                {
                    for (std::size_t index = 0; index < K_COUNT; ++index)
                        values[index] = static_cast<float>(index + 1);
                });
            });
        });
    add_vec_add(graph, "double_on_worker", x, x, y);

    CompiledGraph compiled = graph.compile().value();
    const OffloadState* writer_state = writer.offload_state();
    ASSERT_NE(writer_state, nullptr) << "a local node that writes a shared buffer keeps a state";
    EXPECT_FALSE(writer_state->offloadable);

    master_context.execution_engine().execute(compiled).value();

    const std::optional<BufferHandle> after = cluster.buffer_handle(x.data());
    ASSERT_TRUE(after.has_value());
    EXPECT_GT(after->version, before->version);
    EXPECT_EQ(worker_runs.load(), 1);
    expect_elements(y.data(), y.count(),
                    [](std::size_t index) { return 2.0f * static_cast<float>(index + 1); });

    worker.stop();
}

/** @brief Tests the vector-add offload over TCP on the loopback address. */
TEST(Integration_DistributedE2ETest, OffloadOverTCPLoopback)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 1024;
    std::atomic<int> worker_runs{0};
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    sycl::queue& queue = master_context.devices().queue(0);
    USMFloats a(queue, K_COUNT, 0.0f);
    USMFloats b(queue, K_COUNT, 0.0f);
    USMFloats out(queue, K_COUNT, K_UNWRITTEN);
    a.ramp(1.0f);
    b.ramp(3.0f);

    // The default options bind 127.0.0.1 on a port the OS picks, known once attach() returns.
    const std::shared_ptr<TCPTransport> master_transport =
        TCPTransport::make_master(TCPMasterOptions());
    Cluster cluster = Cluster::attach(master_context, master_transport, eager_options()).value();
    const Endpoint listening = master_transport->local_endpoint();
    ASSERT_NE(listening.port, 0);

    LocalWorker worker(TCPTransport::make_worker(listening), vec_add_kernels(worker_runs));
    ASSERT_TRUE(worker.start());
    ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

    share(master_context, a);
    share(master_context, b);
    share(master_context, out);

    TaskGraph graph(master_context.offload());
    add_vec_add(graph, "vec_add_tcp", a, b, out);
    master_context.execution_engine().execute(graph.compile().value()).value();

    EXPECT_EQ(worker_runs.load(), 1);
    expect_sum(out, a, b);
    const ClusterStatistics statistics = cluster.statistics();
    EXPECT_GT(statistics.bytes_sent, 2 * a.size_bytes());
    EXPECT_GT(statistics.bytes_received, out.size_bytes());

    worker.stop();
}

/** @brief Tests that a task whose worker falls silent is taken back and finishes locally. */
TEST(Integration_DistributedE2ETest, WorkerDeathFallsBackToLocal)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 1024;
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    sycl::queue& queue = master_context.devices().queue(0);
    USMFloats a(queue, K_COUNT, 0.0f);
    USMFloats b(queue, K_COUNT, 0.0f);
    USMFloats out(queue, K_COUNT, K_UNWRITTEN);
    a.ramp(1.0f);
    b.ramp(2.0f);

    InProcessFabric fabric;
    ScriptedWorker worker(fabric, K_VEC_ADD, DispatchAnswer::FallSilent);
    ClusterOptions options = eager_options();
    options.liveness_timeout = std::chrono::milliseconds(300);
    Cluster cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric),
                                      std::move(options)).value();
    ASSERT_TRUE(worker.start());
    ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

    share(master_context, a);
    share(master_context, b);
    share(master_context, out);

    TaskGraph graph(master_context.offload());
    add_vec_add(graph, "vec_add_death", a, b, out);
    CompiledGraph compiled = graph.compile().value();
    BackgroundRun run(master_context, compiled);
    ASSERT_TRUE(run.wait()) << "execute() did not return after the worker fell silent";

    EXPECT_GE(worker.dispatch_count(), 1u) << "the task never reached the worker";
    EXPECT_GE(cluster.redispatched(), 1u);
    expect_sum(out, a, b);
}

/** @brief Tests that the output fence refuses a chunk of an old version and takes a current one. */
TEST(Integration_DistributedReliabilityTest, StaleWritebackIsFenced)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 16;
    constexpr float K_AUTHORITATIVE = 7.0f;
    constexpr float K_POISON = 999.0f;
    constexpr float K_FRESH = 42.0f;
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    sycl::queue& queue = master_context.devices().queue(0);
    USMFloats a(queue, K_COUNT, 1.0f);
    USMFloats b(queue, K_COUNT, 2.0f);
    USMFloats out(queue, K_COUNT, K_AUTHORITATIVE);

    InProcessFabric fabric;
    ScriptedWorker worker(fabric, K_VEC_ADD, DispatchAnswer::Accept);
    Cluster cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric),
                                      eager_options()).value();
    ASSERT_TRUE(worker.start());
    ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

    share(master_context, a);
    share(master_context, b);
    share(master_context, out);

    TaskGraph graph(master_context.offload());
    add_vec_add(graph, "fenced", a, b, out);
    CompiledGraph compiled = graph.compile().value();
    BackgroundRun run(master_context, compiled);
    ASSERT_TRUE(worker.wait_for_dispatches(1));

    const Wire::TaskDispatchMessage dispatch = worker.dispatch(0);
    ASSERT_EQ(dispatch.outputs.size(), 1u);
    const BufferHandle current = dispatch.outputs[0];
    ASSERT_GT(current.version, 1u);
    BufferHandle stale = current;
    stale.version = current.version - 1;

    const std::vector<float> poison(K_COUNT, K_POISON);
    ASSERT_EQ(worker.send_stream({output_chunk_frame(dispatch.task_id, stale, bytes_of(poison))}),
              SendStatus::Queued);
    ASSERT_TRUE(eventually([&] { return cluster.stale_writebacks() >= 1; }))
        << "the chunk of the old version was not refused";
    expect_elements(out.data(), out.count(), [](std::size_t) { return K_AUTHORITATIVE; });

    const std::vector<float> fresh(K_COUNT, K_FRESH);
    ASSERT_EQ(worker.send_stream({output_chunk_frame(dispatch.task_id, current, bytes_of(fresh)),
                                  success_frame(dispatch)}),
              SendStatus::Queued);
    ASSERT_TRUE(run.wait()) << "execute() did not return after the worker's result";

    expect_elements(out.data(), out.count(), [](std::size_t) { return K_FRESH; });
    const ClusterStatistics statistics = cluster.statistics();
    EXPECT_EQ(statistics.stale_writebacks, 1u);
    EXPECT_EQ(statistics.chunks_received, 1u);
    EXPECT_EQ(statistics.redispatched, 0u);
    EXPECT_EQ(worker.refused_sends(), 0);
}

/** @brief Tests that a task whose input chunks are all lost fails remotely and runs locally. */
TEST(Integration_DistributedReliabilityTest, LostBufferPushTimesOutInsteadOfHanging)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 64;
    std::atomic<int> worker_runs{0};
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    sycl::queue& queue = master_context.devices().queue(0);
    USMFloats a(queue, K_COUNT, 0.0f);
    USMFloats b(queue, K_COUNT, 0.0f);
    USMFloats out(queue, K_COUNT, K_UNWRITTEN);
    a.ramp(1.0f);
    b.ramp(2.0f);

    InProcessFabric fabric;
    fabric.set_fault_policy([](const FaultedFrame& frame)
    {
        const bool is_pull_reply = frame.direction == FrameDirection::MasterToWorker &&
                                   frame.type == MessageType::BufferChunk;
        return is_pull_reply ? FaultAction::Drop : FaultAction::Deliver;
    });

    WorkerAgentOptions worker_options;
    worker_options.pull_idle_timeout = std::chrono::milliseconds(200);
    LocalWorker worker(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs),
                       std::move(worker_options));
    Cluster cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric),
                                      eager_options()).value();
    ASSERT_TRUE(worker.start());
    ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

    share(master_context, a);
    share(master_context, b);
    share(master_context, out);

    TaskGraph graph(master_context.offload());
    add_vec_add(graph, "vec_add_lost_chunks", a, b, out);
    CompiledGraph compiled = graph.compile().value();
    BackgroundRun run(master_context, compiled);
    ASSERT_TRUE(run.wait()) << "execute() did not return although the worker's pull timed out";

    EXPECT_EQ(worker_runs.load(), 0) << "the kernel ran without its inputs";
    EXPECT_GE(worker.statistics().tasks_failed_input_unavailable, 1u);
    EXPECT_GE(cluster.redispatched(), 1u);
    expect_sum(out, a, b);

    worker.stop();
}

/** @brief Tests that the fluent surface builds and runs an offloaded task. */
TEST(Integration_DistributedE2ETest, FluentClusterOffload)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 1024;
    std::atomic<int> worker_runs{0};
    InProcessFabric fabric;
    Runtime runtime = Runtime::create().value();
    LocalWorker worker(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
    Cluster cluster =
        Cluster::attach(runtime, InProcessTransport::make_master(fabric), eager_options()).value();
    ASSERT_TRUE(worker.start());
    ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

    auto a = runtime.buffer<float>(K_COUNT).value().distributed();
    auto b = runtime.buffer<float>(K_COUNT).value().distributed();
    auto out = runtime.buffer<float>(K_COUNT).value().distributed();
    for (std::size_t index = 0; index < K_COUNT; ++index)
    {
        a[index] = static_cast<float>(index);
        b[index] = static_cast<float>(2 * index);
        out[index] = K_UNWRITTEN;
    }

    const float* left = a.data();
    const float* right = b.data();
    float* sum = out.data();
    const std::uint64_t parameters[] = {static_cast<std::uint64_t>(K_COUNT)};
    NodeOptions vector_add;
    vector_add.operation = K_VEC_ADD;
    vector_add.parameters = parameters;

    auto graph = runtime.graph();
    graph.add(K_COUNT, Reads(a, b), Writes(out),
              [left, right, sum](std::size_t index) { sum[index] = left[index] + right[index]; },
              vector_add);
    graph.run().value();

    EXPECT_GE(cluster.offloaded(), 1u);
    EXPECT_GE(worker_runs.load(), 1);
    expect_elements(sum, K_COUNT,
                    [left, right](std::size_t index) { return left[index] + right[index]; });

    worker.stop();
}

/** @brief Tests offload of a double-buffered State across even and odd step counts. */
TEST(Integration_DistributedE2ETest, FluentDistributedStateParity)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 1024;
    constexpr std::size_t K_EVEN_STEPS = 4;
    constexpr std::size_t K_ODD_STEPS = 5;
    std::uint64_t offloaded_even = 0;
    std::uint64_t offloaded_odd = 0;
    const std::vector<int> even = evolve_distributed_state(K_COUNT, K_EVEN_STEPS, offloaded_even);
    const std::vector<int> odd = evolve_distributed_state(K_COUNT, K_ODD_STEPS, offloaded_odd);

    EXPECT_GE(offloaded_even, 1u);
    EXPECT_GE(offloaded_odd, 1u);
    expect_elements(even.data(), even.size(), [](std::size_t index)
    {
        return static_cast<int>(index + K_EVEN_STEPS * (index % 7));
    });
    expect_elements(odd.data(), odd.size(), [](std::size_t index)
    {
        return static_cast<int>(index + K_ODD_STEPS * (index % 7));
    });
}

/** @brief Tests detach() and the displacement of one master by a later attach(). */
TEST(Integration_DistributedE2ETest, ClusterDetachAndDisplacement)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    EXPECT_EQ(master_context.offload(), nullptr);

    const auto attach_master = [&master_context](InProcessFabric& fabric)
    {
        return Cluster::attach(master_context, InProcessTransport::make_master(fabric)).value();
    };

    InProcessFabric first_fabric;
    Cluster first = attach_master(first_fabric);
    EXPECT_NE(master_context.offload(), nullptr);

    first.detach();
    EXPECT_EQ(master_context.offload(), nullptr);
    first.detach();
    EXPECT_EQ(master_context.offload(), nullptr) << "a second detach() must do nothing";

    InProcessFabric second_fabric;
    Cluster second = attach_master(second_fabric);
    const OffloadCoordinator* second_installed = master_context.offload();
    ASSERT_NE(second_installed, nullptr);

    InProcessFabric third_fabric;
    Cluster third = attach_master(third_fabric);
    const OffloadCoordinator* third_installed = master_context.offload();
    ASSERT_NE(third_installed, nullptr);
    EXPECT_NE(third_installed, second_installed);

    second.detach();
    EXPECT_EQ(master_context.offload(), third_installed)
        << "a displaced handle removed the master that replaced it";
}

/** @brief Tests that a handle outlives its runtime and that its detach() then does nothing. */
TEST(Integration_DistributedE2ETest, ClusterHandleOutlivesRuntime)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    InProcessFabric fabric;
    auto master_context = SushiRuntime::Test::make_context();
    Cluster cluster = Cluster::attach(*master_context, InProcessTransport::make_master(fabric)).value();
    const Cluster copy = cluster;

    master_context.reset();

    cluster.detach();
    EXPECT_EQ(copy.alive_workers(), 0u);
    EXPECT_EQ(copy.offloaded(), 0u);
}

/** @brief Tests that stopping the master returns a task still out on a worker to the scheduler. */
TEST(Integration_DistributedE2ETest, StopResubmitsPendingNodes)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 128;
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    sycl::queue& queue = master_context.devices().queue(0);
    USMFloats a(queue, K_COUNT, 0.0f);
    USMFloats b(queue, K_COUNT, 0.0f);
    USMFloats out(queue, K_COUNT, K_UNWRITTEN);
    a.ramp(1.0f);
    b.ramp(2.0f);

    InProcessFabric fabric;
    ScriptedWorker worker(fabric, K_VEC_ADD, DispatchAnswer::Accept);
    Cluster cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric),
                                      eager_options()).value();
    ASSERT_TRUE(worker.start());
    ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

    share(master_context, a);
    share(master_context, b);
    share(master_context, out);

    TaskGraph graph(master_context.offload());
    add_vec_add(graph, "held_by_worker", a, b, out);
    CompiledGraph compiled = graph.compile().value();
    BackgroundRun run(master_context, compiled);
    ASSERT_TRUE(worker.wait_for_dispatches(1));
    EXPECT_EQ(cluster.statistics().pending_tasks, 1u);

    master_context.offload()->stop();

    ASSERT_TRUE(run.wait()) << "the run hung on a node the stopped master still held";
    expect_sum(out, a, b);
    const ClusterStatistics statistics = cluster.statistics();
    EXPECT_EQ(statistics.offloaded, 1u);
    EXPECT_EQ(statistics.redispatched, 1u);
    EXPECT_EQ(statistics.pending_tasks, 0u);
}

/** @brief Tests that a stopped master offloads nothing and the graph runs locally. */
TEST(Integration_DistributedE2ETest, TryOffloadAfterStopRunsLocally)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 128;
    std::atomic<int> worker_runs{0};
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    sycl::queue& queue = master_context.devices().queue(0);
    USMFloats a(queue, K_COUNT, 0.0f);
    USMFloats b(queue, K_COUNT, 0.0f);
    USMFloats out(queue, K_COUNT, K_UNWRITTEN);
    a.ramp(1.0f);
    b.ramp(2.0f);

    InProcessFabric fabric;
    LocalWorker worker(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
    Cluster cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric),
                                      eager_options()).value();
    ASSERT_TRUE(worker.start());
    ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

    share(master_context, a);
    share(master_context, b);
    share(master_context, out);

    TaskGraph graph(master_context.offload());
    Internal::Graph::Node& node = add_vec_add(graph, "after_stop", a, b, out);
    CompiledGraph compiled = graph.compile().value();
    const OffloadState* state = node.offload_state();
    ASSERT_NE(state, nullptr);
    ASSERT_TRUE(state->offloadable) << "the node must be one the scheduler offers";

    master_context.offload()->stop();

    BackgroundRun run(master_context, compiled);
    ASSERT_TRUE(run.wait());

    EXPECT_EQ(worker_runs.load(), 0);
    EXPECT_EQ(cluster.offloaded(), 0u);
    EXPECT_GE(cluster.declined(), 1u);
    expect_sum(out, a, b);

    worker.stop();
}

/** @brief Tests that each local step of a State bumps the buffer that step wrote. */
TEST(Integration_DistributedE2ETest, DynamicStateBumpsTheLiveBufferAfterSwap)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 64;
    constexpr std::size_t K_STEPS = 2;
    InProcessFabric fabric;
    Runtime runtime = Runtime::create().value();
    Cluster cluster = Cluster::attach(runtime, InProcessTransport::make_master(fabric)).value();

    auto field = runtime.state<int>(K_COUNT).value().distributed();
    for (std::size_t index = 0; index < K_COUNT; ++index)
    {
        field.current()[index] = static_cast<int>(index);
        field.next()[index] = static_cast<int>(index);
    }

    void* front = field.current();
    void* back = field.next();
    const std::optional<BufferHandle> front_before = cluster.buffer_handle(front);
    const std::optional<BufferHandle> back_before = cluster.buffer_handle(back);
    ASSERT_TRUE(front_before.has_value());
    ASSERT_TRUE(back_before.has_value());

    // No worker is attached, so both steps run locally: the first writes back, the second front.
    const std::uint64_t parameters[] = {static_cast<std::uint64_t>(K_COUNT)};
    NodeOptions step;
    step.operation = K_STATE_STEP;
    step.parameters = parameters;

    auto graph = runtime.graph();
    graph.add_rule(field,
                   [](std::size_t index, const int* current, int* next)
                   { next[index] = current[index] + static_cast<int>(index % 7); },
                   step);
    graph.run(K_STEPS).value();

    const std::optional<BufferHandle> front_after = cluster.buffer_handle(front);
    const std::optional<BufferHandle> back_after = cluster.buffer_handle(back);
    ASSERT_TRUE(front_after.has_value());
    ASSERT_TRUE(back_after.has_value());
    EXPECT_GT(front_after->version, front_before->version)
        << "the step after the swap did not bump the buffer it wrote";
    EXPECT_GT(back_after->version, back_before->version);
    EXPECT_EQ(front_after->version - front_before->version,
              back_after->version - back_before->version)
        << "two steps wrote each buffer once, so both versions rise by the same amount";

    EXPECT_EQ(cluster.offloaded(), 0u);
    for (std::size_t index = 0; index < K_COUNT; ++index)
        ASSERT_EQ(field[index], static_cast<int>(index + K_STEPS * (index % 7)));
}

/** @brief Tests that an attach whose transport does not start throws and installs nothing. */
TEST(Integration_DistributedE2ETest, AttachStartFailureLeavesNothingInstalled)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    const std::string refusal = "cannot listen on 127.0.0.1:9: the stub refuses";
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    const auto transport = std::make_shared<StubTransport>(
        TransportExposure::Loopback, TransportResult{TransportStatus::BindFailed, refusal});

    const Result<Cluster> attached = Cluster::attach(master_context, transport);
    ASSERT_FALSE(attached.has_value())
        << "attach() returned a cluster although the transport did not start";
    const std::string message = attached.error().message;

    EXPECT_NE(message.find(refusal), std::string::npos) << "the error says: " << message;
    EXPECT_EQ(transport->start_calls(), 1);
    EXPECT_EQ(master_context.offload(), nullptr);

    InProcessFabric fabric;
    Cluster cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric)).value();
    EXPECT_NE(master_context.offload(), nullptr) << "the context takes no master after a failure";
    cluster.detach();
}

/** @brief Tests that the attempt cap of one run does not keep a node local in the next run. */
TEST(Integration_DistributedE2ETest, AttemptCapDoesNotOutliveTheRun)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 128;
    constexpr std::size_t K_RUNS = 2;
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    sycl::queue& queue = master_context.devices().queue(0);
    USMFloats a(queue, K_COUNT, 0.0f);
    USMFloats b(queue, K_COUNT, 0.0f);
    USMFloats out(queue, K_COUNT, K_UNWRITTEN);
    a.ramp(1.0f);
    b.ramp(2.0f);

    InProcessFabric fabric;
    ScriptedWorker worker(fabric, K_VEC_ADD, DispatchAnswer::Fail);
    Cluster cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric),
                                      eager_options()).value();
    ASSERT_TRUE(worker.start());
    ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

    share(master_context, a);
    share(master_context, b);
    share(master_context, out);

    TaskGraph graph(master_context.offload());
    add_vec_add(graph, "always_fails_remotely", a, b, out);
    CompiledGraph compiled = graph.compile().value();
    compiled.capture_base_state();

    // Every dispatch fails on the worker, so each run spends the cap and then runs locally.
    for (std::size_t round = 1; round <= K_RUNS; ++round)
    {
        SCOPED_TRACE(round);
        out.fill(K_UNWRITTEN);
        compiled.rearm();
        BackgroundRun run(master_context, compiled);
        const bool is_finished = run.wait();
        const ClusterStatistics seen = cluster.statistics();
        ASSERT_TRUE(is_finished)
            << "dispatches " << worker.dispatch_count() << ", offloaded " << seen.offloaded
            << ", declined " << seen.declined << ", redispatched " << seen.redispatched
            << ", pending " << seen.pending_tasks << ", alive " << seen.alive_workers;

        expect_sum(out, a, b);
        EXPECT_EQ(worker.dispatch_count(), round * K_MAXIMUM_OFFLOAD_ATTEMPTS);
        EXPECT_EQ(cluster.declined(), round);
        EXPECT_EQ(cluster.redispatched(), round * K_MAXIMUM_OFFLOAD_ATTEMPTS);
    }
}

/** @brief Tests that attach() refuses an empty secret on a transport other machines reach. */
TEST(Integration_DistributedE2ETest, RefusesEmptySecretOnNetworkExposure)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    const auto exposed =
        std::make_shared<StubTransport>(TransportExposure::Network, TransportResult());

    EXPECT_FALSE(Cluster::attach(master_context, exposed).has_value());
    EXPECT_EQ(exposed->start_calls(), 0) << "the transport was started before the refusal";
    EXPECT_EQ(master_context.offload(), nullptr);

    const std::vector<std::uint8_t> key(SharedSecret::K_MINIMUM_BYTES, 0x42);
    ClusterOptions options;
    options.secret =
        SharedSecret::from_bytes(span<const std::uint8_t>(key.data(), key.size())).value();
    Cluster cluster = Cluster::attach(master_context, exposed, std::move(options)).value();
    EXPECT_EQ(exposed->start_calls(), 1);
    EXPECT_NE(master_context.offload(), nullptr);
    cluster.detach();
}

/** @brief Tests that a task added with no lambda gives one result with a worker and with none. */
TEST(Integration_DistributedE2ETest, RegisteredTaskRunsWithAndWithoutAWorker)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 512;
    const RegisteredSum local = run_registered_sum(K_COUNT, false);
    const RegisteredSum remote = run_registered_sum(K_COUNT, true);

    EXPECT_EQ(local.local_runs, 1);
    EXPECT_EQ(local.worker_runs, 0);
    EXPECT_EQ(remote.local_runs, 0) << "the registered kernel ran locally beside a worker";
    EXPECT_EQ(remote.worker_runs, 1);
    EXPECT_EQ(remote.offloaded, 1u);

    ASSERT_EQ(local.values.size(), K_COUNT);
    ASSERT_EQ(remote.values.size(), K_COUNT);
    expect_elements(local.values.data(), K_COUNT,
                    [](std::size_t index) { return 3.0f * static_cast<float>(index); });
    expect_elements(remote.values.data(), K_COUNT,
                    [&local](std::size_t index) { return local.values[index]; });
}

/** @brief Tests that an attach on a transport another master runs on throws and stops nothing. */
TEST(Integration_DistributedE2ETest, AttachOnAStartedTransportLeavesTheFirstMasterServing)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    std::atomic<int> worker_runs{0};
    const auto first_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& first_context = *first_context_owner;
    const auto second_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& second_context = *second_context_owner;
    InProcessFabric fabric;
    const std::shared_ptr<InProcessTransport> transport = InProcessTransport::make_master(fabric);
    Cluster first = Cluster::attach(first_context, transport).value();
    const OffloadCoordinator* const installed = first_context.offload();

    EXPECT_FALSE(Cluster::attach(second_context, transport).has_value());
    EXPECT_EQ(second_context.offload(), nullptr);
    EXPECT_EQ(first_context.offload(), installed);

    // A worker registers only with a master whose transport still runs.
    LocalWorker worker(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
    ASSERT_TRUE(worker.start());
    EXPECT_TRUE(first.wait_for_workers(1, K_WAIT))
        << "the failed attach stopped the transport of the master attached before";

    worker.stop();
    first.detach();
}

/** @brief Tests that a detached handle reports no worker, no kernel and no reached count. */
TEST(Integration_DistributedE2ETest, DetachedClusterReportsNoWorkers)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    std::atomic<int> worker_runs{0};
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    InProcessFabric fabric;
    LocalWorker first(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
    LocalWorker second(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
    Cluster cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric)).value();
    ASSERT_TRUE(first.start());
    ASSERT_TRUE(second.start());
    ASSERT_TRUE(cluster.wait_for_workers(2, K_WAIT));
    ASSERT_TRUE(cluster.supports(K_VEC_ADD));
    const Cluster copy = cluster;

    cluster.detach();

    EXPECT_FALSE(cluster.wait_for_workers(2, std::chrono::milliseconds(100)))
        << "a detached handle reached the count of the workers it had";
    EXPECT_EQ(cluster.alive_workers(), 0u);
    EXPECT_FALSE(cluster.supports(K_VEC_ADD));

    const ClusterStatistics statistics = copy.statistics();
    EXPECT_EQ(statistics.alive_workers, 0u);
    EXPECT_EQ(statistics.pending_tasks, 0u);
    EXPECT_TRUE(statistics.workers.empty());

    first.stop();
    second.stop();
}

/**
 * @brief Tests that attach() is refused while a run is in flight, and that detach() then stops
 *        the master and leaves it installed until the run has returned.
 */
TEST(Integration_DistributedE2ETest, AttachIsRefusedAndDetachDeferredDuringARun)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t K_COUNT = 128;
    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    sycl::queue& queue = master_context.devices().queue(0);
    USMFloats a(queue, K_COUNT, 0.0f);
    USMFloats b(queue, K_COUNT, 0.0f);
    USMFloats out(queue, K_COUNT, K_UNWRITTEN);
    a.ramp(1.0f);
    b.ramp(2.0f);

    InProcessFabric fabric;
    ScriptedWorker worker(fabric, K_VEC_ADD, DispatchAnswer::Accept);
    Cluster cluster = Cluster::attach(master_context, InProcessTransport::make_master(fabric),
                                      eager_options()).value();
    ASSERT_TRUE(worker.start());
    ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));
    const OffloadCoordinator* const installed = master_context.offload();

    share(master_context, a);
    share(master_context, b);
    share(master_context, out);

    TaskGraph graph(master_context.offload());
    add_vec_add(graph, "held_during_attach", a, b, out);
    CompiledGraph compiled = graph.compile().value();
    BackgroundRun run(master_context, compiled);
    ASSERT_TRUE(worker.wait_for_dispatches(1));

    const auto second_transport =
        std::make_shared<StubTransport>(TransportExposure::Loopback, TransportResult());
    const Result<Cluster> second = Cluster::attach(master_context, second_transport);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, ErrorCode::InvalidState);
    EXPECT_EQ(second_transport->start_calls(), 0) << "the refusal came after a transport start";
    EXPECT_EQ(master_context.offload(), installed);
    EXPECT_EQ(cluster.alive_workers(), 1u) << "the refused attach stopped the installed master";
    EXPECT_EQ(cluster.statistics().pending_tasks, 1u);

    cluster.detach();
    EXPECT_EQ(master_context.offload(), installed)
        << "detach() removed the coordinator the run in flight reads";
    EXPECT_EQ(cluster.alive_workers(), 0u);

    ASSERT_TRUE(run.wait()) << "the run hung on a node the detached master still held";
    expect_sum(out, a, b);
    EXPECT_EQ(cluster.statistics().redispatched, 1u);

    cluster.detach();
    EXPECT_EQ(master_context.offload(), nullptr)
        << "detach() after the run left the stopped master installed";
}

/** @brief Tests that a handle that was moved from still names the master it named before. */
TEST(Integration_DistributedE2ETest, MovedFromHandleStillNamesTheMaster)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    InProcessFabric fabric;
    Cluster source = Cluster::attach(master_context, InProcessTransport::make_master(fabric)).value();
    Cluster moved = std::move(source);
    Cluster assigned = moved;
    assigned = std::move(moved);

    EXPECT_EQ(source.alive_workers(), 0u);
    EXPECT_EQ(moved.offloaded(), 0u);
    EXPECT_TRUE(assigned.wait_for_workers(0, std::chrono::milliseconds(0)));

    source.detach();

    EXPECT_EQ(master_context.offload(), nullptr) << "the moved-from handle names no master";
    EXPECT_FALSE(moved.wait_for_workers(0, std::chrono::milliseconds(0)))
        << "the handle moved to names another master than its source";
    EXPECT_FALSE(assigned.wait_for_workers(0, std::chrono::milliseconds(0)));
}

/** @brief Tests that a liveness or handshake timeout of 0 is refused and a deadline of 0 is not. */
TEST(Integration_DistributedE2ETest, RefusesATimeoutBelowOneMillisecond)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    const auto master_context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& master_context = *master_context_owner;
    const auto refusal_of = [&master_context](ClusterOptions options)
    {
        const auto transport =
            std::make_shared<StubTransport>(TransportExposure::Loopback, TransportResult());
        const Result<Cluster> attached =
            Cluster::attach(master_context, transport, std::move(options));
        EXPECT_EQ(transport->start_calls(), 0) << "the transport was started before the refusal";
        return attached.has_value() ? std::string() : attached.error().message;
    };

    ClusterOptions without_liveness;
    without_liveness.liveness_timeout = std::chrono::milliseconds(0);
    const std::string liveness_refusal = refusal_of(std::move(without_liveness));
    EXPECT_NE(liveness_refusal.find("ClusterOptions::liveness_timeout is 0 ms"),
              std::string::npos)
        << "the error says: " << liveness_refusal;

    ClusterOptions without_handshake;
    without_handshake.handshake_timeout = std::chrono::milliseconds(0);
    const std::string handshake_refusal = refusal_of(std::move(without_handshake));
    EXPECT_NE(handshake_refusal.find("ClusterOptions::handshake_timeout is 0 ms"),
              std::string::npos)
        << "the error says: " << handshake_refusal;
    EXPECT_EQ(master_context.offload(), nullptr);

    ClusterOptions without_deadline;
    without_deadline.task_deadline = std::chrono::milliseconds(0);
    without_deadline.liveness_timeout = K_MINIMUM_TIMEOUT;
    without_deadline.handshake_timeout = K_MINIMUM_TIMEOUT;
    Cluster cluster = Cluster::attach(
        master_context,
        std::make_shared<StubTransport>(TransportExposure::Loopback, TransportResult()),
        std::move(without_deadline)).value();

    const Result<void> refused = cluster.liveness_timeout(std::chrono::milliseconds(0));
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidState);
    EXPECT_TRUE(cluster.liveness_timeout(K_MINIMUM_TIMEOUT).has_value());
    cluster.task_deadline(std::chrono::milliseconds(0));
    cluster.detach();
}

/** @brief Tests that a worker agent seals the registry it is built with, before start(). */
TEST(Integration_DistributedE2EDeathTest, WorkerAgentSealsItsRegistry)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    std::atomic<int> worker_runs{0};
    InProcessFabric fabric;
    const sushi_ptr<KernelRegistry> kernels = vec_add_kernels(worker_runs);
    ASSERT_FALSE(kernels->is_sealed());

    LocalWorker worker(InProcessTransport::make_worker(fabric), kernels);

    EXPECT_TRUE(kernels->is_sealed()) << "the agent read its operations from an open registry";
    EXPECT_FALSE(kernels->has(K_STATE_STEP));
    EXPECT_DEATH(kernels->register_kernel(K_STATE_STEP, vec_add_kernel(worker_runs)),
                 "the registry is sealed");
}
