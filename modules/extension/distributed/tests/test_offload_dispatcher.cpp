/**************************************************************************/
/* test_offload_dispatcher.cpp                                            */
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
 * @file test_offload_dispatcher.cpp
 * @brief Tests OffloadDispatcher over real bricks, a fake transport and a fake policy.
 * @author Mustafa Garip
 * @see modules/extension/distributed/tests/README.md
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/buffer_handle.hpp>
#include <SushiRuntime/distributed/cluster_statistics.hpp>
#include <SushiRuntime/distributed/i_offload_policy.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/message_type.hpp>

#include "buffer_registry.hpp"
#include "handshake.hpp"
#include "offload_dispatcher.hpp"
#include "pending_tasks.hpp"
#include "wire_messages.hpp"
#include "worker_directory.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Names the worker every test registers. */
    constexpr PeerId K_WORKER{1};

    /** @brief Names an operation the worker has a kernel for. */
    constexpr Graph::OpID K_KNOWN_OPERATION{501};

    /** @brief Names an operation no worker has a kernel for. */
    constexpr Graph::OpID K_ABSENT_OPERATION{502};

    /** @brief Holds the tasks the worker says it queues at most. */
    constexpr std::uint32_t K_WORKER_MAX_JOBS = 8;

    /** @brief Holds the elements of each buffer a test shares. */
    constexpr std::size_t K_BUFFER_ELEMENTS = 16;

    /** @brief Names the storage of one buffer of a test. */
    using Storage = std::array<float, K_BUFFER_ELEMENTS>;

    /** @brief Holds one frame a test transport was asked to send. */
    struct OfferedFrame
    {
        PeerId peer;                    /**< The peer the frame was for. */
        MessageType type;               /**< The type of the frame. */
        std::vector<std::byte> payload; /**< A copy of the payload. */
    };

    /**
     * @brief Records every send() and answers it with a status the test sets.
     */
    class RecordingTransport final : public ITransport
    {
        public:
            /** @brief Reports a start that needs nothing. */
            [[nodiscard]] TransportResult start(TransportEvents& /*events*/) override
            {
                return TransportResult();
            }

            /** @brief Does nothing; the transport owns no thread. */
            void stop() noexcept override
            {

            }

            /** @brief Keeps a copy of the frame and returns the status set for the test. */
            [[nodiscard]] SendStatus send(PeerId peer, MessageType type,
                                          span<const std::byte> payload) override
            {
                OfferedFrame frame{peer, type, std::vector<std::byte>()};
                frame.payload.assign(payload.begin(), payload.end());
                offered.push_back(std::move(frame));
                return answer;
            }

            /** @brief Refuses every stream; the dispatcher sends none. */
            [[nodiscard]] SendStatus send_stream(PeerId /*peer*/,
                                                 std::unique_ptr<FrameSource> /*source*/) override
            {
                return SendStatus::Stopped;
            }

            /** @brief Does nothing; the transport holds no connection. */
            void close_peer(PeerId /*peer*/) noexcept override
            {

            }

            /** @brief Reports a transport only this process reaches. */
            [[nodiscard]] TransportExposure exposure() const noexcept override
            {
                return TransportExposure::Loopback;
            }

            /** @brief Reports that no byte moved. */
            [[nodiscard]] TransportStatistics statistics() const noexcept override
            {
                return TransportStatistics();
            }

            /** @brief Holds what send() returns. */
            SendStatus answer = SendStatus::Queued;

            /** @brief Holds every frame send() was given, in order. */
            std::vector<OfferedFrame> offered;
    };

    /**
     * @brief Picks the first worker it is offered and counts how often it was asked.
     */
    class FirstWorkerPolicy final : public IOffloadPolicy
    {
        public:
            /** @brief Returns the first offered worker, or nothing when none was offered. */
            std::optional<int> pick_worker(const Graph::Node& /*node*/,
                                           span<const WorkerStats> workers,
                                           std::uint64_t /*est_bytes*/) override
            {
                ++calls;
                if (workers.empty())
                    return std::nullopt;
                return workers.begin()->worker_id;
            }

            /** @brief Counts the calls to pick_worker(). */
            int calls = 0;
    };

    /** @brief Returns handshake octets as the bytes a wire message carries. */
    template <std::size_t Count>
    std::array<std::byte, Count> to_wire(const std::array<std::uint8_t, Count>& octets)
    {
        std::array<std::byte, Count> bytes{};
        std::transform(octets.begin(), octets.end(), bytes.begin(),
                       [](std::uint8_t octet) { return std::byte{octet}; });
        return bytes;
    }

    /** @brief Returns the dispatch a transport was offered, decoded. */
    Wire::TaskDispatchMessage decode_dispatch(const OfferedFrame& frame)
    {
        Wire::TaskDispatchMessage dispatch;
        const Wire::DecodeResult decoded = Wire::TaskDispatchMessage::decode(
            span<const std::byte>(frame.payload.data(), frame.payload.size()), dispatch);
        EXPECT_TRUE(decoded.ok());
        return dispatch;
    }

    /**
     * @brief Builds a dispatcher over a registry, a directory with one worker, a pending
     *        table and a recording transport, and restores the logger a test changed.
     */
    class Unit_OffloadDispatcherTest : public ::testing::Test
    {
        protected:
            /** @brief Registers the worker and shares two of the three buffers. */
            void SetUp() override
            {
                saved_level_ = Core::log_level();
                Core::set_log_synchronous(true);
                Core::set_log_level(Core::LogLevel::Warn);

                register_worker();
                registry_.register_buffer(first_.data(), sizeof(Storage));
                registry_.register_buffer(second_.data(), sizeof(Storage));
            }

            /** @brief Puts the default sink and the saved level back. */
            void TearDown() override
            {
                Core::set_log_sink(nullptr);
                Core::set_log_level(saved_level_);
            }

            /** @brief Takes the worker through HELLO and PROOF under the empty secret. */
            void register_worker()
            {
                const std::chrono::steady_clock::time_point now =
                    std::chrono::steady_clock::now();
                HandshakeNonce worker_nonce{};
                worker_nonce.bytes.fill(0x11);
                HandshakeNonce master_nonce{};
                master_nonce.bytes.fill(0x22);

                directory_.add_peer(K_WORKER, "test:1", now);

                Wire::HelloMessage hello;
                hello.worker_nonce = to_wire(worker_nonce.bytes);
                hello.build_id = "test-build";
                hello.node_name = "worker";
                ASSERT_EQ(directory_.accept_hello(K_WORKER, hello, master_nonce).step,
                          HandshakeStep::Accepted);

                Wire::ProofMessage proof;
                proof.worker_proof = to_wire(compute_proof(
                    HandshakeRole::Worker, span<const std::uint8_t>(), worker_nonce,
                    master_nonce));
                proof.ops = {K_KNOWN_OPERATION};
                proof.max_jobs = K_WORKER_MAX_JOBS;
                ASSERT_EQ(directory_.accept_proof(K_WORKER, proof, now), HandshakeStep::Accepted);
            }

            /** @brief Returns a node that reads @p read and writes @p written under @p op. */
            static sushi_ptr<Graph::Node> fixed_node(Graph::OpID op, void* read, void* written)
            {
                sushi_ptr<Graph::Node> node = make_sushi<Graph::Node>();
                node->metadata.op_id = op;
                node->io_reads_.emplace_back(read);
                node->io_writes_.emplace_back(written);
                return node;
            }

            /** @brief Runs prepare() over the given nodes. */
            void prepare(const std::vector<sushi_ptr<Graph::Node>>& nodes)
            {
                dispatcher_.prepare(
                    span<const sushi_ptr<Graph::Node>>(nodes.data(), nodes.size()));
            }

            /** @brief Returns the version the registry holds for an address. */
            std::uint32_t version_of(void* address) const
            {
                const std::optional<BufferHandle> handle = registry_.lookup(address);
                EXPECT_TRUE(handle.has_value());
                return handle.has_value() ? handle->version : 0;
            }

            /** @brief Returns the worker's inflight count as the directory reports it. */
            std::uint32_t worker_inflight() const
            {
                std::vector<WorkerSnapshot> workers;
                directory_.snapshot(std::chrono::steady_clock::now(), workers);
                EXPECT_EQ(workers.size(), 1u);
                return workers.empty() ? 0 : workers.front().inflight;
            }

            /// @brief The storage of the first shared buffer.
            Storage first_{};

            /// @brief The storage of the second shared buffer.
            Storage second_{};

            /// @brief The storage of a buffer no test shares.
            Storage unshared_{};

            /// @brief The registry the dispatcher reads handles from.
            BufferRegistry registry_;

            /// @brief The policy the directory asks.
            std::shared_ptr<FirstWorkerPolicy> policy_ = std::make_shared<FirstWorkerPolicy>();

            /// @brief The directory with the one worker.
            WorkerDirectory directory_{span<const std::uint8_t>(), policy_};

            /// @brief The table that takes each dispatched node.
            PendingTasks pending_;

            /// @brief The transport that records each dispatch.
            RecordingTransport transport_;

            /// @brief The dispatcher under test.
            OffloadDispatcher dispatcher_{registry_, directory_, pending_, transport_};

            /// @brief The level the process had when the test began.
            Core::LogLevel saved_level_ = Core::LogLevel::Warn;
    };
} // namespace

/** @brief Tests that a node at the attempt cap is declined and offered again next run. */
TEST_F(Unit_OffloadDispatcherTest, AttemptCapDeclinesWithoutClearingOffloadable)
{
    const sushi_ptr<Graph::Node> node =
        fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
    prepare({node});
    Graph::OffloadState* state = node->offload_state();
    ASSERT_NE(state, nullptr);
    ASSERT_TRUE(state->offloadable);

    state->attempts.store(K_MAXIMUM_OFFLOAD_ATTEMPTS);
    EXPECT_FALSE(dispatcher_.try_offload(*node));
    EXPECT_TRUE(state->offloadable);
    EXPECT_EQ(state->attempts.load(), K_MAXIMUM_OFFLOAD_ATTEMPTS);
    EXPECT_TRUE(transport_.offered.empty());
    EXPECT_EQ(policy_->calls, 0);
    EXPECT_EQ(dispatcher_.statistics().declined, 1u);

    dispatcher_.complete_local(*node);
    EXPECT_EQ(state->attempts.load(), 0u);

    EXPECT_TRUE(dispatcher_.try_offload(*node));
    EXPECT_EQ(transport_.offered.size(), 1u);
    EXPECT_EQ(dispatcher_.statistics().offloaded, 1u);
    EXPECT_EQ(pending_.size(), 1u);

    std::vector<PendingTask> held;
    pending_.drain(held);
    ASSERT_EQ(held.size(), 1u);
    EXPECT_EQ(held.front().node, node.get());
}

/** @brief Tests that a send the transport refuses leaves no task, no load and a false. */
TEST_F(Unit_OffloadDispatcherTest, RefusedSendUndoesThePendingEntry)
{
    const sushi_ptr<Graph::Node> node =
        fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
    prepare({node});
    const std::uint32_t output_version = version_of(second_.data());

    transport_.answer = SendStatus::QueueFull;
    EXPECT_FALSE(dispatcher_.try_offload(*node));

    ASSERT_EQ(transport_.offered.size(), 1u);
    EXPECT_EQ(transport_.offered.front().type, MessageType::TaskDispatch);
    EXPECT_EQ(transport_.offered.front().peer, K_WORKER);
    EXPECT_EQ(pending_.size(), 0u);
    EXPECT_EQ(worker_inflight(), 0u);

    const OffloadDispatcherStatistics counters = dispatcher_.statistics();
    EXPECT_EQ(counters.offloaded, 0u);
    EXPECT_EQ(counters.declined, 1u);
    EXPECT_EQ(counters.send_queue_full, 1u);

    // The dispatch frame named the bumped version, so the registry keeps it.
    EXPECT_EQ(version_of(second_.data()), output_version + 1);
    EXPECT_EQ(node->offload_state()->attempts.load(), 1u);
}

/** @brief Tests that nothing is dispatched once the pending table and the directory closed. */
TEST_F(Unit_OffloadDispatcherTest, StoppedCoordinatorDeclines)
{
    const sushi_ptr<Graph::Node> node =
        fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
    prepare({node});

    // With only the table closed the worker is picked, then given back.
    pending_.close();
    EXPECT_FALSE(dispatcher_.try_offload(*node));
    EXPECT_EQ(policy_->calls, 1);
    EXPECT_EQ(worker_inflight(), 0u);
    EXPECT_TRUE(transport_.offered.empty());

    directory_.close();
    EXPECT_FALSE(dispatcher_.try_offload(*node));
    EXPECT_EQ(policy_->calls, 1);
    EXPECT_TRUE(transport_.offered.empty());
    EXPECT_EQ(pending_.size(), 0u);
    EXPECT_EQ(dispatcher_.statistics().declined, 2u);
    EXPECT_TRUE(node->offload_state()->offloadable);
}

/** @brief Tests that a double-buffered node dispatches and settles its live lists. */
TEST_F(Unit_OffloadDispatcherTest, DynamicNodeResolvesItsLiveLists)
{
    const sushi_ptr<Graph::Node> node =
        fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
    Graph::OffloadState& state = node->offload();
    state.live_reads = std::make_shared<std::vector<void*>>(1, static_cast<void*>(first_.data()));
    state.live_writes = std::make_shared<std::vector<void*>>(1, static_cast<void*>(second_.data()));
    prepare({node});
    ASSERT_TRUE(state.offloadable);
    EXPECT_EQ(state.payload, nullptr);

    const GlobalBufferID first_id = registry_.lookup(first_.data())->id;
    const GlobalBufferID second_id = registry_.lookup(second_.data())->id;

    ASSERT_TRUE(dispatcher_.try_offload(*node));
    ASSERT_EQ(transport_.offered.size(), 1u);
    const Wire::TaskDispatchMessage before_swap = decode_dispatch(transport_.offered[0]);
    ASSERT_EQ(before_swap.inputs.size(), 1u);
    ASSERT_EQ(before_swap.outputs.size(), 1u);
    EXPECT_EQ(before_swap.inputs[0].id, first_id);
    EXPECT_EQ(before_swap.outputs[0].id, second_id);
    EXPECT_EQ(before_swap.outputs[0].version, version_of(second_.data()));

    std::vector<PendingTask> held;
    pending_.drain(held);
    ASSERT_EQ(held.size(), 1u);
    directory_.release_inflight(held.front().worker);
    dispatcher_.complete_remote(*node);

    // The step boundary swaps the two buffers; the baked lists still name the old roles.
    state.live_reads->assign(1, static_cast<void*>(second_.data()));
    state.live_writes->assign(1, static_cast<void*>(first_.data()));

    ASSERT_TRUE(dispatcher_.try_offload(*node));
    ASSERT_EQ(transport_.offered.size(), 2u);
    const Wire::TaskDispatchMessage after_swap = decode_dispatch(transport_.offered[1]);
    ASSERT_EQ(after_swap.inputs.size(), 1u);
    ASSERT_EQ(after_swap.outputs.size(), 1u);
    EXPECT_EQ(after_swap.inputs[0].id, second_id);
    EXPECT_EQ(after_swap.outputs[0].id, first_id);

    held.clear();
    pending_.drain(held);
    ASSERT_EQ(held.size(), 1u);

    const std::uint32_t first_version = version_of(first_.data());
    const std::uint32_t second_version = version_of(second_.data());
    dispatcher_.complete_local(*node);
    EXPECT_EQ(version_of(first_.data()), first_version + 1);
    EXPECT_EQ(version_of(second_.data()), second_version);
}

/** @brief Tests that one prepare() that leaves op-tagged nodes local logs one WARN. */
TEST_F(Unit_OffloadDispatcherTest, PrepareWarnsOnceForLocalOnlyOps)
{
    std::vector<std::string> warnings;
    std::vector<std::string>* const sink_target = &warnings;
    Core::set_log_sink([sink_target](Core::LogLevel level, std::string_view line)
    {
        if (level == Core::LogLevel::Warn)
            sink_target->push_back(std::string(line));
    });

    const sushi_ptr<Graph::Node> offloadable =
        fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
    const sushi_ptr<Graph::Node> without_worker =
        fixed_node(K_ABSENT_OPERATION, first_.data(), second_.data());
    const sushi_ptr<Graph::Node> without_share =
        fixed_node(K_KNOWN_OPERATION, first_.data(), unshared_.data());
    const sushi_ptr<Graph::Node> untagged =
        fixed_node(Graph::OpID(0), first_.data(), unshared_.data());

    prepare({offloadable, without_worker, without_share, untagged, sushi_ptr<Graph::Node>()});

    ASSERT_EQ(warnings.size(), 1u);
    const std::string expected =
        "2 of 3 op-tagged nodes will run locally: 1 no live worker for the op, 1 buffer not "
        "shared, 0 buffer written twice (first: op " +
        std::to_string(K_ABSENT_OPERATION.value) + ")";
    EXPECT_NE(warnings.front().find(expected), std::string::npos) << warnings.front();

    EXPECT_TRUE(offloadable->offload_state()->offloadable);
    EXPECT_FALSE(without_worker->offload_state()->offloadable);
    EXPECT_EQ(without_share->offload_state(), nullptr);
    EXPECT_EQ(untagged->offload_state(), nullptr);

    prepare({offloadable});
    EXPECT_EQ(warnings.size(), 1u);
}

/** @brief Tests that a node that stays local raises only the shared buffers it writes. */
TEST_F(Unit_OffloadDispatcherTest, LocalNodeBumpsOnlyItsSharedWrites)
{
    const sushi_ptr<Graph::Node> writer =
        fixed_node(Graph::OpID(0), unshared_.data(), second_.data());
    const sushi_ptr<Graph::Node> reader =
        fixed_node(Graph::OpID(0), second_.data(), unshared_.data());
    prepare({writer, reader});

    EXPECT_EQ(reader->offload_state(), nullptr);
    ASSERT_NE(writer->offload_state(), nullptr);
    EXPECT_FALSE(writer->offload_state()->offloadable);

    const std::uint32_t first_version = version_of(first_.data());
    const std::uint32_t second_version = version_of(second_.data());
    dispatcher_.complete_local(*writer);
    dispatcher_.complete_local(*reader);
    EXPECT_EQ(version_of(first_.data()), first_version);
    EXPECT_EQ(version_of(second_.data()), second_version + 1);
}

/** @brief Tests that a node whose writes name one buffer twice stays local and is counted. */
TEST_F(Unit_OffloadDispatcherTest, NodeThatWritesOneBufferTwiceStaysLocal)
{
    std::vector<std::string> warnings;
    std::vector<std::string>* const sink_target = &warnings;
    Core::set_log_sink([sink_target](Core::LogLevel level, std::string_view line)
    {
        if (level == Core::LogLevel::Warn)
            sink_target->push_back(std::string(line));
    });

    const sushi_ptr<Graph::Node> fixed =
        fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
    fixed->io_writes_.emplace_back(second_.data());

    const sushi_ptr<Graph::Node> dynamic =
        fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
    Graph::OffloadState& dynamic_state = dynamic->offload();
    dynamic_state.live_reads =
        std::make_shared<std::vector<void*>>(1, static_cast<void*>(first_.data()));
    dynamic_state.live_writes =
        std::make_shared<std::vector<void*>>(2, static_cast<void*>(second_.data()));

    // The same buffer as an input and as an output is one write, so this node may leave.
    const sushi_ptr<Graph::Node> in_place =
        fixed_node(K_KNOWN_OPERATION, second_.data(), second_.data());

    prepare({fixed, dynamic, in_place});

    ASSERT_EQ(warnings.size(), 1u);
    const std::string expected =
        "2 of 3 op-tagged nodes will run locally: 0 no live worker for the op, 0 buffer not "
        "shared, 2 buffer written twice (first: op " +
        std::to_string(K_KNOWN_OPERATION.value) + ")";
    EXPECT_NE(warnings.front().find(expected), std::string::npos) << warnings.front();

    ASSERT_NE(fixed->offload_state(), nullptr);
    EXPECT_FALSE(fixed->offload_state()->offloadable);
    EXPECT_FALSE(dynamic_state.offloadable);
    EXPECT_TRUE(in_place->offload_state()->offloadable);

    // The local run still raises the buffer the node wrote, so a worker's copy goes stale.
    const std::uint32_t version = version_of(second_.data());
    dispatcher_.complete_local(*fixed);
    EXPECT_GT(version_of(second_.data()), version);
}
