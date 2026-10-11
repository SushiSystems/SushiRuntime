/**************************************************************************/
/* test_distributed_tcp_faults.cpp                                        */
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
 * @file test_distributed_tcp_faults.cpp
 * @brief Tests a master and a worker over loopback TCP while the link between them is faulted.
 * @author Mustafa Garip
 */

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/logging/log_level.hpp>
#include <SushiRuntime/logging/log_sink.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/distributed/cluster_statistics.hpp>
#include <SushiRuntime/distributed/endpoint.hpp>
#include <SushiRuntime/distributed/offload_policy.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/message_type.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>

#include "socket.hpp"
#include "tcp_fault_proxy.hpp"
#include "test_helpers.hpp"
#include "wire_frame.hpp"
#include "wire_messages.hpp"

using namespace SushiRuntime;
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
            std::optional<int> pick_worker(const Internal::Graph::Node& /*node*/,
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
            void record(LogLevel level, std::string_view line)
            {
                if (level != LogLevel::Error || line.find(text_) == std::string_view::npos)
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
        return SharedSecret::from_bytes(span<const std::uint8_t>(bytes.data(), bytes.size()));
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
     * The master is a Cluster on a RuntimeContext; the worker is a WorkerAgent on a second
     * context with one kernel. Each test is skipped on a machine with no SYCL device.
     */
    class Integration_DistributedTcpFaultsTest : public ::testing::Test
    {
        protected:
            /** @brief Builds the master's context and makes log calls reach a sink at once. */
            void SetUp() override
            {
                if (!SushiRuntime::Test::has_any_sycl_device())
                    GTEST_SKIP() << "SYCL reports no device.";

                saved_level_ = log_level();
                set_log_synchronous(true);
                set_log_level(LogLevel::Warn);
                master_context_ = std::make_unique<Internal::Execution::RuntimeContext>();
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

                set_log_sink(nullptr);
                if (saved_level_)
                    set_log_level(*saved_level_);
            }

            /** @brief Attaches a master that listens on a free loopback port. */
            void start_master(ClusterOptions options = ClusterOptions(),
                              const TCPMasterOptions& transport = TCPMasterOptions())
            {
                options.policy = std::make_shared<FirstWorkerPolicy>();
                master_transport_ = TCPTransport::make_master(transport);
                cluster_.emplace(
                    Cluster::attach(*master_context_, master_transport_, std::move(options)));
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
                if (!worker_context_)
                    worker_context_ = std::make_unique<Internal::Execution::RuntimeContext>();

                RunCounters* const runs = &runs_;
                sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
                kernels->register_kernel(
                    K_MASK_OPERATION,
                    [runs](sycl::queue& /*queue*/, span<const BufferView> inputs,
                           span<const BufferView> outputs,
                           const Internal::Graph::TaskMetadata& /*metadata*/) -> sycl::event
                    {
                        apply_mask(inputs[0].as<const std::byte>(), outputs[0].as<std::byte>(),
                                   outputs[0].size_bytes);
                        runs->worker.fetch_add(1);
                        return sycl::event{};
                    });

                options.name = "tcp-faults-worker";
                agent_ = std::make_unique<WorkerAgent>(*worker_context_,
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
                Internal::Execution::RuntimeContext* const context = master_context_.get();
                RunCounters* const runs = &runs_;
                run_ = std::async(std::launch::async,
                                  [context, runs, task, beside = std::move(beside)]
                {
                    const std::byte* const input = task.input->data();
                    std::byte* const output = task.output->data();
                    const std::size_t count = task.output->size();

                    Internal::Graph::TaskMetadata metadata;
                    metadata.name = "tcp_faults_mask";
                    metadata.op_id = K_MASK_OPERATION;

                    Internal::Graph::TaskGraph graph(context->offload());
                    graph.add_task(
                        metadata, {task.input->data()}, {task.output->data()},
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
                        });
                    if (beside)
                        graph.add_native({}, {}, beside);

                    context->execution_engine().execute(graph.compile());
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
                set_log_sink([counter](LogLevel level, std::string_view line)
                {
                    counter->record(level, line);
                });
            }

            /// @brief The master's runtime; declared first, so it is destroyed last.
            std::unique_ptr<Internal::Execution::RuntimeContext> master_context_;

            /// @brief The worker's runtime; built by the first start_worker().
            std::unique_ptr<Internal::Execution::RuntimeContext> worker_context_;

            /// @brief Where the mask ran.
            RunCounters runs_;

            /// @brief Counts the master's lines about a peer of protocol version 3.
            ErrorLineCounter version_errors_{"protocol v3"};

            /// @brief The log level to put back; empty when SetUp() skipped the test.
            std::optional<LogLevel> saved_level_;

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
                buffers_.push_back(
                    std::make_unique<SharedBytes>(master_context_->memory(), size));
                SharedBytes& buffer = *buffers_.back();
                master_context_->offload()->share(buffer.data(), buffer.size());
                return buffer;
            }
    };
} // namespace

/** @brief Tests that a connection that never says HELLO is closed and keeps no worker out. */
TEST_F(Integration_DistributedTcpFaultsTest, StrayConnectionTakesNoSlot)
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
TEST_F(Integration_DistributedTcpFaultsTest, GarbageBytesClosedMasterLives)
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
TEST_F(Integration_DistributedTcpFaultsTest, V1FrameRejected)
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
TEST_F(Integration_DistributedTcpFaultsTest, OversizedLengthClosesPeerNotProcess)
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
TEST_F(Integration_DistributedTcpFaultsTest, WrongSecretRejected)
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
TEST_F(Integration_DistributedTcpFaultsTest, VersionMismatchLoggedOncePerConnection)
{
    if (K_LOG_LEVEL_CEILING < LogLevel::Error)
        GTEST_SKIP() << "This build compiles no log call.";

    constexpr std::size_t K_CONNECTIONS = 2;
    count_version_errors();
    start_master();
    std::vector<std::byte> two_headers = hello_header(3, 0);
    const std::vector<std::byte> second = hello_header(3, 0);
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
TEST_F(Integration_DistributedTcpFaultsTest, MasterKilledWorkerSurvivesAndReconnects)
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
TEST_F(Integration_DistributedTcpFaultsTest, WorkerCutMidChunkTaskFinishesLocally)
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
TEST_F(Integration_DistributedTcpFaultsTest, StalledLinkReapedThenWorkerRejoins)
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
    cluster_->liveness_timeout(K_WAIT);
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
TEST_F(Integration_DistributedTcpFaultsTest, LinkStalledMidTransferLostAtLivenessClose)
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
TEST_F(Integration_DistributedTcpFaultsTest, ThrottledTransferKeepsWorkerAlive)
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
TEST_F(Integration_DistributedTcpFaultsTest, ManyChunkBufferCrossesIntact)
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
TEST_F(Integration_DistributedTcpFaultsTest, DispatchReturnsWhilePeerStalled)
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
