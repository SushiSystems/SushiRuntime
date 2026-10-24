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
 * @brief Tests OffloadDispatcher: the attempt cap, refused sends, live lists and versions.
 * @see tests/extension/offload_coordination/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_level.hpp>
#include <SushiLog/log/log_record.hpp>

#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/cluster_load/cluster_statistics.hpp>
#include <SushiRuntime/cluster_load/offload_policy.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/placement/placement_rule.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>
#include <SushiRuntime/link_security/handshake.hpp>
#include <SushiRuntime/link_security/sha256.hpp>

#include <SushiRuntime/offload_coordination/held_nodes.hpp>
#include <SushiRuntime/offload_coordination/offload_dispatcher.hpp>
#include <SushiRuntime/offload_coordination/worker_directory.hpp>

using namespace SushiRuntime;

namespace OffloadDispatcherTests
{
    using namespace SushiRuntime::Distributed;

    namespace
    {
        /** @brief Names the worker every test registers. */
        constexpr PeerID K_WORKER{1};

        /** @brief Names the machine K_WORKER runs on. */
        constexpr const char* K_WORKER_NAME = "worker";

        /** @brief Names a second worker, which only a test of two rule sets registers. */
        constexpr PeerID K_SECOND_WORKER{2};

        /** @brief Names the machine K_SECOND_WORKER runs on. */
        constexpr const char* K_SECOND_WORKER_NAME = "second";

        /** @brief Names an operation the worker has a kernel for. */
        constexpr OperationID K_KNOWN_OPERATION{501};

        /** @brief Names an operation no worker has a kernel for. */
        constexpr OperationID K_ABSENT_OPERATION{502};

        /** @brief Holds the tasks the worker says it queues at most. */
        constexpr std::uint32_t K_WORKER_MAX_JOBS = 8;

        /** @brief Holds the elements of each buffer a test shares. */
        constexpr std::size_t K_BUFFER_ELEMENTS = 16;

        /** @brief Names the storage of one buffer of a test. */
        using Storage = std::array<float, K_BUFFER_ELEMENTS>;

        /** @brief Holds one frame a test transport was asked to send. */
        struct OfferedFrame
        {
            PeerID peer;                    /**< The peer the frame was for. */
            MessageType type;               /**< The type of the frame. */
            std::vector<std::byte> payload; /**< A copy of the payload. */
        };

        /**
         * @brief Records every send() and answers it with a status the test sets.
         */
        class RecordingTransport final : public Transport
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
                [[nodiscard]] SendStatus send(PeerID peer, MessageType type,
                                              span<const std::byte> payload) override
                {
                    OfferedFrame frame{peer, type, std::vector<std::byte>()};
                    frame.payload.assign(payload.begin(), payload.end());
                    offered.push_back(std::move(frame));
                    return answer;
                }

                /** @brief Refuses every stream; the dispatcher sends none. */
                [[nodiscard]] SendStatus send_stream(PeerID /*peer*/,
                                                     std::unique_ptr<FrameSource> /*source*/) override
                {
                    return SendStatus::Stopped;
                }

                /** @brief Does nothing; the transport holds no connection. */
                void close_peer(PeerID /*peer*/) noexcept override
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
        class FirstWorkerPolicy final : public OffloadPolicy
        {
            public:
                /** @brief Returns the first offered worker, or nothing when none was offered. */
                std::optional<int> pick_worker(const NodeOptions& /*task*/,
                                               span<const WorkerLoad> workers,
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

        /** @brief Returns a rule set whose one rule sends every node to the machine @p name. */
        std::shared_ptr<const PlacementRules> rules_naming(const char* name)
        {
            PlacementRule rule;
            rule.target.machine_name = name;
            auto rules = std::make_shared<PlacementRules>();
            EXPECT_TRUE(rules->add(std::move(rule)));
            return rules;
        }

        /** @brief Returns a rule set whose one rule, "pin", sends every node to K_WORKER_NAME. */
        std::shared_ptr<const PlacementRules> pin_rules(AbsentAction action)
        {
            PlacementRule rule;
            rule.name = "pin";
            rule.target.machine_name = K_WORKER_NAME;
            rule.if_absent.action = action;
            auto rules = std::make_shared<PlacementRules>();
            EXPECT_TRUE(rules->add(std::move(rule)));
            return rules;
        }

        /** @brief Records the errors a Fail rule ends a run with; it never resubmits. */
        class RecordingSink final : public Internal::Graph::OffloadSink
        {
            public:
                /** @brief Counts a resubmission, which no test here expects. */
                void resubmit(Internal::Graph::Node& /*node*/) override
                {
                    ++resubmitted;
                }

                /** @brief Counts a remote completion, which no test here expects. */
                void complete_remote(Internal::Graph::Node& /*node*/) override
                {
                    ++completed;
                }

                /** @brief Keeps the error a run ended with. */
                void fail(Internal::Graph::Node& /*node*/, Error error) override
                {
                    errors.push_back(std::move(error));
                }

                /** @brief Holds every error fail() was given, in order. */
                std::vector<Error> errors;

                /** @brief Counts the calls to resubmit(). */
                int resubmitted = 0;

                /** @brief Counts the calls to complete_remote(). */
                int completed = 0;
        };

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
        class Medium_OffloadDispatcherTest : public ::testing::Test
        {
            protected:
                /** @brief Registers the worker and shares two of the three buffers. */
                void SetUp() override
                {
                    saved_level_ = SushiLog::log_level(K_SUSHIRUNTIME_LOG_PROJECT);
                    SushiLog::set_log_synchronous(true);
                    SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Warn);

                    register_worker(K_WORKER, K_WORKER_NAME);
                    registry_.register_buffer(first_.data(), sizeof(Storage));
                    registry_.register_buffer(second_.data(), sizeof(Storage));
                }

                /** @brief Puts the default sink and the saved level back. */
                void TearDown() override
                {
                    SushiLog::set_log_sink(nullptr);
                    SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, saved_level_);
                }

                /** @brief Takes a worker through HELLO and PROOF under the empty secret. */
                void register_worker(PeerID peer, const char* name)
                {
                    const std::chrono::steady_clock::time_point now =
                        std::chrono::steady_clock::now();
                    HandshakeNonce worker_nonce{};
                    worker_nonce.bytes.fill(0x11);
                    HandshakeNonce master_nonce{};
                    master_nonce.bytes.fill(0x22);

                    directory_.add_peer(peer, "test:" + std::to_string(peer.value), now);

                    Wire::HelloMessage hello;
                    hello.worker_nonce = to_wire(worker_nonce.bytes);
                    hello.build_id = "test-build";
                    hello.node_name = name;
                    ASSERT_EQ(directory_.accept_hello(peer, hello, master_nonce).step,
                              HandshakeStep::Accepted);

                    Wire::ProofMessage proof;
                    proof.worker_proof = to_wire(compute_proof(
                        HandshakeRole::Worker, span<const std::uint8_t>(), worker_nonce,
                        master_nonce));
                    proof.ops = {K_KNOWN_OPERATION};
                    proof.max_jobs = K_WORKER_MAX_JOBS;
                    ASSERT_EQ(directory_.accept_proof(peer, proof, now), HandshakeStep::Accepted);
                }

                /** @brief Returns a node that reads @p read and writes @p written under @p op. */
                static sushi_ptr<Internal::Graph::Node> fixed_node(OperationID op, void* read, void* written)
                {
                    sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
                    node->metadata.op_id = op;
                    node->io_reads_.emplace_back(read);
                    node->io_writes_.emplace_back(written);
                    return node;
                }

                /** @brief Runs prepare() over the given nodes, under @p rules. */
                void prepare(const std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                             const std::shared_ptr<const PlacementRules>& rules = nullptr)
                {
                    dispatcher_.prepare(
                        span<const sushi_ptr<Internal::Graph::Node>>(nodes.data(), nodes.size()),
                        rules);
                }

                /**
                 * @brief Offers a node, takes it back from the pending table as a worker's
                 *        result would, and returns the peer its dispatch went to.
                 */
                PeerID offload_and_return(Internal::Graph::Node& node)
                {
                    const std::size_t sent = transport_.offered.size();
                    EXPECT_TRUE(dispatcher_.try_offload(node));
                    std::vector<PendingTask> taken;
                    pending_.drain(taken);
                    EXPECT_EQ(taken.size(), 1u);
                    dispatcher_.complete_remote(node);
                    EXPECT_EQ(transport_.offered.size(), sent + 1);
                    return transport_.offered.size() == sent + 1 ? transport_.offered.back().peer
                                                                 : PeerID{0};
                }

                /** @brief Sends the text of every WARN to warnings_ until TearDown(). */
                void capture_warnings()
                {
                    std::vector<std::string>* const sink_target = &warnings_;
                    SushiLog::set_log_sink([sink_target](const SushiLog::LogRecord& record)
                    {
                        if (record.level == SushiLog::LogLevel::Warn)
                            sink_target->push_back(std::string(record.text));
                    });
                }

                /** @brief Returns the WARNs that name the "pin" rule and contain @p reason. */
                std::size_t pin_warnings(const char* reason) const
                {
                    return static_cast<std::size_t>(std::count_if(
                        warnings_.begin(), warnings_.end(),
                        [reason](const std::string& text)
                        {
                            return text.find("\"pin\"") != std::string::npos &&
                                   text.find(reason) != std::string::npos;
                        }));
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

                /// @brief The table that holds a node a rule makes wait.
                HeldNodes held_;

                /// @brief The dispatcher under test, on a master named after no rule.
                OffloadDispatcher dispatcher_{
                    registry_,  directory_, pending_,
                    transport_, held_,      NodeFacts{"master", OperatingSystem::Unknown, {}, {}}};

                /// @brief The WARNs capture_warnings() collected.
                std::vector<std::string> warnings_;

                /// @brief The sink a test binds when a Fail rule must end a run.
                RecordingSink sink_;

                /// @brief The level the process had when the test began.
                SushiLog::LogLevel saved_level_ = SushiLog::LogLevel::Warn;
        };
    } // namespace

    /** @brief Tests that a node at the attempt cap is declined and offered again next run. */
    TEST_F(Medium_OffloadDispatcherTest, AttemptCapDeclinesWithoutClearingOffloadable)
    {
        const sushi_ptr<Internal::Graph::Node> node =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        prepare({node});
        Internal::Graph::OffloadState* state = node->offload_state();
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
    TEST_F(Medium_OffloadDispatcherTest, RefusedSendUndoesThePendingEntry)
    {
        const sushi_ptr<Internal::Graph::Node> node =
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
    TEST_F(Medium_OffloadDispatcherTest, StoppedCoordinatorDeclines)
    {
        const sushi_ptr<Internal::Graph::Node> node =
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
    TEST_F(Medium_OffloadDispatcherTest, DynamicNodeResolvesItsLiveLists)
    {
        const sushi_ptr<Internal::Graph::Node> node =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        Internal::Graph::OffloadState& state = node->offload();
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
    TEST_F(Medium_OffloadDispatcherTest, PrepareWarnsOnceForLocalOnlyOps)
    {
        std::vector<std::string> warnings;
        std::vector<std::string>* const sink_target = &warnings;
        SushiLog::set_log_sink([sink_target](const SushiLog::LogRecord& record)
        {
            if (record.level == SushiLog::LogLevel::Warn)
                sink_target->push_back(std::string(record.text));
        });

        const sushi_ptr<Internal::Graph::Node> offloadable =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        const sushi_ptr<Internal::Graph::Node> without_worker =
            fixed_node(K_ABSENT_OPERATION, first_.data(), second_.data());
        const sushi_ptr<Internal::Graph::Node> without_share =
            fixed_node(K_KNOWN_OPERATION, first_.data(), unshared_.data());
        const sushi_ptr<Internal::Graph::Node> untagged =
            fixed_node(OperationID(0), first_.data(), unshared_.data());

        prepare({offloadable, without_worker, without_share, untagged, sushi_ptr<Internal::Graph::Node>()});

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
    TEST_F(Medium_OffloadDispatcherTest, LocalNodeBumpsOnlyItsSharedWrites)
    {
        const sushi_ptr<Internal::Graph::Node> writer =
            fixed_node(OperationID(0), unshared_.data(), second_.data());
        const sushi_ptr<Internal::Graph::Node> reader =
            fixed_node(OperationID(0), second_.data(), unshared_.data());
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
    TEST_F(Medium_OffloadDispatcherTest, NodeThatWritesOneBufferTwiceStaysLocal)
    {
        std::vector<std::string> warnings;
        std::vector<std::string>* const sink_target = &warnings;
        SushiLog::set_log_sink([sink_target](const SushiLog::LogRecord& record)
        {
            if (record.level == SushiLog::LogLevel::Warn)
                sink_target->push_back(std::string(record.text));
        });

        const sushi_ptr<Internal::Graph::Node> fixed =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        fixed->io_writes_.emplace_back(second_.data());

        const sushi_ptr<Internal::Graph::Node> dynamic =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        Internal::Graph::OffloadState& dynamic_state = dynamic->offload();
        dynamic_state.live_reads =
            std::make_shared<std::vector<void*>>(1, static_cast<void*>(first_.data()));
        dynamic_state.live_writes =
            std::make_shared<std::vector<void*>>(2, static_cast<void*>(second_.data()));

        // The same buffer as an input and as an output is one write, so this node may leave.
        const sushi_ptr<Internal::Graph::Node> in_place =
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

    /**
     * @brief Tests that two plans prepared under different rule sets and run alternately each
     *        place their node by their own set, not by the set of the later prepare().
     */
    TEST_F(Medium_OffloadDispatcherTest, Regression_EachPlanPlacesByItsOwnRules)
    {
        register_worker(K_SECOND_WORKER, K_SECOND_WORKER_NAME);

        const sushi_ptr<Internal::Graph::Node> first_plan =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        first_plan->metadata.placement_rule = 0;
        prepare({first_plan}, rules_naming(K_WORKER_NAME));

        const sushi_ptr<Internal::Graph::Node> second_plan =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        second_plan->metadata.placement_rule = 0;
        prepare({second_plan}, rules_naming(K_SECOND_WORKER_NAME));

        for (int round = 0; round < 2; ++round)
        {
            EXPECT_EQ(offload_and_return(*first_plan), K_WORKER) << "round " << round;
            EXPECT_EQ(offload_and_return(*second_plan), K_SECOND_WORKER) << "round " << round;
        }
        EXPECT_EQ(policy_->calls, 0);
    }

    /** @brief Tests that a Fail rule naming a worker ends the run of an untagged node. */
    TEST_F(Medium_OffloadDispatcherTest, FailRuleRefusesAnUntaggedNodeNamingTheRule)
    {
        dispatcher_.bind(sink_);
        const sushi_ptr<Internal::Graph::Node> node =
            fixed_node(OperationID(0), first_.data(), second_.data());
        node->metadata.placement_rule = 0;
        prepare({node}, pin_rules(AbsentAction::Fail));
        ASSERT_NE(node->offload_state(), nullptr);
        EXPECT_TRUE(node->offload_state()->offloadable);

        EXPECT_TRUE(dispatcher_.try_offload(*node));
        ASSERT_EQ(sink_.errors.size(), 1u);
        EXPECT_EQ(sink_.errors.front().code, ErrorCode::InvalidArgument);
        EXPECT_NE(sink_.errors.front().message.find("\"pin\""), std::string::npos)
            << sink_.errors.front().message;
        EXPECT_NE(sink_.errors.front().message.find("names no operation"), std::string::npos)
            << sink_.errors.front().message;
        EXPECT_TRUE(transport_.offered.empty());
    }

    /**
     * @brief Tests that a Local rule naming a worker runs nodes that cannot travel on the master
     *        and logs one WARN per rule and reason.
     */
    TEST_F(Medium_OffloadDispatcherTest, LocalRuleRunsNodesThatCannotTravelAndWarnsOnce)
    {
        capture_warnings();
        dispatcher_.bind(sink_);
        const sushi_ptr<Internal::Graph::Node> untagged =
            fixed_node(OperationID(0), first_.data(), second_.data());
        const sushi_ptr<Internal::Graph::Node> also_untagged =
            fixed_node(OperationID(0), second_.data(), first_.data());
        const sushi_ptr<Internal::Graph::Node> not_shared =
            fixed_node(K_KNOWN_OPERATION, first_.data(), unshared_.data());
        for (const sushi_ptr<Internal::Graph::Node>& node : {untagged, also_untagged, not_shared})
            node->metadata.placement_rule = 0;
        prepare({untagged, also_untagged, not_shared}, pin_rules(AbsentAction::Local));

        for (int round = 0; round < 2; ++round)
            for (const sushi_ptr<Internal::Graph::Node>& node :
                 {untagged, also_untagged, not_shared})
            {
                ASSERT_NE(node->offload_state(), nullptr);
                EXPECT_FALSE(dispatcher_.try_offload(*node)) << "round " << round;
                dispatcher_.complete_local(*node);
            }

        EXPECT_EQ(pin_warnings("names no operation"), 1u);
        EXPECT_EQ(pin_warnings("not shared"), 1u);
        EXPECT_TRUE(sink_.errors.empty());
        EXPECT_TRUE(transport_.offered.empty());
    }

    /** @brief Tests that a rule-placed node out of attempts follows the rule's absent policy. */
    TEST_F(Medium_OffloadDispatcherTest, RulePlacedNodeOutOfAttemptsFollowsItsPolicy)
    {
        capture_warnings();
        dispatcher_.bind(sink_);
        const sushi_ptr<Internal::Graph::Node> failing =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        failing->metadata.placement_rule = 0;
        prepare({failing}, pin_rules(AbsentAction::Fail));
        failing->offload_state()->attempts.store(K_MAXIMUM_OFFLOAD_ATTEMPTS);
        EXPECT_TRUE(dispatcher_.try_offload(*failing));
        ASSERT_EQ(sink_.errors.size(), 1u);
        EXPECT_EQ(sink_.errors.front().code, ErrorCode::InvalidState);
        EXPECT_NE(sink_.errors.front().message.find("\"pin\""), std::string::npos)
            << sink_.errors.front().message;

        const sushi_ptr<Internal::Graph::Node> local =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        local->metadata.placement_rule = 0;
        prepare({local}, pin_rules(AbsentAction::Wait));
        local->offload_state()->attempts.store(K_MAXIMUM_OFFLOAD_ATTEMPTS);
        EXPECT_FALSE(dispatcher_.try_offload(*local));
        EXPECT_EQ(held_.size(), 0u);
        EXPECT_EQ(pin_warnings("every dispatch"), 1u);
        EXPECT_EQ(sink_.errors.size(), 1u);
        EXPECT_TRUE(transport_.offered.empty());
    }

    /** @brief Tests that a node no rule places is declined at the cap and never refused. */
    TEST_F(Medium_OffloadDispatcherTest, NodeWithoutARuleOutOfAttemptsIsDeclined)
    {
        capture_warnings();
        dispatcher_.bind(sink_);
        const sushi_ptr<Internal::Graph::Node> node =
            fixed_node(K_KNOWN_OPERATION, first_.data(), second_.data());
        const sushi_ptr<Internal::Graph::Node> untagged =
            fixed_node(OperationID(0), first_.data(), second_.data());
        prepare({node, untagged}, pin_rules(AbsentAction::Fail));
        EXPECT_FALSE(untagged->offload_state()->offloadable);

        node->offload_state()->attempts.store(K_MAXIMUM_OFFLOAD_ATTEMPTS);
        EXPECT_FALSE(dispatcher_.try_offload(*node));
        EXPECT_TRUE(sink_.errors.empty());
        EXPECT_TRUE(warnings_.empty());
        EXPECT_EQ(dispatcher_.statistics().declined, 1u);
    }
} // namespace OffloadDispatcherTests
