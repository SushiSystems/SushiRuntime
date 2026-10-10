/**************************************************************************/
/* test_worker_directory.cpp                                              */
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
 * @file test_worker_directory.cpp
 * @brief Tests WorkerDirectory: the handshake decisions, the peer gate, liveness and pick().
 * @see modules/extension/distributed/tests/README.md
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
#include <thread>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/cluster_statistics.hpp>
#include <SushiRuntime/distributed/i_offload_policy.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/message_type.hpp>

#include "handshake.hpp"
#include "sha256.hpp"
#include "wire_messages.hpp"
#include "worker_directory.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    using std::chrono::milliseconds;

    /** @brief Names the first peer of a test. */
    constexpr PeerId K_FIRST{1};

    /** @brief Names the second peer of a test. */
    constexpr PeerId K_SECOND{2};

    /** @brief Names the third peer of a test. */
    constexpr PeerId K_THIRD{3};

    /** @brief Names a peer no test adds to the directory. */
    constexpr PeerId K_STRANGER{9};

    /** @brief Holds a wait no test outlasts; a wake that never comes hangs the test. */
    constexpr milliseconds K_UNTIL_WOKEN = std::chrono::hours(24);

    /** @brief Names an operation only the first worker of a test runs. */
    constexpr Graph::OpID K_RARE_OPERATION{101};

    /** @brief Names an operation every worker of a test runs. */
    constexpr Graph::OpID K_COMMON_OPERATION{202};

    /** @brief Names an operation no worker of a test runs. */
    constexpr Graph::OpID K_ABSENT_OPERATION{303};

    /** @brief Returns a fixed time point @p offset after the tests' zero. */
    std::chrono::steady_clock::time_point at(milliseconds offset)
    {
        return std::chrono::steady_clock::time_point(std::chrono::hours(1)) + offset;
    }

    /** @brief Returns a nonce whose bytes all hold @p value. */
    HandshakeNonce nonce_of(std::uint8_t value)
    {
        HandshakeNonce nonce{};
        nonce.bytes.fill(value);
        return nonce;
    }

    /** @brief Returns handshake octets as the bytes a wire message carries. */
    template <std::size_t Count>
    std::array<std::byte, Count> to_wire(const std::array<std::uint8_t, Count>& octets)
    {
        std::array<std::byte, Count> bytes{};
        std::transform(octets.begin(), octets.end(), bytes.begin(),
                       [](std::uint8_t octet) { return std::byte{octet}; });
        return bytes;
    }

    /** @brief Returns the HELLO of a worker called "worker" with the given nonce. */
    Wire::HelloMessage hello_with(const HandshakeNonce& worker_nonce)
    {
        Wire::HelloMessage hello;
        hello.worker_nonce = to_wire(worker_nonce.bytes);
        hello.build_id = "test-build";
        hello.node_name = "worker";
        return hello;
    }

    /** @brief Returns the PROOF a worker holding @p secret sends for the two nonces. */
    Wire::ProofMessage proof_with(span<const std::uint8_t> secret,
                                  const HandshakeNonce& worker_nonce,
                                  const HandshakeNonce& master_nonce,
                                  std::vector<Graph::OpID> operations, std::uint32_t max_jobs)
    {
        Wire::ProofMessage proof;
        proof.worker_proof = to_wire(
            compute_proof(HandshakeRole::Worker, secret, worker_nonce, master_nonce));
        proof.ops = std::move(operations);
        proof.max_jobs = max_jobs;
        return proof;
    }

    /** @brief Returns the type bytes of the messages the directory admits from a peer. */
    std::vector<int> admitted_types(const WorkerDirectory& directory, PeerId peer)
    {
        std::vector<int> admitted;
        for (int type = K_FIRST_MESSAGE_TYPE; type <= K_LAST_MESSAGE_TYPE; ++type)
        {
            if (directory.admits(peer, static_cast<MessageType>(type)))
                admitted.push_back(type);
        }
        return admitted;
    }

    /** @brief Returns the type bytes of the given messages. */
    std::vector<int> type_bytes(const std::vector<MessageType>& types)
    {
        std::vector<int> bytes;
        for (const MessageType type : types)
            bytes.push_back(static_cast<int>(type));
        return bytes;
    }

    /** @brief Returns the values of the given peer ids. */
    std::vector<std::uint32_t> values_of(const std::vector<PeerId>& peers)
    {
        std::vector<std::uint32_t> values;
        for (const PeerId peer : peers)
            values.push_back(peer.value);
        return values;
    }

    /** @brief Picks the first worker it is offered and records every call. */
    class RecordingPolicy final : public IOffloadPolicy
    {
        public:
            /** @brief Records the offer, then returns @ref answer or the first worker. */
            std::optional<int> pick_worker(const Graph::Node&, span<const WorkerStats> workers,
                                           std::uint64_t) override
            {
                ++calls;
                offered.assign(workers.begin(), workers.end());
                if (keeps_local)
                    return std::nullopt;
                if (answer.has_value())
                    return answer;
                return workers.front().worker_id;
            }

            /** @brief Returns the ids of the workers of the last call. */
            [[nodiscard]] std::vector<int> offered_ids() const
            {
                std::vector<int> ids;
                for (const WorkerStats& worker : offered)
                    ids.push_back(worker.worker_id);
                return ids;
            }

            /** @brief Counts the calls. */
            int calls = 0;

            /** @brief Holds the workers of the last call. */
            std::vector<WorkerStats> offered;

            /** @brief Holds the id to answer with in place of the first worker. */
            std::optional<int> answer;

            /** @brief Makes every call keep the task local. */
            bool keeps_local = false;
    };

    /** @brief Builds a directory over a 32-byte secret and a recording policy. */
    class Unit_WorkerDirectory : public ::testing::Test
    {
        protected:
            /** @brief Fills the secret with the bytes 0 to 31 and builds the directory. */
            Unit_WorkerDirectory()
                : secret_(make_secret()),
                  policy_(std::make_shared<RecordingPolicy>()),
                  directory_(secret(), policy_)
            {

            }

            /** @brief Returns the secret the directory verifies proofs with. */
            [[nodiscard]] span<const std::uint8_t> secret() const noexcept
            {
                return span<const std::uint8_t>(secret_.data(), secret_.size());
            }

            /** @brief Connects a peer and takes it through HELLO and PROOF at time @p now. */
            HandshakeStep register_worker(PeerId peer, std::vector<Graph::OpID> operations,
                                          std::uint32_t max_jobs,
                                          std::chrono::steady_clock::time_point now)
            {
                const HandshakeNonce worker_nonce =
                    nonce_of(static_cast<std::uint8_t>(0x10 + peer.value));
                const HandshakeNonce master_nonce =
                    nonce_of(static_cast<std::uint8_t>(0x80 + peer.value));

                directory_.add_peer(peer, "10.0.0." + std::to_string(peer.value) + ":7000", now);
                const HelloReply reply =
                    directory_.accept_hello(peer, hello_with(worker_nonce), master_nonce);
                if (reply.step != HandshakeStep::Accepted)
                    return reply.step;

                return directory_.accept_proof(
                    peer,
                    proof_with(secret(), worker_nonce, master_nonce, std::move(operations),
                               max_jobs),
                    now);
            }

            /** @brief Holds the cluster key. */
            std::vector<std::uint8_t> secret_;

            /** @brief Holds the policy the directory asks. */
            std::shared_ptr<RecordingPolicy> policy_;

            /** @brief Holds the directory under test. */
            WorkerDirectory directory_;

        private:
            /** @brief Returns the bytes 0 to 31. */
            static std::vector<std::uint8_t> make_secret()
            {
                std::vector<std::uint8_t> bytes(32);
                for (std::size_t index = 0; index < bytes.size(); ++index)
                    bytes[index] = static_cast<std::uint8_t>(index);
                return bytes;
            }
    };
} // namespace

TEST_F(Unit_WorkerDirectory, UnregisteredPeerMessagesDropped)
{
    const HandshakeNonce worker_nonce = nonce_of(0x11);
    const HandshakeNonce master_nonce = nonce_of(0x22);

    EXPECT_TRUE(admitted_types(directory_, K_STRANGER).empty());

    directory_.add_peer(K_FIRST, "10.0.0.1:7000", at(milliseconds(0)));
    EXPECT_EQ(admitted_types(directory_, K_FIRST),
              type_bytes({MessageType::Hello, MessageType::Goodbye}));

    ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(worker_nonce), master_nonce).step,
              HandshakeStep::Accepted);
    EXPECT_EQ(admitted_types(directory_, K_FIRST),
              type_bytes({MessageType::Hello, MessageType::Proof, MessageType::Goodbye}));
    EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::BufferRequest));
    EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::BufferChunk));
    EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::TaskResult));

    ASSERT_EQ(directory_.accept_proof(
                  K_FIRST, proof_with(secret(), worker_nonce, master_nonce, {}, 4),
                  at(milliseconds(1))),
              HandshakeStep::Accepted);

    std::vector<int> every_type_but_the_handshake;
    for (int type = K_FIRST_MESSAGE_TYPE; type <= K_LAST_MESSAGE_TYPE; ++type)
    {
        if (type != static_cast<int>(MessageType::Hello) &&
            type != static_cast<int>(MessageType::Proof))
            every_type_but_the_handshake.push_back(type);
    }
    EXPECT_EQ(admitted_types(directory_, K_FIRST), every_type_but_the_handshake);

    const RemovedPeer removed = directory_.remove_peer(K_FIRST);
    EXPECT_TRUE(removed.known);
    EXPECT_TRUE(admitted_types(directory_, K_FIRST).empty());
}

TEST_F(Unit_WorkerDirectory, HandshakeDeadlineClosesPeer)
{
    const milliseconds limit(5000);

    directory_.add_peer(K_FIRST, "10.0.0.1:7000", at(milliseconds(0)));
    ASSERT_EQ(register_worker(K_SECOND, {}, 4, at(milliseconds(0))), HandshakeStep::Accepted);
    directory_.add_peer(K_THIRD, "10.0.0.3:7000", at(milliseconds(400)));

    // A peer that keeps sending HELLO still owes PROOF, and its time runs from the connect.
    ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(nonce_of(1)), nonce_of(2)).step,
              HandshakeStep::Accepted);
    ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(nonce_of(3)), nonce_of(4)).step,
              HandshakeStep::Accepted);

    std::vector<PeerId> overdue;
    directory_.take_handshake_overdue(at(milliseconds(5000)), limit, overdue);
    EXPECT_TRUE(overdue.empty()) << "the limit itself is still within the limit";

    directory_.take_handshake_overdue(at(milliseconds(5001)), limit, overdue);
    EXPECT_EQ(values_of(overdue), (std::vector<std::uint32_t>{1}));

    directory_.take_handshake_overdue(at(milliseconds(5400)), limit, overdue);
    EXPECT_EQ(values_of(overdue), (std::vector<std::uint32_t>{1}));

    directory_.take_handshake_overdue(at(milliseconds(5401)), limit, overdue);
    EXPECT_EQ(values_of(overdue), (std::vector<std::uint32_t>{1, 3}));

    directory_.take_handshake_overdue(at(milliseconds(900000)), limit, overdue);
    EXPECT_EQ(overdue.size(), 2u) << "each peer is listed once and a worker never";

    const RemovedPeer removed = directory_.remove_peer(K_FIRST);
    EXPECT_TRUE(removed.known);
    EXPECT_FALSE(removed.registered);
    EXPECT_EQ(removed.name, "worker");
    EXPECT_EQ(removed.endpoint, "10.0.0.1:7000");
    EXPECT_FALSE(directory_.remove_peer(K_FIRST).known);
    EXPECT_EQ(directory_.alive_count(), 1u);
}

TEST_F(Unit_WorkerDirectory, WaitForWorkersWakesOnRegistration)
{
    EXPECT_TRUE(directory_.wait_for_workers(0, milliseconds(0)));
    EXPECT_FALSE(directory_.wait_for_workers(1, milliseconds(0)));

    std::atomic<bool> reached{false};
    std::thread waiter([&] { reached = directory_.wait_for_workers(1, K_UNTIL_WOKEN); });
    const HandshakeStep step = register_worker(K_FIRST, {}, 4, at(milliseconds(0)));
    waiter.join();

    EXPECT_EQ(step, HandshakeStep::Accepted);
    EXPECT_TRUE(reached.load());
    EXPECT_TRUE(directory_.wait_for_workers(1, milliseconds(0)));

    std::atomic<bool> reached_two{true};
    std::thread second_waiter(
        [&] { reached_two = directory_.wait_for_workers(2, K_UNTIL_WOKEN); });
    directory_.close();
    second_waiter.join();

    EXPECT_FALSE(reached_two.load()) << "close() wakes a waiter whose count was not reached";
}

TEST_F(Unit_WorkerDirectory, WrongProofIsRejected)
{
    const HandshakeNonce worker_nonce = nonce_of(0x11);
    const HandshakeNonce master_nonce = nonce_of(0x22);
    const std::vector<std::uint8_t> other_secret(32, 0xee);
    const span<const std::uint8_t> other(other_secret.data(), other_secret.size());

    directory_.add_peer(K_FIRST, "10.0.0.1:7000", at(milliseconds(0)));
    const HelloReply reply =
        directory_.accept_hello(K_FIRST, hello_with(worker_nonce), master_nonce);
    ASSERT_EQ(reply.step, HandshakeStep::Accepted);
    EXPECT_EQ(reply.master_proof,
              compute_proof(HandshakeRole::Master, secret(), worker_nonce, master_nonce));

    EXPECT_EQ(directory_.accept_proof(
                  K_FIRST, proof_with(other, worker_nonce, master_nonce, {}, 4),
                  at(milliseconds(1))),
              HandshakeStep::AuthenticationFailed);
    EXPECT_EQ(directory_.alive_count(), 0u);
    EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::TaskResult));
    EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::Proof));

    // The refused proof spent the nonces, so the right proof for them comes too late.
    EXPECT_EQ(directory_.accept_proof(
                  K_FIRST, proof_with(secret(), worker_nonce, master_nonce, {}, 4),
                  at(milliseconds(2))),
              HandshakeStep::OutOfOrder);

    const HelloReply second =
        directory_.accept_hello(K_FIRST, hello_with(worker_nonce), master_nonce);
    ASSERT_EQ(second.step, HandshakeStep::Accepted);
    Wire::ProofMessage replayed;
    replayed.worker_proof = to_wire(second.master_proof);
    replayed.max_jobs = 4;
    EXPECT_EQ(directory_.accept_proof(K_FIRST, replayed, at(milliseconds(3))),
              HandshakeStep::AuthenticationFailed)
        << "the master's own proof does not pass as the worker's";

    ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(worker_nonce), master_nonce).step,
              HandshakeStep::Accepted);
    EXPECT_EQ(directory_.accept_proof(
                  K_FIRST,
                  proof_with(span<const std::uint8_t>(), worker_nonce, master_nonce, {}, 4),
                  at(milliseconds(4))),
              HandshakeStep::AuthenticationFailed)
        << "a worker without the secret file proves under an empty key";

    EXPECT_EQ(directory_.alive_count(), 0u);
    EXPECT_FALSE(directory_.remove_peer(K_FIRST).registered);
}

TEST_F(Unit_WorkerDirectory, SecondHelloRestartsTheHandshake)
{
    const HandshakeNonce first_worker_nonce = nonce_of(0x11);
    const HandshakeNonce first_master_nonce = nonce_of(0x22);
    const HandshakeNonce second_worker_nonce = nonce_of(0x33);
    const HandshakeNonce second_master_nonce = nonce_of(0x44);

    EXPECT_EQ(directory_.accept_hello(K_STRANGER, hello_with(first_worker_nonce),
                                      first_master_nonce).step,
              HandshakeStep::UnknownPeer);
    EXPECT_EQ(directory_.accept_proof(
                  K_STRANGER,
                  proof_with(secret(), first_worker_nonce, first_master_nonce, {}, 4),
                  at(milliseconds(0))),
              HandshakeStep::UnknownPeer);

    directory_.add_peer(K_FIRST, "10.0.0.1:7000", at(milliseconds(0)));
    directory_.add_peer(K_SECOND, "10.0.0.2:7000", at(milliseconds(0)));
    for (const PeerId peer : {K_FIRST, K_SECOND})
    {
        const HelloReply first =
            directory_.accept_hello(peer, hello_with(first_worker_nonce), first_master_nonce);
        const HelloReply second =
            directory_.accept_hello(peer, hello_with(second_worker_nonce), second_master_nonce);
        ASSERT_EQ(first.step, HandshakeStep::Accepted);
        ASSERT_EQ(second.step, HandshakeStep::Accepted);
        EXPECT_NE(first.master_proof, second.master_proof);
        EXPECT_EQ(second.master_proof,
                  compute_proof(HandshakeRole::Master, secret(), second_worker_nonce,
                                second_master_nonce));
    }

    EXPECT_EQ(directory_.accept_proof(
                  K_FIRST,
                  proof_with(secret(), first_worker_nonce, first_master_nonce, {}, 4),
                  at(milliseconds(1))),
              HandshakeStep::AuthenticationFailed)
        << "a proof for the nonces of the first HELLO";
    EXPECT_EQ(directory_.accept_proof(
                  K_SECOND,
                  proof_with(secret(), second_worker_nonce, second_master_nonce, {}, 4),
                  at(milliseconds(1))),
              HandshakeStep::Accepted);
    EXPECT_EQ(directory_.alive_count(), 1u);

    // A registered worker owes no handshake frame, and sending one costs it nothing.
    EXPECT_EQ(directory_.accept_hello(K_SECOND, hello_with(first_worker_nonce),
                                      first_master_nonce).step,
              HandshakeStep::OutOfOrder);
    EXPECT_EQ(directory_.accept_proof(
                  K_SECOND,
                  proof_with(secret(), second_worker_nonce, second_master_nonce, {}, 4),
                  at(milliseconds(2))),
              HandshakeStep::OutOfOrder);
    EXPECT_EQ(directory_.alive_count(), 1u);
    EXPECT_TRUE(directory_.admits(K_SECOND, MessageType::TaskResult));
}

TEST_F(Unit_WorkerDirectory, SilentWorkerIsTakenOnce)
{
    const sushi_ptr<Graph::Node> node = make_sushi<Graph::Node>();
    const milliseconds liveness(500);

    ASSERT_EQ(register_worker(K_FIRST, {K_RARE_OPERATION, K_COMMON_OPERATION}, 4,
                              at(milliseconds(0))),
              HandshakeStep::Accepted);
    ASSERT_EQ(register_worker(K_SECOND, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
              HandshakeStep::Accepted);
    EXPECT_EQ(directory_.alive_count(), 2u);
    EXPECT_TRUE(directory_.supports(K_RARE_OPERATION));
    EXPECT_TRUE(directory_.supports(K_COMMON_OPERATION));
    EXPECT_FALSE(directory_.supports(K_ABSENT_OPERATION));

    std::vector<PeerId> silent;
    directory_.take_silent(at(milliseconds(500)), liveness, silent);
    EXPECT_TRUE(silent.empty()) << "the limit itself is still within the limit";

    directory_.note_frame(K_SECOND, at(milliseconds(900)));
    directory_.take_silent(at(milliseconds(1000)), liveness, silent);
    EXPECT_EQ(values_of(silent), (std::vector<std::uint32_t>{1}));
    EXPECT_EQ(directory_.alive_count(), 1u);

    directory_.take_silent(at(milliseconds(1000)), liveness, silent);
    EXPECT_EQ(silent.size(), 1u) << "a worker already taken is not listed again";

    std::vector<PeerId> workers;
    directory_.list_workers(workers);
    EXPECT_EQ(values_of(workers), (std::vector<std::uint32_t>{2}));
    EXPECT_FALSE(directory_.supports(K_RARE_OPERATION));
    EXPECT_TRUE(directory_.supports(K_COMMON_OPERATION));

    const std::optional<PeerId> picked = directory_.pick(*node, 1024);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(picked->value, 2u);
    EXPECT_EQ(policy_->offered_ids(), (std::vector<int>{2}));

    // A late frame does not bring the worker back; only a new connection registers.
    directory_.note_frame(K_FIRST, at(milliseconds(1001)));
    EXPECT_EQ(directory_.alive_count(), 1u);

    const RemovedPeer removed = directory_.remove_peer(K_FIRST);
    EXPECT_TRUE(removed.known);
    EXPECT_TRUE(removed.registered);
    EXPECT_EQ(directory_.alive_count(), 1u) << "the worker stopped counting when it was taken";

    directory_.take_silent(at(milliseconds(1401)), liveness, silent);
    EXPECT_EQ(values_of(silent), (std::vector<std::uint32_t>{1, 2}));
    EXPECT_EQ(directory_.alive_count(), 0u);
    EXPECT_FALSE(directory_.supports(K_COMMON_OPERATION));
    EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
}

TEST_F(Unit_WorkerDirectory, PickSkipsWorkersAtTheirJobLimit)
{
    const sushi_ptr<Graph::Node> node = make_sushi<Graph::Node>();

    EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
    EXPECT_EQ(policy_->calls, 0) << "the policy is not asked when no worker has room";

    ASSERT_EQ(register_worker(K_FIRST, {}, 1, at(milliseconds(0))), HandshakeStep::Accepted);
    ASSERT_EQ(register_worker(K_SECOND, {}, 2, at(milliseconds(0))), HandshakeStep::Accepted);

    std::optional<PeerId> picked = directory_.pick(*node, 1024);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(picked->value, 1u);
    EXPECT_EQ(policy_->offered_ids(), (std::vector<int>{1, 2}));

    picked = directory_.pick(*node, 1024);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(picked->value, 2u);
    EXPECT_EQ(policy_->offered_ids(), (std::vector<int>{2}));
    ASSERT_EQ(policy_->offered.size(), 1u);
    EXPECT_TRUE(policy_->offered[0].alive);
    EXPECT_EQ(policy_->offered[0].inflight, 0u);

    picked = directory_.pick(*node, 1024);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(picked->value, 2u);
    EXPECT_EQ(policy_->offered[0].inflight, 1u);
    EXPECT_EQ(policy_->calls, 3);

    EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
    EXPECT_EQ(policy_->calls, 3) << "both workers are at the limit they sent in PROOF";

    directory_.release_inflight(K_FIRST);
    directory_.release_inflight(K_FIRST);
    picked = directory_.pick(*node, 1024);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(picked->value, 1u) << "one release frees one slot, and a second lowers nothing";
    EXPECT_FALSE(directory_.pick(*node, 1024).has_value());

    // An answer the policy was not offered counts as no pick and raises no count.
    directory_.release_inflight(K_SECOND);
    policy_->answer = 1;
    EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
    policy_->answer = 77;
    EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
    policy_->answer.reset();

    policy_->keeps_local = true;
    EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
    policy_->keeps_local = false;

    directory_.close();
    const int calls_before_close = policy_->calls;
    EXPECT_FALSE(directory_.pick(*node, 1024).has_value());
    EXPECT_EQ(policy_->calls, calls_before_close) << "a closed directory asks nobody";
}

TEST_F(Unit_WorkerDirectory, SnapshotReportsWhatTheWorkerSent)
{
    const sushi_ptr<Graph::Node> node = make_sushi<Graph::Node>();

    directory_.add_peer(K_FIRST, "10.0.0.1:7000", at(milliseconds(0)));
    ASSERT_EQ(register_worker(K_SECOND, {K_COMMON_OPERATION}, 4, at(milliseconds(100))),
              HandshakeStep::Accepted);

    directory_.note_pong(K_SECOND, 250, 4096);
    directory_.note_heartbeat(K_SECOND, 3, 8192);
    directory_.note_frame(K_SECOND, at(milliseconds(400)));
    directory_.note_pong(K_FIRST, 1, 1);
    ASSERT_TRUE(directory_.pick(*node, 1024).has_value());

    std::vector<WorkerSnapshot> workers;
    directory_.snapshot(at(milliseconds(1000)), workers);

    ASSERT_EQ(workers.size(), 1u) << "a peer that has not finished the handshake is no worker";
    EXPECT_EQ(workers[0].id, 2u);
    EXPECT_EQ(workers[0].name, "worker");
    EXPECT_EQ(workers[0].endpoint, "10.0.0.2:7000");
    EXPECT_EQ(workers[0].inflight, 1u);
    EXPECT_EQ(workers[0].rtt_micros, 250u);
    EXPECT_EQ(workers[0].free_mem, 4096u);
    EXPECT_EQ(workers[0].queue_depth, 3u);
    EXPECT_EQ(workers[0].cache_bytes, 8192u);
    EXPECT_EQ(workers[0].ms_since_last_frame, 600u);
}
