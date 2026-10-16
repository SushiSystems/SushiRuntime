/**************************************************************************/
/* test_distributed.cpp                                                   */
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
 * @file test_distributed.cpp
 * @brief Tests offload end to end, under fault injection and over faulty TCP links.
 * @see tests/extension/distributed/README.md
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
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
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
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/api/fluent.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/registered_task.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiLog/log/log_level.hpp>
#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_sink.hpp>
#include <SushiLog/log/log_record.hpp>
#include <SushiRuntime/log/log_gate.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/device_probe/device_probe.hpp>
#include <SushiRuntime/cluster_load/offload_policy.hpp>
#include <SushiRuntime/cluster_load/cluster_statistics.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/endpoint.hpp>
#include <SushiRuntime/frame_transport/frame_sending.hpp>
#include <SushiRuntime/link_security/handshake.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>
#include <SushiRuntime/network_socket/socket.hpp>
#include <SushiRuntime/offload_coordination/offload_dispatcher.hpp>

#include "tcp_fault_proxy.hpp"

using namespace SushiRuntime;

/** @brief Tests kernel offload from a master to workers, end to end, on a SYCL device. */
namespace EndToEndTests
{
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
                           span<const BufferView> outputs, const NodeOptions&) -> sycl::event
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
                        span<const BufferView> outputs, const NodeOptions&) -> sycl::event
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

        /** @brief Shares a buffer with the workers of a cluster. */
        void share(Cluster& cluster, const USMFloats& buffer)
        {
            cluster.share(buffer.data(), buffer.size_bytes());
        }

        /** @brief Adds an op-tagged task out = a + b whose local body computes the same sum. */
        void add_vec_add(Graph& graph, const char* name, const USMFloats& a, const USMFloats& b,
                         const USMFloats& out)
        {
            NodeOptions options;
            options.name = name;
            options.operation = K_VEC_ADD;

            float* left = a.data();
            float* right = b.data();
            float* sum = out.data();
            const std::size_t count = out.count();
            graph.add_task(
                Reads(BufferRegion(left), BufferRegion(right)), Writes(BufferRegion(sum)),
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
                }, options);
        }

        /**
         * @brief Runs a graph once on its own thread, so a run that never returns fails a test
         *        by timeout.
         */
        class BackgroundRun final
        {
            public:
                /** @brief Starts the run; @p graph outlives this object. */
                explicit BackgroundRun(Graph& graph)
                {
                    auto done = std::make_shared<std::promise<void>>();
                    finished_ = done->get_future();
                    thread_ = std::thread([&graph, done]
                    {
                        graph.run().value();
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
                /// @brief Becomes ready when run() returned.
                std::future<void> finished_;

                /// @brief The thread that calls run().
                std::thread thread_;
        };

        /**
         * @brief Runs one real worker: its own runtime and the agent that serves a transport.
         */
        class LocalWorker final
        {
            public:
                /** @brief Builds the worker; nothing runs until start(). */
                LocalWorker(std::shared_ptr<Transport> transport, sushi_ptr<KernelRegistry> kernels,
                            WorkerAgentOptions options = WorkerAgentOptions())
                    : agent_(runtime_, std::move(transport), std::move(kernels), std::move(options))
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
                Runtime runtime_ = Runtime::create().value();

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

            auto field = cluster.share(runtime.state<int>(count).value());
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
         * @brief Runs out = a + b as a task recorded by record_registered_task(), with no lambda.
         * @param with_worker Attaches a cluster with one worker and shares the buffers.
         */
        RegisteredSum run_registered_sum(std::size_t count, bool with_worker)
        {
            std::atomic<int> local_runs{0};
            std::atomic<int> worker_runs{0};

            Runtime master_runtime = Runtime::create().value();
            RuntimeContext& master_context =
                SushiRuntime::Internal::ContextAccess::context(master_runtime);
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
                cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          eager_options()).value();
                EXPECT_TRUE(worker->start());
                EXPECT_TRUE(cluster->wait_for_workers(1, K_WAIT));
                share(*cluster, a);
                share(*cluster, b);
                share(*cluster, out);
            }

            const sushi_ptr<KernelRegistry> kernels = vec_add_kernels(local_runs);
            NodeOptions options;
            options.name = "registered_sum";
            options.operation = K_VEC_ADD;
            const BufferView inputs[] = {a.view(), b.view()};
            const BufferView outputs[] = {out.view()};

            auto graph = master_runtime.graph();
            graph.extend([&](Recorder& recorder)
            {
                record_registered_task(recorder, *kernels, options,
                                       span<const BufferView>(inputs, 2),
                                       span<const BufferView>(outputs, 1));
            });
            graph.run().value();

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
    TEST(Medium_DistributedE2ETest, OffloadVectorAddRoundTrip)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        constexpr std::size_t K_COUNT = 1024;
        std::atomic<int> worker_runs{0};
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        sycl::queue& queue = master_context.devices().queue(0);
        USMFloats a(queue, K_COUNT, 0.0f);
        USMFloats b(queue, K_COUNT, 0.0f);
        USMFloats out(queue, K_COUNT, K_UNWRITTEN);
        a.ramp(1.0f);
        b.ramp(2.0f);

        InProcessFabric fabric;
        LocalWorker worker(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          eager_options()).value();
        ASSERT_TRUE(worker.start());
        ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));
        EXPECT_TRUE(cluster.supports(K_VEC_ADD));

        share(cluster, a);
        share(cluster, b);
        share(cluster, out);

        auto graph = master_runtime.graph();
        add_vec_add(graph, "vec_add", a, b, out);
        graph.run().value();

        EXPECT_EQ(worker_runs.load(), 1) << "the task ran on the local fallback, not on the worker";
        EXPECT_EQ(cluster.offloaded(), 1u);
        expect_sum(out, a, b);

        worker.stop();
    }

    /** @brief Tests that two independent tasks both run on the workers of a two-worker cluster. */
    TEST(Medium_DistributedE2ETest, FanOutAcrossTwoWorkers)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        constexpr std::size_t K_COUNT = 256;
        std::atomic<int> first_runs{0};
        std::atomic<int> second_runs{0};
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        sycl::queue& queue = master_context.devices().queue(0);
        USMFloats in(queue, K_COUNT, 0.0f);
        USMFloats first_out(queue, K_COUNT, K_UNWRITTEN);
        USMFloats second_out(queue, K_COUNT, K_UNWRITTEN);
        in.ramp(1.0f);

        InProcessFabric fabric;
        LocalWorker first(InProcessTransport::make_worker(fabric), vec_add_kernels(first_runs));
        LocalWorker second(InProcessTransport::make_worker(fabric), vec_add_kernels(second_runs));
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          eager_options()).value();
        ASSERT_TRUE(first.start());
        ASSERT_TRUE(second.start());
        ASSERT_TRUE(cluster.wait_for_workers(2, K_WAIT));
        EXPECT_EQ(cluster.alive_workers(), 2u);

        share(cluster, in);
        share(cluster, first_out);
        share(cluster, second_out);

        auto graph = master_runtime.graph();
        add_vec_add(graph, "fan_out_first", in, in, first_out);
        add_vec_add(graph, "fan_out_second", in, in, second_out);
        graph.run().value();

        EXPECT_EQ(cluster.offloaded(), 2u);
        EXPECT_EQ(first_runs.load() + second_runs.load(), 2);
        expect_sum(first_out, in, in);
        expect_sum(second_out, in, in);

        first.stop();
        second.stop();
    }

    /** @brief Tests that a local write raises a shared buffer's version and the worker reads it. */
    TEST(Medium_DistributedE2ETest, AutoVersionBumpAcrossLocalWriteThenOffloadedRead)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        constexpr std::size_t K_COUNT = 512;
        std::atomic<int> worker_runs{0};
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        sycl::queue& queue = master_context.devices().queue(0);
        USMFloats x(queue, K_COUNT, -7.0f);
        USMFloats y(queue, K_COUNT, K_UNWRITTEN);

        InProcessFabric fabric;
        LocalWorker worker(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          eager_options()).value();
        ASSERT_TRUE(worker.start());
        ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

        share(cluster, x);
        share(cluster, y);
        const std::optional<BufferHandle> before = cluster.buffer_handle(x.data());
        ASSERT_TRUE(before.has_value());

        auto graph = master_runtime.graph();
        NodeOptions fill_options;
        fill_options.name = "local_fill";
        float* values = x.data();
        graph.add_task(
            Reads(), Writes(BufferRegion(values)),
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
            }, fill_options);
        add_vec_add(graph, "double_on_worker", x, x, y);

        graph.run().value();

        const std::optional<BufferHandle> after = cluster.buffer_handle(x.data());
        ASSERT_TRUE(after.has_value());
        EXPECT_GT(after->version, before->version);
        EXPECT_EQ(worker_runs.load(), 1);
        expect_elements(y.data(), y.count(),
                        [](std::size_t index) { return 2.0f * static_cast<float>(index + 1); });

        worker.stop();
    }

    /** @brief Tests the vector-add offload over TCP on the loopback address. */
    TEST(Medium_DistributedE2ETest, OffloadOverTCPLoopback)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        constexpr std::size_t K_COUNT = 1024;
        std::atomic<int> worker_runs{0};
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        sycl::queue& queue = master_context.devices().queue(0);
        USMFloats a(queue, K_COUNT, 0.0f);
        USMFloats b(queue, K_COUNT, 0.0f);
        USMFloats out(queue, K_COUNT, K_UNWRITTEN);
        a.ramp(1.0f);
        b.ramp(3.0f);

        // The default options bind 127.0.0.1 on a port the OS picks, known once attach() returns.
        const std::shared_ptr<TCPTransport> master_transport =
            TCPTransport::make_master(TCPMasterOptions());
        Cluster cluster = Cluster::attach(master_runtime, master_transport, eager_options()).value();
        const Endpoint listening = master_transport->local_endpoint();
        ASSERT_NE(listening.port, 0);

        LocalWorker worker(TCPTransport::make_worker(listening), vec_add_kernels(worker_runs));
        ASSERT_TRUE(worker.start());
        ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

        share(cluster, a);
        share(cluster, b);
        share(cluster, out);

        auto graph = master_runtime.graph();
        add_vec_add(graph, "vec_add_tcp", a, b, out);
        graph.run().value();

        EXPECT_EQ(worker_runs.load(), 1);
        expect_sum(out, a, b);
        const ClusterStatistics statistics = cluster.statistics();
        EXPECT_GT(statistics.bytes_sent, 2 * a.size_bytes());
        EXPECT_GT(statistics.bytes_received, out.size_bytes());

        worker.stop();
    }

    /** @brief Tests that a task whose worker falls silent is taken back and finishes locally. */
    TEST(Medium_DistributedE2ETest, WorkerDeathFallsBackToLocal)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        constexpr std::size_t K_COUNT = 1024;
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
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
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          std::move(options)).value();
        ASSERT_TRUE(worker.start());
        ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

        share(cluster, a);
        share(cluster, b);
        share(cluster, out);

        auto graph = master_runtime.graph();
        add_vec_add(graph, "vec_add_death", a, b, out);
        ASSERT_TRUE(graph.compile());
        BackgroundRun run(graph);
        ASSERT_TRUE(run.wait()) << "execute() did not return after the worker fell silent";

        EXPECT_GE(worker.dispatch_count(), 1u) << "the task never reached the worker";
        EXPECT_GE(cluster.redispatched(), 1u);
        expect_sum(out, a, b);
    }

    /** @brief Tests that the output fence refuses a chunk of an old version and takes a current one. */
    TEST(Medium_DistributedReliabilityTest, StaleWritebackIsFenced)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        constexpr std::size_t K_COUNT = 16;
        constexpr float K_AUTHORITATIVE = 7.0f;
        constexpr float K_POISON = 999.0f;
        constexpr float K_FRESH = 42.0f;
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        sycl::queue& queue = master_context.devices().queue(0);
        USMFloats a(queue, K_COUNT, 1.0f);
        USMFloats b(queue, K_COUNT, 2.0f);
        USMFloats out(queue, K_COUNT, K_AUTHORITATIVE);

        InProcessFabric fabric;
        ScriptedWorker worker(fabric, K_VEC_ADD, DispatchAnswer::Accept);
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          eager_options()).value();
        ASSERT_TRUE(worker.start());
        ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

        share(cluster, a);
        share(cluster, b);
        share(cluster, out);

        auto graph = master_runtime.graph();
        add_vec_add(graph, "fenced", a, b, out);
        ASSERT_TRUE(graph.compile());
        BackgroundRun run(graph);
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
    TEST(Medium_DistributedReliabilityTest, LostBufferPushTimesOutInsteadOfHanging)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        constexpr std::size_t K_COUNT = 64;
        std::atomic<int> worker_runs{0};
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
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
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          eager_options()).value();
        ASSERT_TRUE(worker.start());
        ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

        share(cluster, a);
        share(cluster, b);
        share(cluster, out);

        auto graph = master_runtime.graph();
        add_vec_add(graph, "vec_add_lost_chunks", a, b, out);
        ASSERT_TRUE(graph.compile());
        BackgroundRun run(graph);
        ASSERT_TRUE(run.wait()) << "execute() did not return although the worker's pull timed out";

        EXPECT_EQ(worker_runs.load(), 0) << "the kernel ran without its inputs";
        EXPECT_GE(worker.statistics().tasks_failed_input_unavailable, 1u);
        EXPECT_GE(cluster.redispatched(), 1u);
        expect_sum(out, a, b);

        worker.stop();
    }

    /** @brief Tests that the fluent surface builds and runs an offloaded task. */
    TEST(Medium_DistributedE2ETest, FluentClusterOffload)
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

        auto a = cluster.share(runtime.buffer<float>(K_COUNT).value());
        auto b = cluster.share(runtime.buffer<float>(K_COUNT).value());
        auto out = cluster.share(runtime.buffer<float>(K_COUNT).value());
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
    TEST(Medium_DistributedE2ETest, FluentDistributedStateParity)
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
    TEST(Medium_DistributedE2ETest, ClusterDetachAndDisplacement)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        EXPECT_EQ(master_context.offload(), nullptr);

        const auto attach_master = [&master_runtime](InProcessFabric& fabric)
        {
            return Cluster::attach(master_runtime, InProcessTransport::make_master(fabric)).value();
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
    TEST(Medium_DistributedE2ETest, ClusterHandleOutlivesRuntime)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        InProcessFabric fabric;
        std::optional<Runtime> master_runtime(Runtime::create().value());
        Cluster cluster =
            Cluster::attach(*master_runtime, InProcessTransport::make_master(fabric)).value();
        const Cluster copy = cluster;

        master_runtime.reset();

        cluster.detach();
        EXPECT_EQ(copy.alive_workers(), 0u);
        EXPECT_EQ(copy.offloaded(), 0u);
    }

    /** @brief Tests that stopping the master returns a task still out on a worker to the scheduler. */
    TEST(Medium_DistributedE2ETest, StopResubmitsPendingNodes)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        constexpr std::size_t K_COUNT = 128;
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        sycl::queue& queue = master_context.devices().queue(0);
        USMFloats a(queue, K_COUNT, 0.0f);
        USMFloats b(queue, K_COUNT, 0.0f);
        USMFloats out(queue, K_COUNT, K_UNWRITTEN);
        a.ramp(1.0f);
        b.ramp(2.0f);

        InProcessFabric fabric;
        ScriptedWorker worker(fabric, K_VEC_ADD, DispatchAnswer::Accept);
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          eager_options()).value();
        ASSERT_TRUE(worker.start());
        ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

        share(cluster, a);
        share(cluster, b);
        share(cluster, out);

        auto graph = master_runtime.graph();
        add_vec_add(graph, "held_by_worker", a, b, out);
        ASSERT_TRUE(graph.compile());
        BackgroundRun run(graph);
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
    TEST(Medium_DistributedE2ETest, TryOffloadAfterStopRunsLocally)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        constexpr std::size_t K_COUNT = 128;
        std::atomic<int> worker_runs{0};
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        sycl::queue& queue = master_context.devices().queue(0);
        USMFloats a(queue, K_COUNT, 0.0f);
        USMFloats b(queue, K_COUNT, 0.0f);
        USMFloats out(queue, K_COUNT, K_UNWRITTEN);
        a.ramp(1.0f);
        b.ramp(2.0f);

        InProcessFabric fabric;
        LocalWorker worker(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          eager_options()).value();
        ASSERT_TRUE(worker.start());
        ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

        share(cluster, a);
        share(cluster, b);
        share(cluster, out);

        auto graph = master_runtime.graph();
        add_vec_add(graph, "after_stop", a, b, out);
        // The plan is built while the master runs, so its node is one the scheduler offers.
        ASSERT_TRUE(graph.compile());

        master_context.offload()->stop();

        BackgroundRun run(graph);
        ASSERT_TRUE(run.wait());

        EXPECT_EQ(worker_runs.load(), 0);
        EXPECT_EQ(cluster.offloaded(), 0u);
        EXPECT_GE(cluster.declined(), 1u);
        expect_sum(out, a, b);

        worker.stop();
    }

    /** @brief Tests that each local step of a State bumps the buffer that step wrote. */
    TEST(Medium_DistributedE2ETest, DynamicStateBumpsTheLiveBufferAfterSwap)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        constexpr std::size_t K_COUNT = 64;
        constexpr std::size_t K_STEPS = 2;
        InProcessFabric fabric;
        Runtime runtime = Runtime::create().value();
        Cluster cluster = Cluster::attach(runtime, InProcessTransport::make_master(fabric)).value();

        auto field = cluster.share(runtime.state<int>(K_COUNT).value());
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
    TEST(Medium_DistributedE2ETest, AttachStartFailureLeavesNothingInstalled)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        const std::string refusal = "cannot listen on 127.0.0.1:9: the stub refuses";
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        const auto transport = std::make_shared<StubTransport>(
            TransportExposure::Loopback, TransportResult{TransportStatus::BindFailed, refusal});

        const Result<Cluster> attached = Cluster::attach(master_runtime, transport);
        ASSERT_FALSE(attached.has_value())
            << "attach() returned a cluster although the transport did not start";
        const std::string message = attached.error().message;

        EXPECT_NE(message.find(refusal), std::string::npos) << "the error says: " << message;
        EXPECT_EQ(transport->start_calls(), 1);
        EXPECT_EQ(master_context.offload(), nullptr);

        InProcessFabric fabric;
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric)).value();
        EXPECT_NE(master_context.offload(), nullptr) << "the context takes no master after a failure";
        cluster.detach();
    }

    /** @brief Tests that the attempt cap of one run does not keep a node local in the next run. */
    TEST(Medium_DistributedE2ETest, AttemptCapDoesNotOutliveTheRun)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        constexpr std::size_t K_COUNT = 128;
        constexpr std::size_t K_RUNS = 2;
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        sycl::queue& queue = master_context.devices().queue(0);
        USMFloats a(queue, K_COUNT, 0.0f);
        USMFloats b(queue, K_COUNT, 0.0f);
        USMFloats out(queue, K_COUNT, K_UNWRITTEN);
        a.ramp(1.0f);
        b.ramp(2.0f);

        InProcessFabric fabric;
        ScriptedWorker worker(fabric, K_VEC_ADD, DispatchAnswer::Fail);
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          eager_options()).value();
        ASSERT_TRUE(worker.start());
        ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));

        share(cluster, a);
        share(cluster, b);
        share(cluster, out);

        auto graph = master_runtime.graph();
        add_vec_add(graph, "always_fails_remotely", a, b, out);
        ASSERT_TRUE(graph.compile());

        // Every dispatch fails on the worker, so each run spends the cap and then runs locally.
        for (std::size_t round = 1; round <= K_RUNS; ++round)
        {
            SCOPED_TRACE(round);
            out.fill(K_UNWRITTEN);
            BackgroundRun run(graph);
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
    TEST(Medium_DistributedE2ETest, RefusesEmptySecretOnNetworkExposure)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        const auto exposed =
            std::make_shared<StubTransport>(TransportExposure::Network, TransportResult());

        EXPECT_FALSE(Cluster::attach(master_runtime, exposed).has_value());
        EXPECT_EQ(exposed->start_calls(), 0) << "the transport was started before the refusal";
        EXPECT_EQ(master_context.offload(), nullptr);

        const std::vector<std::uint8_t> key(SharedSecret::K_MINIMUM_BYTES, 0x42);
        ClusterOptions options;
        options.secret =
            SharedSecret::from_bytes(span<const std::uint8_t>(key.data(), key.size())).value();
        Cluster cluster = Cluster::attach(master_runtime, exposed, std::move(options)).value();
        EXPECT_EQ(exposed->start_calls(), 1);
        EXPECT_NE(master_context.offload(), nullptr);
        cluster.detach();
    }

    /** @brief Tests that a task added with no lambda gives one result with a worker and with none. */
    TEST(Medium_DistributedE2ETest, RegisteredTaskRunsWithAndWithoutAWorker)
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
    TEST(Medium_DistributedE2ETest, AttachOnAStartedTransportLeavesTheFirstMasterServing)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        std::atomic<int> worker_runs{0};
        Runtime first_runtime = Runtime::create().value();
        RuntimeContext& first_context = SushiRuntime::Internal::ContextAccess::context(first_runtime);
        Runtime second_runtime = Runtime::create().value();
        RuntimeContext& second_context = SushiRuntime::Internal::ContextAccess::context(second_runtime);
        InProcessFabric fabric;
        const std::shared_ptr<InProcessTransport> transport = InProcessTransport::make_master(fabric);
        Cluster first = Cluster::attach(first_runtime, transport).value();
        const OffloadCoordinator* const installed = first_context.offload();

        EXPECT_FALSE(Cluster::attach(second_runtime, transport).has_value());
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
    TEST(Medium_DistributedE2ETest, DetachedClusterReportsNoWorkers)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        std::atomic<int> worker_runs{0};
        Runtime master_runtime = Runtime::create().value();
        InProcessFabric fabric;
        LocalWorker first(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
        LocalWorker second(InProcessTransport::make_worker(fabric), vec_add_kernels(worker_runs));
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric)).value();
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
    TEST(Medium_DistributedE2ETest, AttachIsRefusedAndDetachDeferredDuringARun)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        constexpr std::size_t K_COUNT = 128;
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        sycl::queue& queue = master_context.devices().queue(0);
        USMFloats a(queue, K_COUNT, 0.0f);
        USMFloats b(queue, K_COUNT, 0.0f);
        USMFloats out(queue, K_COUNT, K_UNWRITTEN);
        a.ramp(1.0f);
        b.ramp(2.0f);

        InProcessFabric fabric;
        ScriptedWorker worker(fabric, K_VEC_ADD, DispatchAnswer::Accept);
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          eager_options()).value();
        ASSERT_TRUE(worker.start());
        ASSERT_TRUE(cluster.wait_for_workers(1, K_WAIT));
        const OffloadCoordinator* const installed = master_context.offload();

        share(cluster, a);
        share(cluster, b);
        share(cluster, out);

        auto graph = master_runtime.graph();
        add_vec_add(graph, "held_during_attach", a, b, out);
        ASSERT_TRUE(graph.compile());
        BackgroundRun run(graph);
        ASSERT_TRUE(worker.wait_for_dispatches(1));

        const auto second_transport =
            std::make_shared<StubTransport>(TransportExposure::Loopback, TransportResult());
        const Result<Cluster> second = Cluster::attach(master_runtime, second_transport);
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
    TEST(Medium_DistributedE2ETest, MovedFromHandleStillNamesTheMaster)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        InProcessFabric fabric;
        Cluster source = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric)).value();
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
    TEST(Medium_DistributedE2ETest, RefusesATimeoutBelowOneMillisecond)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context = SushiRuntime::Internal::ContextAccess::context(master_runtime);
        const auto refusal_of = [&master_runtime](ClusterOptions options)
        {
            const auto transport =
                std::make_shared<StubTransport>(TransportExposure::Loopback, TransportResult());
            const Result<Cluster> attached =
                Cluster::attach(master_runtime, transport, std::move(options));
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
            master_runtime,
            std::make_shared<StubTransport>(TransportExposure::Loopback, TransportResult()),
            std::move(without_deadline)).value();

        const Result<void> refused = cluster.liveness_timeout(std::chrono::milliseconds(0));
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, ErrorCode::InvalidState);
        EXPECT_TRUE(cluster.liveness_timeout(K_MINIMUM_TIMEOUT).has_value());
        cluster.task_deadline(std::chrono::milliseconds(0));
        cluster.detach();
    }

    /** @brief Tests that Cluster::share stops on a device-resident Buffer and on such a State. */
    TEST(Medium_DistributedE2EDeathTest, ShareStopsOnADeviceResidentHandle)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        InProcessFabric fabric;
        Runtime runtime = Runtime::create().value();
        Cluster cluster = Cluster::attach(runtime, InProcessTransport::make_master(fabric)).value();
        auto buffer = runtime.buffer<float>(8, DeviceIndex{0}, Residency::Device).value();
        auto field = runtime.state<float>(8, DeviceIndex{0}, Residency::Device).value();

        EXPECT_DEATH(static_cast<void>(cluster.share(buffer)),
                     "Cluster::share.buffer.*the handle has Device residency");
        EXPECT_DEATH(static_cast<void>(cluster.share(std::move(buffer))),
                     "Cluster::share.buffer.*the handle has Device residency");
        EXPECT_DEATH(static_cast<void>(cluster.share(field)),
                     "Cluster::share.state.*the handle has Device residency");
        cluster.detach();
    }

    /** @brief Tests that Cluster::share registers a host-addressable handle and skips an empty one. */
    TEST(Medium_DistributedE2ETest, ShareRegistersAHostAccessibleHandle)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        InProcessFabric fabric;
        Runtime runtime = Runtime::create().value();
        Cluster cluster = Cluster::attach(runtime, InProcessTransport::make_master(fabric)).value();
        auto buffer = runtime.buffer<float>(8).value();
        auto field = runtime.state<float>(8).value();
        auto moved_from = runtime.buffer<float>(8).value();
        auto taken = std::move(moved_from);
        ASSERT_EQ(taken.size(), 8u);

        EXPECT_EQ(&cluster.share(buffer), &buffer);
        EXPECT_EQ(&cluster.share(field), &field);
        cluster.share(moved_from);
        auto pinned = cluster.share(runtime.buffer<float>(8, DeviceIndex{0}, Residency::Host).value());

        EXPECT_TRUE(cluster.buffer_handle(buffer.data()).has_value());
        EXPECT_TRUE(cluster.buffer_handle(field.current()).has_value());
        EXPECT_TRUE(cluster.buffer_handle(field.next()).has_value());
        EXPECT_TRUE(cluster.buffer_handle(pinned.data()).has_value());
        EXPECT_FALSE(cluster.buffer_handle(taken.data()).has_value())
            << "a buffer nobody shared has a handle";
        cluster.detach();
    }

    /** @brief Tests that a worker agent seals the registry it is built with, before start(). */
    TEST(Medium_DistributedE2EDeathTest, WorkerAgentSealsItsRegistry)
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
} // namespace EndToEndTests

/** @brief Tests that an offload graph under seeded message faults ends equal to a clean run. */
namespace FaultInjectionTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Graph;
    using namespace SushiRuntime::Literals;
    using namespace SushiRuntime::Distributed;

    namespace
    {
        /** @brief Names the doubling operation: out[i] = in[i] * 2. */
        constexpr auto K_VEC_DOUBLE = "vec.double"_op;

        /** @brief Holds the elements of each buffer. */
        constexpr std::size_t K_COUNT = 128;

        /** @brief Holds how long a test waits for a cluster to assemble. */
        constexpr std::chrono::seconds K_WAIT{10};

        /** @brief Holds how long a run under faults may take before it counts as hung. */
        constexpr std::chrono::seconds K_RUN_LIMIT{30};

        /** @brief Holds the tolerance of every float comparison; see README.md. */
        constexpr float K_TOLERANCE = 0.0f;

        /** @brief Holds the value an output holds before any task wrote it. */
        constexpr float K_UNWRITTEN = -1.0f;

        /** @brief Holds the wait between two flushes of the frames the fault policy parked. */
        constexpr std::chrono::milliseconds K_FLUSH_PERIOD{25};

        /** @brief Holds how long the nemesis keeps one worker's link cut. */
        constexpr std::chrono::milliseconds K_PARTITION_LENGTH{60};

        /** @brief Holds the value mixed into a seed for the nemesis's own random source. */
        constexpr std::uint64_t K_NEMESIS_SALT = 0x9e3779b97f4a7c15ull;

        /** @brief Returns the value in[i] holds: i + 1. */
        constexpr float input_value(std::size_t index) noexcept
        {
            return static_cast<float>(index) + 1.0f;
        }

        /**
         * @brief Owns one shared USM allocation of K_COUNT floats on a queue.
         */
        class USMFloats final
        {
            public:
                /**
                 * @brief Allocates the floats and sets each to @p value.
                 * @throws std::runtime_error When the device gives no memory.
                 */
                USMFloats(sycl::queue& queue, float value)
                    : queue_(queue), data_(sycl::malloc_shared<float>(K_COUNT, queue))
                {
                    if (data_ == nullptr)
                        throw std::runtime_error("sycl::malloc_shared returned null for a test buffer");
                    std::fill_n(data_, K_COUNT, value);
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

                /** @brief Returns the base address. */
                [[nodiscard]] float* data() const noexcept
                {
                    return data_;
                }

            private:
                /// @brief The queue the allocation belongs to.
                sycl::queue& queue_;

                /// @brief The base address.
                float* data_;
        };

        /** @brief Returns an input buffer whose element i holds input_value(i). */
        std::unique_ptr<USMFloats> make_input(sycl::queue& queue)
        {
            auto input = std::make_unique<USMFloats>(queue, 0.0f);
            for (std::size_t index = 0; index < K_COUNT; ++index)
                input->data()[index] = input_value(index);
            return input;
        }

        /** @brief Returns the index of the first element that is not twice its input, or K_COUNT. */
        std::size_t first_wrong_element(const USMFloats& output)
        {
            for (std::size_t index = 0; index < K_COUNT; ++index)
                if (std::abs(output.data()[index] - 2.0f * input_value(index)) > K_TOLERANCE)
                    return index;
            return K_COUNT;
        }

        /**
         * @brief Returns a registry that holds the doubling kernel.
         * @param runs Counts the kernel's runs; outlives the registry.
         */
        sushi_ptr<KernelRegistry> vec_double_kernels(std::atomic<int>& runs)
        {
            sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
            kernels->register_kernel(
                K_VEC_DOUBLE,
                [&runs](sycl::queue& queue, span<const BufferView> inputs,
                        span<const BufferView> outputs, const NodeOptions&) -> sycl::event
                {
                    const float* in = inputs[0].as<const float>();
                    float* out = outputs[0].as<float>();
                    const std::size_t count = outputs[0].size_bytes / sizeof(float);
                    std::atomic<int>* counter = &runs;

                    return queue.submit([&](sycl::handler& handler)
                    {
                        SushiRuntime::Detail::enqueue_host_task(handler, [=]
                        {
                            for (std::size_t index = 0; index < count; ++index)
                                out[index] = in[index] * 2.0f;
                            counter->fetch_add(1, std::memory_order_relaxed);
                        });
                    });
                });
            return kernels;
        }

        /** @brief Adds an op-tagged task out = in * 2 whose local body computes the same. */
        void add_vec_double(Graph& graph, const USMFloats& input, const USMFloats& output)
        {
            NodeOptions options;
            options.name = "double";
            options.operation = K_VEC_DOUBLE;

            float* in = input.data();
            float* out = output.data();
            graph.add_task(
                Reads(BufferRegion(in)), Writes(BufferRegion(out)),
                [in, out](sycl::queue& queue, const std::vector<sycl::event>&) -> sycl::event
                {
                    return queue.submit([&](sycl::handler& handler)
                    {
                        SushiRuntime::Detail::enqueue_host_task(handler, [=]
                        {
                            for (std::size_t index = 0; index < K_COUNT; ++index)
                                out[index] = in[index] * 2.0f;
                        });
                    });
                }, options);
        }

        /**
         * @brief Runs a graph once on its own thread, so a run that never returns is
         *        reported and not waited for.
         */
        class BackgroundRun final
        {
            public:
                /** @brief Starts the run; @p graph outlives this object. */
                explicit BackgroundRun(Graph& graph)
                {
                    auto done = std::make_shared<std::promise<void>>();
                    finished_ = done->get_future();
                    thread_ = std::thread([&graph, done]
                    {
                        graph.run().value();
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

                /** @brief Returns whether the run returned within @p limit, and joins it if so. */
                [[nodiscard]] bool wait(std::chrono::milliseconds limit)
                {
                    if (finished_.wait_for(limit) != std::future_status::ready)
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
         * @brief Runs one real worker: its own runtime and the agent that serves a fabric.
         */
        class LocalWorker final
        {
            public:
                /** @brief Builds the worker on the next worker endpoint of @p fabric. */
                LocalWorker(InProcessFabric& fabric, std::atomic<int>& runs,
                            WorkerAgentOptions options)
                    : agent_(runtime_, InProcessTransport::make_worker(fabric),
                             vec_double_kernels(runs), std::move(options))
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

            private:
                /// @brief The worker's own runtime; declared first, destroyed last.
                Runtime runtime_ = Runtime::create().value();

                /// @brief The agent under test.
                WorkerAgent agent_;
        };

        /** @brief Returns worker options with a short HELLO retry and a short pull timeout. */
        WorkerAgentOptions impatient_worker()
        {
            WorkerAgentOptions options;
            options.heartbeat_interval = std::chrono::milliseconds(40);
            options.pull_idle_timeout = std::chrono::milliseconds(120);
            return options;
        }

        /** @brief Returns master options that offload any task and give up on a worker quickly. */
        ClusterOptions impatient_master()
        {
            ClusterOptions options;
            options.policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
            options.liveness_timeout = std::chrono::milliseconds(250);
            options.task_deadline = std::chrono::milliseconds(300);
            return options;
        }

        /**
         * @brief Returns a policy that drops 8 frames in 100, repeats 4 and parks 8, by @p seed.
         *
         * The fabric calls a policy from several threads at once, so the policy locks its
         * random source.
         */
        FaultPolicy seeded_faults(std::uint64_t seed)
        {
            /** @brief Holds the random source of one policy and the mutex that guards it. */
            struct Source
            {
                /** @brief Guards random. */
                std::mutex mutex;

                /** @brief Draws the fate of each frame. */
                std::mt19937_64 random;
            };

            auto source = std::make_shared<Source>();
            source->random.seed(seed);
            return [source](const FaultedFrame&)
            {
                const std::lock_guard<std::mutex> lock(source->mutex);
                const std::uint64_t draw = source->random() % 100;
                if (draw < 8)
                    return FaultAction::Drop;
                if (draw < 12)
                    return FaultAction::Duplicate;
                if (draw < 20)
                    return FaultAction::Hold;
                return FaultAction::Deliver;
            };
        }

        /**
         * @brief Returns a policy that drops the first frame of each given type and delivers
         *        every other frame.
         * @param dropped Counts the frames the policy dropped.
         */
        FaultPolicy drop_first_of(const std::vector<MessageType>& types,
                                  std::shared_ptr<std::atomic<int>> dropped)
        {
            auto is_owed = std::make_shared<std::array<std::atomic<bool>, K_MESSAGE_TYPE_COUNT + 1>>();
            for (std::atomic<bool>& flag : *is_owed)
                flag.store(false);
            for (const MessageType type : types)
                (*is_owed)[static_cast<std::size_t>(type)].store(true);

            return [is_owed, dropped = std::move(dropped)](const FaultedFrame& frame)
            {
                if (!(*is_owed)[static_cast<std::size_t>(frame.type)].exchange(false))
                    return FaultAction::Deliver;

                dropped->fetch_add(1, std::memory_order_relaxed);
                return FaultAction::Drop;
            };
        }

        /**
         * @brief Puts a fabric under a seeded fault policy and, on its own thread, flushes
         *        parked frames in a shuffled order and cuts one worker's link now and then.
         */
        class Nemesis final
        {
            public:
                /** @brief Installs the policy and starts the thread. */
                Nemesis(InProcessFabric& fabric, std::uint64_t seed, std::size_t worker_count)
                    : fabric_(fabric), worker_count_(worker_count)
                {
                    fabric_.set_fault_policy(seeded_faults(seed));
                    thread_ = std::thread(&Nemesis::run, this, seed ^ K_NEMESIS_SALT);
                }

                /** @brief Heals the fabric if heal() was not called. */
                ~Nemesis()
                {
                    heal();
                }

                Nemesis(const Nemesis&) = delete;
                Nemesis& operator=(const Nemesis&) = delete;
                Nemesis(Nemesis&&) = delete;
                Nemesis& operator=(Nemesis&&) = delete;

                /** @brief Joins the thread, restores every link, removes the policy and flushes. */
                void heal()
                {
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        stopping_ = true;
                    }
                    changed_.notify_all();
                    if (thread_.joinable())
                        thread_.join();

                    for (std::size_t worker = 0; worker < worker_count_; ++worker)
                        fabric_.set_partitioned(worker, false);
                    fabric_.set_fault_policy(FaultPolicy());
                    fabric_.flush_held();
                }

            private:
                /** @brief Waits for @p length and returns whether the nemesis should keep running. */
                [[nodiscard]] bool pause(std::chrono::milliseconds length)
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    return !changed_.wait_for(lock, length, [&] { return stopping_; });
                }

                /** @brief Flushes and partitions until heal() asks the thread to end. */
                void run(std::uint64_t seed)
                {
                    std::mt19937_64 random(seed);
                    while (pause(K_FLUSH_PERIOD))
                    {
                        fabric_.flush_held(random());
                        if ((random() & 7u) != 0)
                            continue;

                        const std::size_t worker = random() % worker_count_;
                        fabric_.set_partitioned(worker, true);
                        const bool keeps_running = pause(K_PARTITION_LENGTH);
                        fabric_.set_partitioned(worker, false);
                        if (!keeps_running)
                            return;
                    }
                }

                /// @brief The fabric under faults.
                InProcessFabric& fabric_;

                /// @brief The number of worker endpoints the nemesis may cut.
                std::size_t worker_count_;

                /// @brief Guards stopping_.
                std::mutex mutex_;

                /// @brief Wakes a pause when heal() is called.
                std::condition_variable changed_;

                /// @brief Set by heal().
                bool stopping_ = false;

                /// @brief The thread that flushes and partitions.
                std::thread thread_;
        };

        /**
         * @brief Runs four independent offloadable tasks on two workers under the faults of one
         *        seed.
         * @return An empty string when the run returned and every output is right, else the reason.
         */
        std::string run_one_scenario(std::uint64_t seed)
        {
            constexpr std::size_t K_WORKERS = 2;
            constexpr std::size_t K_TASKS = 4;

            std::atomic<int> worker_runs{0};
            Runtime master_runtime = Runtime::create().value();
            RuntimeContext& master_context =
                SushiRuntime::Internal::ContextAccess::context(master_runtime);
            sycl::queue& queue = master_context.devices().queue(0);
            const std::unique_ptr<USMFloats> input = make_input(queue);
            std::vector<std::unique_ptr<USMFloats>> outputs;
            for (std::size_t task = 0; task < K_TASKS; ++task)
                outputs.push_back(std::make_unique<USMFloats>(queue, K_UNWRITTEN));

            InProcessFabric fabric;
            LocalWorker first(fabric, worker_runs, impatient_worker());
            LocalWorker second(fabric, worker_runs, impatient_worker());
            Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                              impatient_master()).value();

            // The cluster assembles before any fault is injected; see README.md.
            if (!first.start() || !second.start() || !cluster.wait_for_workers(K_WORKERS, K_WAIT))
                return "the cluster did not assemble";

            OffloadCoordinator* master = master_context.offload();
            cluster.share(input->data(), K_COUNT * sizeof(float));
            auto graph = master_runtime.graph();
            for (const std::unique_ptr<USMFloats>& output : outputs)
            {
                cluster.share(output->data(), K_COUNT * sizeof(float));
                add_vec_double(graph, *input, *output);
            }
            // The plan is built before the first fault, while both workers are known.
            graph.compile().value();

            Nemesis nemesis(fabric, seed, K_WORKERS);
            BackgroundRun run(graph);
            const bool returned = run.wait(K_RUN_LIMIT);
            nemesis.heal();

            // Stopping the master returns every node it holds, so a hung run can still end.
            if (!returned)
                master->stop();
            const bool ended = returned || run.wait(K_WAIT);

            // No worker and no master thread writes an output once all three have stopped.
            first.stop();
            second.stop();
            cluster.detach();

            if (!returned)
                return ended ? "execute() did not return under faults until the master was stopped"
                             : "execute() did not return under faults, nor after the master stopped";

            for (std::size_t task = 0; task < K_TASKS; ++task)
            {
                const std::size_t wrong = first_wrong_element(*outputs[task]);
                if (wrong != K_COUNT)
                    return "output " + std::to_string(task) + " element " + std::to_string(wrong) +
                           " is " + std::to_string(outputs[task]->data()[wrong]);
            }
            return std::string();
        }

        /** @brief Holds what one run on a one-worker cluster with a lossy handshake left behind. */
        struct LossyRegistration
        {
            bool assembled = false;       /**< The master counted the worker as registered. */
            bool returned = false;        /**< execute() returned within the limit. */
            int frames_dropped = 0;       /**< Frames the fault policy dropped. */
            int worker_runs = 0;          /**< Runs of the worker's kernel. */
            std::size_t wrong = K_COUNT;  /**< The first wrong output element, or K_COUNT. */
        };

        /**
         * @brief Registers one worker while the first frame of each given type is lost, then
         *        runs one offloadable task.
         */
        LossyRegistration register_under_loss(const std::vector<MessageType>& lost_types)
        {
            LossyRegistration outcome;
            std::atomic<int> worker_runs{0};
            const auto dropped = std::make_shared<std::atomic<int>>(0);

            Runtime master_runtime = Runtime::create().value();
            RuntimeContext& master_context =
                SushiRuntime::Internal::ContextAccess::context(master_runtime);
            sycl::queue& queue = master_context.devices().queue(0);
            const std::unique_ptr<USMFloats> input = make_input(queue);
            USMFloats output(queue, K_UNWRITTEN);

            InProcessFabric fabric;
            fabric.set_fault_policy(drop_first_of(lost_types, dropped));

            LocalWorker worker(fabric, worker_runs, impatient_worker());
            Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                              impatient_master()).value();
            outcome.assembled = worker.start() && cluster.wait_for_workers(1, K_WAIT);
            if (!outcome.assembled)
            {
                outcome.frames_dropped = dropped->load(std::memory_order_relaxed);
                return outcome;
            }

            OffloadCoordinator* master = master_context.offload();
            cluster.share(input->data(), K_COUNT * sizeof(float));
            cluster.share(output.data(), K_COUNT * sizeof(float));
            auto graph = master_runtime.graph();
            add_vec_double(graph, *input, output);
            graph.compile().value();

            BackgroundRun run(graph);
            outcome.returned = run.wait(K_RUN_LIMIT);
            if (!outcome.returned)
                master->stop();

            worker.stop();
            cluster.detach();

            // The master counts the worker before its WELCOME leaves, so the drop is read last.
            outcome.frames_dropped = dropped->load(std::memory_order_relaxed);
            outcome.worker_runs = worker_runs.load(std::memory_order_relaxed);
            if (outcome.returned)
                outcome.wrong = first_wrong_element(output);
            return outcome;
        }
    } // namespace

    /** @brief Tests that eight seeded fault schedules each end with the outputs of a clean run. */
    TEST(Medium_DistributedJepsenTest, DeterminismUnderRandomFaults)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        for (std::uint64_t seed = 1; seed <= 8; ++seed)
        {
            const std::string failure = run_one_scenario(seed);
            EXPECT_TRUE(failure.empty()) << "seed " << seed << ": " << failure;
        }
    }

    /** @brief Tests that a worker registers although HELLO, CHALLENGE and PROOF are each lost once. */
    TEST(Medium_DistributedJepsenTest, RegistrationSurvivesMessageLoss)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        const LossyRegistration outcome = register_under_loss(
            {MessageType::Hello, MessageType::Challenge, MessageType::Proof});

        ASSERT_TRUE(outcome.assembled) << "the worker never registered after the lost frames";
        EXPECT_EQ(outcome.frames_dropped, 3) << "the handshake did not meet each of the three losses";
        ASSERT_TRUE(outcome.returned) << "execute() did not return after a lossy registration";
        EXPECT_EQ(outcome.worker_runs, 1) << "the registered worker did not run the task";
        EXPECT_EQ(outcome.wrong, K_COUNT) << "output element " << outcome.wrong << " is wrong";
    }

    /** @brief Tests that a worker whose WELCOME is lost registers again and runs the task. */
    TEST(Medium_DistributedJepsenTest, RegistrationSurvivesALostWelcome)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        const LossyRegistration outcome = register_under_loss({MessageType::Welcome});

        ASSERT_TRUE(outcome.assembled);
        EXPECT_EQ(outcome.frames_dropped, 1);
        ASSERT_TRUE(outcome.returned) << "execute() did not return after the lost WELCOME";
        EXPECT_EQ(outcome.wrong, K_COUNT) << "output element " << outcome.wrong << " is wrong";
        EXPECT_EQ(outcome.worker_runs, 1)
            << "the master counts a worker that never learned it was registered";
    }
} // namespace FaultInjectionTests

/** @brief Tests a master and a worker over loopback TCP while the link between them is faulted. */
namespace TCPFaultsTests
{
    using namespace SushiRuntime::Distributed;
    using namespace SushiRuntime::Literals;

    namespace
    {
        /** @brief Names the proxy's fault vocabulary without its namespace. */
        using Distributed::Test::Fault;

        /** @brief Names the proxy's two directions without their namespace. */
        using Distributed::Test::FlowDirection;

        /** @brief Names the proxy without its namespace. */
        using Distributed::Test::TCPFaultProxy;

        /** @brief Bounds every wait on a condition, so a broken layer fails a test and hangs none. */
        constexpr std::chrono::seconds K_WAIT{60};

        /** @brief Holds the pause between two reads of a counter that eventually() waits on. */
        constexpr std::chrono::milliseconds K_POLL{2};

        /** @brief Holds the bytes of one kibibyte. */
        constexpr std::size_t K_KIBIBYTE = 1024;

        /** @brief Holds the bytes of one mebibyte. */
        constexpr std::size_t K_MEBIBYTE = 1024 * K_KIBIBYTE;

        /** @brief Names the one operation the worker of this file has a kernel for. */
        constexpr OperationID K_MASK_OPERATION = "tcp_faults.mask"_op;

        /** @brief Holds the value the kernel and the local fallback combine every byte with. */
        constexpr std::byte K_MASK{0x5a};

        /** @brief Holds the bytes a raw peer reads at most while it waits for the master to close. */
        constexpr std::size_t K_REPLY_BYTES = 64 * K_KIBIBYTE;

        /**
         * @brief Waits until a predicate holds, reading it every K_POLL.
         * @return False when K_WAIT passed first.
         */
        template <typename Predicate>
        [[nodiscard]] bool eventually(Predicate is_reached)
        {
            const std::chrono::steady_clock::time_point limit =
                std::chrono::steady_clock::now() + K_WAIT;
            while (!is_reached())
            {
                if (std::chrono::steady_clock::now() >= limit)
                    return false;
                std::this_thread::sleep_for(K_POLL);
            }
            return true;
        }

        /** @brief Returns the byte the tests write at an offset of an input buffer. */
        [[nodiscard]] constexpr std::byte pattern_at(std::size_t offset) noexcept
        {
            return static_cast<std::byte>((offset * 31u + 7u) & 0xffu);
        }

        /** @brief Writes each of @p count input bytes, combined with K_MASK, to the output. */
        void apply_mask(const std::byte* input, std::byte* output, std::size_t count) noexcept
        {
            for (std::size_t offset = 0; offset < count; ++offset)
                output[offset] = input[offset] ^ K_MASK;
        }

        /** @brief Counts where the mask ran. */
        struct RunCounters
        {
            std::atomic<int> worker{0}; /**< Runs of the kernel on the worker. */
            std::atomic<int> local{0};  /**< Runs of the fallback on the master. */
        };

        /** @brief Owns shared USM of the master's allocator, so its release reaches the cluster. */
        class SharedBytes final
        {
            public:
                /** @brief Allocates @p size bytes on device 0. */
                SharedBytes(Internal::Memory::USMAllocator& memory, std::size_t size)
                    : memory_(memory), size_(size),
                      data_(static_cast<std::byte*>(memory.allocate(
                          Internal::Memory::USMRequest{Internal::Memory::USMKind::Shared, size, 0, 0})))
                {

                }

                /** @brief Releases the bytes. */
                ~SharedBytes()
                {
                    memory_.release(data_);
                }

                SharedBytes(const SharedBytes&) = delete;
                SharedBytes& operator=(const SharedBytes&) = delete;
                SharedBytes(SharedBytes&&) = delete;
                SharedBytes& operator=(SharedBytes&&) = delete;

                /** @brief Returns the address. */
                [[nodiscard]] std::byte* data() const noexcept
                {
                    return data_;
                }

                /** @brief Returns the number of bytes. */
                [[nodiscard]] std::size_t size() const noexcept
                {
                    return size_;
                }

            private:
                /** @brief The allocator the bytes came from. */
                Internal::Memory::USMAllocator& memory_;

                /** @brief The number of bytes. */
                std::size_t size_;

                /** @brief The address. */
                std::byte* data_;
        };

        /** @brief Names the two buffers of one mask task; the fixture owns them. */
        struct MaskTask
        {
            SharedBytes* input = nullptr;  /**< Holds the pattern. */
            SharedBytes* output = nullptr; /**< Receives the masked head of the input. */
        };

        /** @brief Picks the first worker offered, so every offloadable task leaves the master. */
        class FirstWorkerPolicy final : public OffloadPolicy
        {
            public:
                /** @brief Returns the first offered worker, or nothing when none was offered. */
                std::optional<int> pick_worker(const NodeOptions& /*task*/,
                                               span<const WorkerLoad> workers,
                                               std::uint64_t /*est_bytes*/) override
                {
                    if (workers.empty())
                        return std::nullopt;
                    return workers.begin()->worker_id;
                }
        };

        /** @brief Counts the ERROR lines that hold a text and lets a test wait for a count. */
        class ErrorLineCounter final
        {
            public:
                /** @brief Builds a counter of the ERROR lines that hold @p text. */
                explicit ErrorLineCounter(std::string text) : text_(std::move(text))
                {

                }

                /** @brief Counts the line when it is an ERROR that holds the text. */
                void record(SushiLog::LogLevel level, std::string_view line)
                {
                    if (level != SushiLog::LogLevel::Error || line.find(text_) == std::string_view::npos)
                        return;
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        ++count_;
                    }
                    changed_.notify_all();
                }

                /** @brief Waits until @p count lines were counted; false after K_WAIT. */
                [[nodiscard]] bool wait_for(std::size_t count)
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    return changed_.wait_for(lock, K_WAIT, [&] { return count_ >= count; });
                }

                /** @brief Returns the lines counted so far. */
                [[nodiscard]] std::size_t count() const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    return count_;
                }

            private:
                /** @brief The text a counted line holds. */
                std::string text_;

                /** @brief Guards count_. */
                mutable std::mutex mutex_;

                /** @brief Wakes wait_for(). */
                std::condition_variable changed_;

                /** @brief The lines counted. */
                std::size_t count_ = 0;
        };

        /** @brief Returns a secret of 32 equal bytes. */
        [[nodiscard]] SharedSecret make_secret(std::uint8_t value)
        {
            std::array<std::uint8_t, SharedSecret::K_MINIMUM_BYTES> bytes{};
            bytes.fill(value);
            return SharedSecret::from_bytes(span<const std::uint8_t>(bytes.data(), bytes.size()))
                .value();
        }

        /** @brief Returns worker transport options that retry within 50 ms of a loss. */
        [[nodiscard]] TCPWorkerOptions quick_reconnect()
        {
            TCPWorkerOptions options;
            options.backoff_initial = std::chrono::milliseconds(10);
            options.backoff_ceiling = std::chrono::milliseconds(50);
            return options;
        }

        /** @brief Returns one frame header that announces a HELLO of a version and a length. */
        [[nodiscard]] std::vector<std::byte> hello_header(std::uint16_t version,
                                                          std::uint32_t payload_length)
        {
            std::vector<std::byte> bytes;
            Wire::append_frame_header(
                Wire::FrameHeader{version, MessageType::Hello, 0, payload_length}, bytes);
            return bytes;
        }

        /** @brief Writes raw bytes to a socket; false when the write did not finish. */
        [[nodiscard]] bool write_bytes(Socket& socket, const std::vector<std::byte>& bytes)
        {
            return socket.write_all({bytes.data(), bytes.size()}, K_WAIT).is_ok();
        }

        /** @brief Writes one message as a version 2 frame; false when it did not leave. */
        template <typename Message>
        [[nodiscard]] bool write_frame(Socket& socket, const Message& message)
        {
            std::vector<std::byte> frame;
            if (Wire::append_frame(message, 0, frame) != Wire::CodecStatus::Ok)
                return false;
            return write_bytes(socket, frame);
        }

        /** @brief Reads one whole frame and returns its type, or nothing when none arrived. */
        [[nodiscard]] std::optional<MessageType> read_frame_type(Socket& socket)
        {
            std::array<std::byte, Wire::K_FRAME_HEADER_BYTES> header_bytes{};
            if (!socket.read_exact({header_bytes.data(), header_bytes.size()}, K_WAIT).is_ok())
                return std::nullopt;

            Wire::FrameHeader header;
            if (Wire::decode_frame_header({header_bytes.data(), header_bytes.size()}, header) !=
                Wire::FrameStatus::Ok)
                return std::nullopt;

            std::vector<std::byte> payload(header.payload_length);
            if (!payload.empty() &&
                !socket.read_exact({payload.data(), payload.size()}, K_WAIT).is_ok())
                return std::nullopt;
            return header.type;
        }

        /**
         * @brief Reads until the other end closes the connection.
         * @return The result of the read; transferred counts what the other end sent first.
         */
        [[nodiscard]] SocketResult read_to_end(Socket& socket)
        {
            std::vector<std::byte> reply(K_REPLY_BYTES);
            return socket.read_exact({reply.data(), reply.size()}, K_WAIT);
        }

        /** @brief Returns whether a read ended because the connection did, not because time ran out. */
        [[nodiscard]] bool is_ended(const SocketResult& read) noexcept
        {
            return read.status == SocketStatus::Closed || read.status == SocketStatus::Failed;
        }

        /**
         * @brief Runs a master, a worker and a fault proxy in one process, over loopback TCP.
         *
         * The master is a Cluster on a Runtime; the worker is a WorkerAgent on a second
         * runtime with one kernel. Each test is skipped on a machine with no SYCL device.
         */
        class Medium_DistributedTcpFaultsTest : public ::testing::Test
        {
            protected:
                /** @brief Builds the master's runtime and makes log calls reach a sink at once. */
                void SetUp() override
                {
                    if (!SushiRuntime::Test::has_any_sycl_device())
                        GTEST_SKIP() << "SYCL reports no device.";

                    saved_level_ = SushiLog::log_level(K_SUSHIRUNTIME_LOG_PROJECT);
                    SushiLog::set_log_synchronous(true);
                    SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Warn);
                    master_runtime_.emplace(Runtime::create().value());
                }

                /** @brief Stops the worker, the master and the proxy, then frees the buffers. */
                void TearDown() override
                {
                    if (agent_)
                        agent_->stop();
                    if (cluster_)
                        cluster_->detach();
                    proxy_.reset();

                    // A detached master has returned its tasks, so a run left over ends here.
                    if (run_.valid())
                        run_.wait();
                    buffers_.clear();

                    SushiLog::set_log_sink(nullptr);
                    if (saved_level_)
                        SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, *saved_level_);
                }

                /** @brief Attaches a master that listens on a free loopback port. */
                void start_master(ClusterOptions options = ClusterOptions(),
                                  const TCPMasterOptions& transport = TCPMasterOptions())
                {
                    options.policy = std::make_shared<FirstWorkerPolicy>();
                    master_transport_ = TCPTransport::make_master(transport);
                    cluster_.emplace(
                        Cluster::attach(*master_runtime_, master_transport_, std::move(options)).value());
                }

                /** @brief Returns the port the master bound; valid after start_master(). */
                [[nodiscard]] Endpoint master_endpoint() const
                {
                    return master_transport_->local_endpoint();
                }

                /** @brief Starts a proxy in front of the master. */
                void start_proxy()
                {
                    proxy_ = std::make_unique<TCPFaultProxy>(master_endpoint());
                    ASSERT_TRUE(proxy_->started().is_ok()) << describe(proxy_->started());
                }

                /**
                 * @brief Starts a worker with the mask kernel that connects to @p target.
                 * @param target The master's endpoint, or the proxy's.
                 */
                void start_worker(const Endpoint& target,
                                  WorkerAgentOptions options = WorkerAgentOptions(),
                                  const TCPWorkerOptions& transport = quick_reconnect())
                {
                    if (!worker_runtime_)
                        worker_runtime_.emplace(Runtime::create().value());

                    RunCounters* const runs = &runs_;
                    sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
                    kernels->register_kernel(
                        K_MASK_OPERATION,
                        [runs](sycl::queue& /*queue*/, span<const BufferView> inputs,
                               span<const BufferView> outputs,
                               const NodeOptions& /*options*/) -> sycl::event
                        {
                            apply_mask(inputs[0].as<const std::byte>(), outputs[0].as<std::byte>(),
                                       outputs[0].size_bytes);
                            runs->worker.fetch_add(1);
                            return sycl::event{};
                        });

                    options.name = "tcp-faults-worker";
                    agent_ = std::make_unique<WorkerAgent>(*worker_runtime_,
                                                           TCPTransport::make_worker(target, transport),
                                                           kernels, std::move(options));
                    const TransportResult started = agent_->start();
                    ASSERT_TRUE(started.is_ok()) << started.message;
                }

                /** @brief Returns whether the master counted @p count peers closed for a bad frame. */
                [[nodiscard]] bool malformed_peers_reach(std::uint64_t count)
                {
                    return eventually([&]
                    {
                        return cluster_->statistics().peers_closed_malformed == count;
                    });
                }

                /** @brief Returns whether one worker registered with the master within K_WAIT. */
                [[nodiscard]] bool worker_registered()
                {
                    return cluster_->wait_for_workers(1, K_WAIT);
                }

                /**
                 * @brief Allocates and shares the buffers of one mask task and fills the input.
                 * @param output_bytes At most @p input_bytes.
                 */
                [[nodiscard]] MaskTask make_mask_task(std::size_t input_bytes,
                                                      std::size_t output_bytes)
                {
                    MaskTask task;
                    task.input = &add_buffer(input_bytes);
                    task.output = &add_buffer(output_bytes);
                    for (std::size_t offset = 0; offset < input_bytes; ++offset)
                        task.input->data()[offset] = pattern_at(offset);
                    for (std::size_t offset = 0; offset < output_bytes; ++offset)
                        task.output->data()[offset] = std::byte{0};
                    return task;
                }

                /**
                 * @brief Starts a graph with the mask task on another thread.
                 * @param beside Work of a second node that names no operation; empty adds none.
                 */
                void launch(const MaskTask& task, std::function<void()> beside = nullptr)
                {
                    Runtime* const runtime = &*master_runtime_;
                    RunCounters* const runs = &runs_;
                    run_ = std::async(std::launch::async,
                                      [runtime, runs, task, beside = std::move(beside)]
                    {
                        const std::byte* const input = task.input->data();
                        std::byte* const output = task.output->data();
                        const std::size_t count = task.output->size();

                        NodeOptions options;
                        options.name = "tcp_faults_mask";
                        options.operation = K_MASK_OPERATION;

                        auto graph = runtime->graph();
                        graph.add_task(
                            Reads(BufferRegion(task.input->data())),
                            Writes(BufferRegion(task.output->data())),
                            [runs, input, output, count](
                                sycl::queue& queue,
                                const std::vector<sycl::event>& /*dependencies*/) -> sycl::event
                            {
                                return queue.submit([&](sycl::handler& handler)
                                {
                                    SushiRuntime::Detail::enqueue_host_task(handler, [=]
                                    {
                                        apply_mask(input, output, count);
                                        runs->local.fetch_add(1);
                                    });
                                });
                            }, options);
                        if (beside)
                            graph.add_host(beside);

                        graph.run().value();
                    });
                }

                /** @brief Waits for the launched graph; false when it did not end within K_WAIT. */
                [[nodiscard]] bool finish_run()
                {
                    if (run_.wait_for(K_WAIT) != std::future_status::ready)
                        return false;
                    run_.get();
                    return true;
                }

                /** @brief Returns whether the output holds the masked head of the input. */
                [[nodiscard]] static bool is_masked(const MaskTask& task)
                {
                    for (std::size_t offset = 0; offset < task.output->size(); ++offset)
                        if (task.output->data()[offset] != (pattern_at(offset) ^ K_MASK))
                            return false;
                    return true;
                }

                /** @brief Runs one mask task to the end and returns whether its output is right. */
                [[nodiscard]] bool run_mask_task(std::size_t input_bytes, std::size_t output_bytes)
                {
                    const MaskTask task = make_mask_task(input_bytes, output_bytes);
                    launch(task);
                    return finish_run() && is_masked(task);
                }

                /** @brief Sends the ERROR lines of the process to version_errors_. */
                void count_version_errors()
                {
                    ErrorLineCounter* const counter = &version_errors_;
                    SushiLog::set_log_sink([counter](const SushiLog::LogRecord& record)
                    {
                        counter->record(record.level, record.text);
                    });
                }

                /// @brief The master's runtime; declared first, so it is destroyed last.
                std::optional<Runtime> master_runtime_;

                /// @brief The worker's runtime; built by the first start_worker().
                std::optional<Runtime> worker_runtime_;

                /// @brief Where the mask ran.
                RunCounters runs_;

                /// @brief Counts the master's lines about a peer of protocol version 2.
                ErrorLineCounter version_errors_{"protocol v2"};

                /// @brief The log level to put back; empty when SetUp() skipped the test.
                std::optional<SushiLog::LogLevel> saved_level_;

                /// @brief The buffers of every mask task, released in TearDown().
                std::vector<std::unique_ptr<SharedBytes>> buffers_;

                /// @brief The master's transport, kept to read the bound port.
                std::shared_ptr<TCPTransport> master_transport_;

                /// @brief The handle of the attached master.
                std::optional<Cluster> cluster_;

                /// @brief The proxy between the worker and the master, when a test uses one.
                std::unique_ptr<TCPFaultProxy> proxy_;

                /// @brief The worker.
                std::unique_ptr<WorkerAgent> agent_;

                /// @brief The graph started by launch().
                std::future<void> run_;

            private:
                /** @brief Allocates a buffer the fixture owns and shares it with the master. */
                SharedBytes& add_buffer(std::size_t size)
                {
                    Internal::Memory::USMAllocator& memory =
                        SushiRuntime::Internal::ContextAccess::context(*master_runtime_).memory();
                    buffers_.push_back(std::make_unique<SharedBytes>(memory, size));
                    SharedBytes& buffer = *buffers_.back();
                    cluster_->share(buffer.data(), buffer.size());
                    return buffer;
                }
        };
    } // namespace

    /** @brief Tests that a connection that never says HELLO is closed and keeps no worker out. */
    TEST_F(Medium_DistributedTcpFaultsTest, StrayConnectionTakesNoSlot)
    {
        ClusterOptions options;
        options.handshake_timeout = std::chrono::milliseconds(500);
        start_master(std::move(options));

        Socket stray;
        ASSERT_TRUE(stray.connect(master_endpoint(), K_WAIT).is_ok());
        ASSERT_NO_FATAL_FAILURE(start_worker(master_endpoint()));
        ASSERT_TRUE(worker_registered());

        const SocketResult ended = read_to_end(stray);

        EXPECT_TRUE(is_ended(ended)) << describe(ended);
        EXPECT_EQ(ended.transferred, 0u);
        const ClusterStatistics statistics = cluster_->statistics();
        EXPECT_EQ(statistics.alive_workers, 1u);
        EXPECT_EQ(statistics.workers.size(), 1u);
        EXPECT_EQ(statistics.peers_lost, 0u);
        EXPECT_EQ(statistics.handshake_rejected_auth, 0u);
    }

    /** @brief Tests that bytes that are no frame close their connection and leave the master up. */
    TEST_F(Medium_DistributedTcpFaultsTest, GarbageBytesClosedMasterLives)
    {
        start_master();
        Socket peer;
        ASSERT_TRUE(peer.connect(master_endpoint(), K_WAIT).is_ok());
        const std::string_view request = "GET / HTTP/1";
        std::vector<std::byte> garbage;
        for (const char letter : request)
            garbage.push_back(static_cast<std::byte>(letter));
        ASSERT_EQ(garbage.size(), Wire::K_FRAME_HEADER_BYTES);

        ASSERT_TRUE(write_bytes(peer, garbage));
        const SocketResult ended = read_to_end(peer);

        EXPECT_TRUE(is_ended(ended)) << describe(ended);
        EXPECT_EQ(ended.transferred, 0u);
        EXPECT_TRUE(malformed_peers_reach(1));
        ASSERT_NO_FATAL_FAILURE(start_worker(master_endpoint()));
        ASSERT_TRUE(worker_registered());
        EXPECT_TRUE(run_mask_task(4 * K_KIBIBYTE, 4 * K_KIBIBYTE));
        EXPECT_EQ(runs_.worker.load(), 1);
        const ClusterStatistics statistics = cluster_->statistics();
        EXPECT_EQ(statistics.handshake_rejected_version, 0u);
        EXPECT_EQ(statistics.peers_closed_malformed, 1u);
        EXPECT_EQ(statistics.completed_remote, 1u);
    }

    /** @brief Tests that a frame of protocol version 1, a length and a type byte, is refused. */
    TEST_F(Medium_DistributedTcpFaultsTest, V1FrameRejected)
    {
        start_master();
        Socket peer;
        ASSERT_TRUE(peer.connect(master_endpoint(), K_WAIT).is_ok());
        const std::vector<std::byte> version_one_hello = {
            std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}};

        ASSERT_TRUE(write_bytes(peer, version_one_hello));
        const SocketResult ended = read_to_end(peer);

        EXPECT_TRUE(is_ended(ended)) << describe(ended);
        EXPECT_EQ(ended.transferred, 0u);
        EXPECT_TRUE(malformed_peers_reach(1));
        EXPECT_EQ(cluster_->statistics().handshake_rejected_version, 0u);
        EXPECT_EQ(cluster_->alive_workers(), 0u);
        ASSERT_NO_FATAL_FAILURE(start_worker(master_endpoint()));
        EXPECT_TRUE(worker_registered());
    }

    /** @brief Tests that a header announcing more than 1 MiB closes the peer, not the process. */
    TEST_F(Medium_DistributedTcpFaultsTest, OversizedLengthClosesPeerNotProcess)
    {
        start_master();
        Socket peer;
        ASSERT_TRUE(peer.connect(master_endpoint(), K_WAIT).is_ok());

        ASSERT_TRUE(write_bytes(
            peer, hello_header(Wire::K_PROTOCOL_VERSION, Wire::K_MAX_FRAME_PAYLOAD + 1)));
        const SocketResult ended = read_to_end(peer);

        EXPECT_TRUE(is_ended(ended)) << describe(ended);
        EXPECT_EQ(ended.transferred, 0u);
        EXPECT_TRUE(malformed_peers_reach(1));
        ASSERT_NO_FATAL_FAILURE(start_worker(master_endpoint()));
        ASSERT_TRUE(worker_registered());
        EXPECT_TRUE(run_mask_task(4 * K_KIBIBYTE, 4 * K_KIBIBYTE));
        EXPECT_EQ(runs_.worker.load(), 1);
        EXPECT_EQ(cluster_->statistics().peers_closed_malformed, 1u);
    }

    /** @brief Tests that a worker and a raw peer that hold another secret are both kept out. */
    TEST_F(Medium_DistributedTcpFaultsTest, WrongSecretRejected)
    {
        ClusterOptions options;
        options.secret = make_secret(0x11);
        start_master(std::move(options));

        WorkerAgentOptions worker;
        worker.secret = make_secret(0x22);
        ASSERT_NO_FATAL_FAILURE(start_worker(master_endpoint(), std::move(worker)));
        EXPECT_EQ(agent_->wait_for_end(K_WAIT), WorkerEnd::RejectedAuthentication);
        agent_->stop();

        Socket impostor;
        ASSERT_TRUE(impostor.connect(master_endpoint(), K_WAIT).is_ok());
        Wire::HelloMessage hello;
        hello.worker_nonce.fill(std::byte{0x33});
        hello.build_id = "tcp-faults";
        hello.node_name = "impostor";
        ASSERT_TRUE(write_frame(impostor, hello));
        const std::optional<MessageType> answer = read_frame_type(impostor);
        ASSERT_TRUE(answer.has_value());
        ASSERT_EQ(*answer, MessageType::Challenge);
        Wire::ProofMessage proof;
        proof.worker_proof.fill(std::byte{0x44});
        proof.max_jobs = 1;
        ASSERT_TRUE(write_frame(impostor, proof));
        const SocketResult ended = read_to_end(impostor);

        EXPECT_TRUE(is_ended(ended)) << describe(ended);
        const ClusterStatistics statistics = cluster_->statistics();
        EXPECT_EQ(statistics.handshake_rejected_auth, 1u);
        EXPECT_EQ(statistics.alive_workers, 0u);
        EXPECT_TRUE(statistics.workers.empty());
    }

    /** @brief Tests that a peer of another protocol version gets one ERROR line per connection. */
    TEST_F(Medium_DistributedTcpFaultsTest, VersionMismatchLoggedOncePerConnection)
    {
        constexpr std::size_t K_CONNECTIONS = 2;
        count_version_errors();
        start_master();
        std::vector<std::byte> two_headers = hello_header(2, 0);
        const std::vector<std::byte> second = hello_header(2, 0);
        two_headers.insert(two_headers.end(), second.begin(), second.end());

        for (std::size_t connection = 0; connection < K_CONNECTIONS; ++connection)
        {
            Socket peer;
            ASSERT_TRUE(peer.connect(master_endpoint(), K_WAIT).is_ok());
            ASSERT_TRUE(write_bytes(peer, two_headers));
            const SocketResult ended = read_to_end(peer);
            EXPECT_TRUE(is_ended(ended)) << describe(ended);
        }
        ASSERT_TRUE(version_errors_.wait_for(K_CONNECTIONS));
        cluster_->detach();

        EXPECT_EQ(version_errors_.count(), K_CONNECTIONS);
        EXPECT_EQ(cluster_->statistics().handshake_rejected_version, K_CONNECTIONS);
    }

    /** @brief Tests that a worker outlives its master and registers with the one that follows. */
    TEST_F(Medium_DistributedTcpFaultsTest, MasterKilledWorkerSurvivesAndReconnects)
    {
        start_master();
        ASSERT_NO_FATAL_FAILURE(start_proxy());
        ASSERT_NO_FATAL_FAILURE(start_worker(proxy_->endpoint()));
        ASSERT_TRUE(worker_registered());
        ASSERT_TRUE(run_mask_task(4 * K_KIBIBYTE, 4 * K_KIBIBYTE));

        proxy_->set_fault(Fault::cut());
        cluster_->detach();
        start_master();
        proxy_->retarget(master_endpoint());
        proxy_->set_fault(Fault::pass());

        ASSERT_TRUE(worker_registered());
        EXPECT_TRUE(eventually([&] { return agent_->statistics().reconnects >= 1; }));
        EXPECT_EQ(agent_->wait_for_end(std::chrono::milliseconds(0)), WorkerEnd::Running);
        EXPECT_TRUE(run_mask_task(4 * K_KIBIBYTE, 4 * K_KIBIBYTE));
        EXPECT_EQ(runs_.worker.load(), 2);
        EXPECT_EQ(runs_.local.load(), 0);
    }

    /** @brief Tests that a task whose input is cut in the middle of a chunk finishes locally. */
    TEST_F(Medium_DistributedTcpFaultsTest, WorkerCutMidChunkTaskFinishesLocally)
    {
        constexpr std::uint64_t K_BYTES_BEFORE_THE_CUT = 100000;
        ClusterOptions options;
        options.chunk_bytes = static_cast<std::uint32_t>(64 * K_KIBIBYTE);
        start_master(std::move(options));
        ASSERT_NO_FATAL_FAILURE(start_proxy());
        TCPWorkerOptions transport = quick_reconnect();
        transport.reconnect = false;
        ASSERT_NO_FATAL_FAILURE(start_worker(proxy_->endpoint(), WorkerAgentOptions(), transport));
        ASSERT_TRUE(worker_registered());
        const MaskTask task = make_mask_task(K_MEBIBYTE, 4 * K_KIBIBYTE);

        proxy_->set_fault(FlowDirection::MasterToWorker,
                          Fault::truncate_after(K_BYTES_BEFORE_THE_CUT));
        launch(task);

        ASSERT_TRUE(finish_run());
        EXPECT_TRUE(is_masked(task));
        EXPECT_EQ(runs_.worker.load(), 0);
        EXPECT_EQ(runs_.local.load(), 1);

        // Both counters rise before the node goes back, so a finished run already shows them.
        const ClusterStatistics statistics = cluster_->statistics();
        EXPECT_EQ(statistics.peers_lost, 1u);
        EXPECT_EQ(statistics.redispatched, 1u);
        EXPECT_EQ(cluster_->offloaded(), 1u);
        EXPECT_EQ(cluster_->alive_workers(), 0u);
    }

    /** @brief Tests that a worker behind a silent link is reaped and registers again after it. */
    TEST_F(Medium_DistributedTcpFaultsTest, StalledLinkReapedThenWorkerRejoins)
    {
        ClusterOptions options;
        options.liveness_timeout = std::chrono::milliseconds(300);
        start_master(std::move(options));
        ASSERT_NO_FATAL_FAILURE(start_proxy());
        WorkerAgentOptions worker;
        worker.heartbeat_interval = std::chrono::milliseconds(50);
        ASSERT_NO_FATAL_FAILURE(start_worker(proxy_->endpoint(), std::move(worker)));
        ASSERT_TRUE(worker_registered());

        // The master counts the worker before its WELCOME arrives; the stall waits for the WELCOME.
        ASSERT_TRUE(eventually([&] { return agent_->statistics().registrations >= 1; }));

        proxy_->set_fault(Fault::stall());
        ASSERT_TRUE(eventually([&]
        {
            const ClusterStatistics statistics = cluster_->statistics();
            return statistics.peers_lost >= 1 && statistics.alive_workers == 0;
        }));
        cluster_->liveness_timeout(K_WAIT).value();
        proxy_->set_fault(Fault::pass());

        ASSERT_TRUE(worker_registered());
        EXPECT_TRUE(eventually([&] { return agent_->statistics().reconnects >= 1; }));
        EXPECT_TRUE(run_mask_task(4 * K_KIBIBYTE, 4 * K_KIBIBYTE));
        EXPECT_EQ(runs_.worker.load(), 1);
    }

    /**
     * @brief Tests that a link stalled in the middle of a large transfer is lost when liveness
     *        closes it, not when the write idle timeout ends, and that its task finishes locally.
     */
    TEST_F(Medium_DistributedTcpFaultsTest, LinkStalledMidTransferLostAtLivenessClose)
    {
        // The writer must have waited longer than the connection's 1 s linger when liveness closes.
        constexpr std::chrono::milliseconds K_LIVENESS{2000};
        constexpr std::uint64_t K_BYTES_BEFORE_THE_STALL = K_MEBIBYTE;
        ClusterOptions options;
        options.liveness_timeout = K_LIVENESS;
        options.task_deadline = std::chrono::milliseconds(0);
        TCPMasterOptions master_transport;
        master_transport.write_idle_timeout = 10 * K_WAIT;
        start_master(std::move(options), master_transport);
        ASSERT_NO_FATAL_FAILURE(start_proxy());
        WorkerAgentOptions worker;
        worker.heartbeat_interval = std::chrono::milliseconds(50);
        ASSERT_NO_FATAL_FAILURE(start_worker(proxy_->endpoint(), std::move(worker)));
        ASSERT_TRUE(worker_registered());
        const MaskTask task = make_mask_task(32 * K_MEBIBYTE, 4 * K_KIBIBYTE);
        const std::uint64_t forwarded_before =
            proxy_->forwarded_bytes(FlowDirection::MasterToWorker);

        proxy_->set_fault(FlowDirection::MasterToWorker, Fault::throttle(8 * K_MEBIBYTE));
        launch(task);
        ASSERT_TRUE(eventually([&]
        {
            return proxy_->forwarded_bytes(FlowDirection::MasterToWorker) >=
                   forwarded_before + K_BYTES_BEFORE_THE_STALL;
        }));
        proxy_->set_fault(Fault::stall());

        // The master's writer waits in a socket write that would end 10 minutes from here.
        ASSERT_TRUE(eventually([&]
        {
            const ClusterStatistics statistics = cluster_->statistics();
            return statistics.peers_lost == 1 && statistics.redispatched == 1;
        })) << "the stalled peer was not lost when liveness closed it";
        ASSERT_TRUE(finish_run());
        EXPECT_TRUE(is_masked(task));
        EXPECT_EQ(runs_.worker.load(), 0);
        EXPECT_EQ(runs_.local.load(), 1);
        const ClusterStatistics statistics = cluster_->statistics();
        EXPECT_EQ(statistics.completed_remote, 0u);
        EXPECT_EQ(statistics.pending_tasks, 0u);
    }

    /** @brief Tests that 8 MiB at 4 MiB/s crosses under a 500 ms liveness timeout. */
    TEST_F(Medium_DistributedTcpFaultsTest, ThrottledTransferKeepsWorkerAlive)
    {
        constexpr std::chrono::milliseconds K_LIVENESS{500};
        ClusterOptions options;
        options.liveness_timeout = K_LIVENESS;
        start_master(std::move(options));
        ASSERT_NO_FATAL_FAILURE(start_proxy());
        WorkerAgentOptions worker;
        worker.heartbeat_interval = std::chrono::milliseconds(50);
        ASSERT_NO_FATAL_FAILURE(start_worker(proxy_->endpoint(), std::move(worker)));
        ASSERT_TRUE(worker_registered());
        const MaskTask task = make_mask_task(8 * K_MEBIBYTE, 4 * K_KIBIBYTE);

        proxy_->set_fault(FlowDirection::MasterToWorker, Fault::throttle(4 * K_MEBIBYTE));
        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        launch(task);
        ASSERT_TRUE(finish_run());
        const std::chrono::steady_clock::duration elapsed =
            std::chrono::steady_clock::now() - started;

        EXPECT_GE(elapsed, 3 * K_LIVENESS);
        EXPECT_TRUE(is_masked(task));
        EXPECT_EQ(runs_.worker.load(), 1);
        EXPECT_EQ(runs_.local.load(), 0);
        const ClusterStatistics statistics = cluster_->statistics();
        EXPECT_EQ(statistics.peers_lost, 0u);
        EXPECT_EQ(statistics.redispatched, 0u);
        EXPECT_EQ(statistics.tasks_reaped_deadline, 0u);
        EXPECT_EQ(statistics.tasks_reaped_unacknowledged, 0u);
        EXPECT_EQ(statistics.tasks_reaped_lost_result, 0u);
        EXPECT_EQ(agent_->statistics().reconnects, 0u);
    }

    /** @brief Tests that 1 MiB crosses whole in both directions as 256 chunks of 4 KiB each. */
    TEST_F(Medium_DistributedTcpFaultsTest, ManyChunkBufferCrossesIntact)
    {
        ClusterOptions options;
        options.chunk_bytes = K_MINIMUM_CHUNK_BYTES;
        start_master(std::move(options));
        ASSERT_NO_FATAL_FAILURE(start_proxy());
        ASSERT_NO_FATAL_FAILURE(start_worker(proxy_->endpoint()));
        ASSERT_TRUE(worker_registered());

        EXPECT_TRUE(run_mask_task(K_MEBIBYTE, K_MEBIBYTE));

        EXPECT_EQ(runs_.worker.load(), 1);
        EXPECT_EQ(runs_.local.load(), 0);
        const ClusterStatistics statistics = cluster_->statistics();
        EXPECT_EQ(statistics.completed_remote, 1u);
        EXPECT_EQ(statistics.chunks_sent, K_MEBIBYTE / K_MINIMUM_CHUNK_BYTES);
        EXPECT_EQ(statistics.chunks_received, K_MEBIBYTE / K_MINIMUM_CHUNK_BYTES);
        EXPECT_EQ(statistics.transfers_aborted, 0u);
        EXPECT_EQ(statistics.stale_writebacks, 0u);
    }

    /** @brief Tests that a dispatch to a silent link returns and a local node runs beside it. */
    TEST_F(Medium_DistributedTcpFaultsTest, DispatchReturnsWhilePeerStalled)
    {
        ClusterOptions options;
        options.liveness_timeout = K_WAIT;
        options.task_deadline = std::chrono::milliseconds(0);
        start_master(std::move(options));
        ASSERT_NO_FATAL_FAILURE(start_proxy());
        ASSERT_NO_FATAL_FAILURE(start_worker(proxy_->endpoint()));
        ASSERT_TRUE(worker_registered());
        const MaskTask task = make_mask_task(4 * K_KIBIBYTE, 4 * K_KIBIBYTE);
        std::promise<void> local_node;
        std::promise<void>* const local_node_ran = &local_node;
        std::future<void> local_node_done = local_node.get_future();

        proxy_->set_fault(Fault::stall());
        launch(task, [local_node_ran] { local_node_ran->set_value(); });

        EXPECT_EQ(local_node_done.wait_for(K_WAIT), std::future_status::ready);
        EXPECT_TRUE(eventually([&] { return cluster_->offloaded() == 1; }));
        EXPECT_EQ(cluster_->statistics().pending_tasks, 1u);
        EXPECT_EQ(runs_.worker.load(), 0);
        EXPECT_EQ(run_.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);

        proxy_->set_fault(Fault::pass());
        ASSERT_TRUE(finish_run());
        EXPECT_TRUE(is_masked(task));
        EXPECT_EQ(runs_.worker.load(), 1);
        EXPECT_EQ(runs_.local.load(), 0);
    }
} // namespace TCPFaultsTests
