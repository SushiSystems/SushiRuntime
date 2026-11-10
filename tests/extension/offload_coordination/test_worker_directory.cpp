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
 * @file test_worker_directory.cpp
 * @brief Tests WorkerDirectory: the handshake, deadlines, liveness, picks and snapshots.
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
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/placement/placement_rule.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/cluster_load/cluster_statistics.hpp>
#include <SushiRuntime/cluster_load/offload_policy.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>
#include <SushiRuntime/link_security/handshake.hpp>
#include <SushiRuntime/link_security/sha256.hpp>

#include <SushiRuntime/offload_coordination/worker_directory.hpp>

using namespace SushiRuntime;

namespace WorkerDirectoryTests
{
    using namespace SushiRuntime::Distributed;

    namespace
    {
        using std::chrono::milliseconds;

        /** @brief Names the first peer of a test. */
        constexpr PeerID K_FIRST{1};

        /** @brief Names the second peer of a test. */
        constexpr PeerID K_SECOND{2};

        /** @brief Names the third peer of a test. */
        constexpr PeerID K_THIRD{3};

        /** @brief Names a peer no test adds to the directory. */
        constexpr PeerID K_STRANGER{9};

        /** @brief Holds a wait no test outlasts; a wake that never comes hangs the test. */
        constexpr milliseconds K_UNTIL_WOKEN = std::chrono::hours(24);

        /** @brief Holds the time a handshake may take before its peer is overdue. */
        constexpr milliseconds K_HANDSHAKE_LIMIT(5000);

        /** @brief Holds the silence after which a worker is taken. */
        constexpr milliseconds K_LIVENESS(500);

        /** @brief Names an operation only the first worker of a test runs. */
        constexpr OperationID K_RARE_OPERATION{101};

        /** @brief Names an operation every worker of a test runs. */
        constexpr OperationID K_COMMON_OPERATION{202};

        /** @brief Names an operation no worker of a test runs. */
        constexpr OperationID K_ABSENT_OPERATION{303};

        /** @brief Holds the input size below which the policy of the tests keeps a task local. */
        constexpr std::uint64_t K_MIN_OFFLOAD_BYTES = 512;

        /** @brief Holds an input size the policy of the tests offers. */
        constexpr std::uint64_t K_TASK_BYTES = 1024;

        /** @brief Holds an input size the policy of the tests keeps local. */
        constexpr std::uint64_t K_LOCAL_BYTES = 511;

        /** @brief Builds a node tagged with the operation every worker of these tests holds. */
        sushi_ptr<Internal::Graph::Node> common_node()
        {
            sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
            node->metadata.op_id = K_COMMON_OPERATION;
            return node;
        }

        /** @brief Builds a node tagged with the operation only the first worker holds. */
        sushi_ptr<Internal::Graph::Node> rare_node()
        {
            sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
            node->metadata.op_id = K_RARE_OPERATION;
            return node;
        }

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

        /** @brief Returns the nonce the worker side of @p peer sends. */
        HandshakeNonce worker_nonce_of(PeerID peer)
        {
            return nonce_of(static_cast<std::uint8_t>(0x10 + peer.value));
        }

        /** @brief Returns the nonce the master side of @p peer sends. */
        HandshakeNonce master_nonce_of(PeerID peer)
        {
            return nonce_of(static_cast<std::uint8_t>(0x80 + peer.value));
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

        /** @brief Names the build every HELLO of a test carries unless the test names another. */
        constexpr const char* K_BUILD = "test-build";

        /** @brief Returns the HELLO of a worker called "worker" with the given nonce and build. */
        Wire::HelloMessage hello_with(const HandshakeNonce& worker_nonce,
                                      const char* build_id = K_BUILD)
        {
            Wire::HelloMessage hello;
            hello.worker_nonce = to_wire(worker_nonce.bytes);
            hello.build_id = build_id;
            hello.node_name = "worker";
            return hello;
        }

        /** @brief Returns the HELLO of the machine "gpu-box" with one label and one GPU. */
        Wire::HelloMessage gpu_box_hello(PeerID peer, const std::string& zone)
        {
            Wire::HelloMessage hello = hello_with(worker_nonce_of(peer));
            hello.node_name = "gpu-box";
            hello.operating_system = OperatingSystem::Linux;
            hello.labels.push_back(Wire::HelloLabel{"zone", zone});
            hello.devices.push_back(Wire::HelloDevice{DeviceKind::GPU, "Test GPU", 4096});
            return hello;
        }

        /** @brief Returns the target that names "gpu-box", its zone label and a GPU. */
        PlacementTarget gpu_in_zone(const std::string& zone)
        {
            PlacementTarget target;
            target.machine_name = "gpu-box";
            target.machine_label = NodeLabel{"zone", zone};
            target.device_kind = DeviceKind::GPU;
            return target;
        }

        /** @brief Returns the PROOF a worker holding @p secret sends for the two nonces. */
        Wire::ProofMessage proof_with(span<const std::uint8_t> secret,
                                      const HandshakeNonce& worker_nonce,
                                      const HandshakeNonce& master_nonce,
                                      std::vector<OperationID> operations, std::uint32_t max_jobs)
        {
            Wire::ProofMessage proof;
            proof.worker_proof = to_wire(
                compute_proof(HandshakeRole::Worker, secret, worker_nonce, master_nonce));
            proof.ops = std::move(operations);
            proof.max_jobs = max_jobs;
            return proof;
        }

        /** @brief Returns the type bytes of the messages the directory admits from a peer. */
        std::vector<int> admitted_types(const WorkerDirectory& directory, PeerID peer)
        {
            std::vector<int> admitted;
            for (int type = K_FIRST_MESSAGE_TYPE; type <= K_LAST_MESSAGE_TYPE; ++type)
            {
                if (directory.admits(peer, static_cast<MessageType>(type)))
                    admitted.push_back(type);
            }
            return admitted;
        }

        /** @brief Returns the type bytes of every message type but @p excluded. */
        std::vector<int> every_type_but(MessageType excluded)
        {
            std::vector<int> types;
            for (int type = K_FIRST_MESSAGE_TYPE; type <= K_LAST_MESSAGE_TYPE; ++type)
            {
                if (type != static_cast<int>(excluded))
                    types.push_back(type);
            }
            return types;
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
        std::vector<std::uint32_t> values_of(const std::vector<PeerID>& peers)
        {
            std::vector<std::uint32_t> values;
            for (const PeerID peer : peers)
                values.push_back(peer.value);
            return values;
        }

        /** @brief Builds a directory over a 32-byte secret and the real least-loaded policy. */
        class WorkerDirectoryFixture : public ::testing::Test
        {
            protected:
                /** @brief Fills the secret with the bytes 0 to 31 and builds the directory. */
                WorkerDirectoryFixture()
                    : secret_(make_secret()),
                      directory_(secret(), std::make_shared<LeastLoadedOffloadPolicy>(
                                               K_MIN_OFFLOAD_BYTES))
                {

                }

                /** @brief Returns the secret the directory verifies proofs with. */
                [[nodiscard]] span<const std::uint8_t> secret() const noexcept
                {
                    return span<const std::uint8_t>(secret_.data(), secret_.size());
                }

                /** @brief Connects a peer at time @p now under an endpoint named by its id. */
                void connect(PeerID peer, std::chrono::steady_clock::time_point now)
                {
                    directory_.add_peer(peer, "10.0.0." + std::to_string(peer.value) + ":7000", now);
                }

                /** @brief Sends the HELLO of a peer with the nonces of the peer. */
                HelloReply say_hello(PeerID peer)
                {
                    return directory_.accept_hello(peer, hello_with(worker_nonce_of(peer)),
                                                   master_nonce_of(peer));
                }

                /** @brief Sends the PROOF of a peer whose HELLO came with the nonces of the peer. */
                HandshakeStep prove(PeerID peer, std::vector<OperationID> operations,
                                    std::uint32_t max_jobs,
                                    std::chrono::steady_clock::time_point now)
                {
                    return directory_.accept_proof(
                        peer,
                        proof_with(secret(), worker_nonce_of(peer), master_nonce_of(peer),
                                   std::move(operations), max_jobs),
                        now);
                }

                /** @brief Connects a peer and takes it through HELLO and PROOF at time @p now. */
                HandshakeStep register_worker(PeerID peer, std::vector<OperationID> operations,
                                              std::uint32_t max_jobs,
                                              std::chrono::steady_clock::time_point now)
                {
                    connect(peer, now);
                    const HelloReply reply = say_hello(peer);
                    if (reply.step != HandshakeStep::Accepted)
                        return reply.step;

                    return prove(peer, std::move(operations), max_jobs, now);
                }

                /** @brief Registers K_FIRST with both operations and K_SECOND with the common one. */
                HandshakeStep register_two_workers()
                {
                    const HandshakeStep first = register_worker(
                        K_FIRST, {K_RARE_OPERATION, K_COMMON_OPERATION}, 4, at(milliseconds(0)));
                    if (first != HandshakeStep::Accepted)
                        return first;

                    return register_worker(K_SECOND, {K_COMMON_OPERATION}, 4, at(milliseconds(0)));
                }

                /** @brief Connects a peer and sends the HELLO of "gpu-box" with a zone label. */
                HandshakeStep hello_from_gpu_box(PeerID peer, const std::string& zone)
                {
                    connect(peer, at(milliseconds(0)));
                    return directory_.accept_hello(peer, gpu_box_hello(peer, zone),
                                                   master_nonce_of(peer)).step;
                }

                /** @brief Holds the cluster key. */
                std::vector<std::uint8_t> secret_;

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

        /** @brief Names the fixture of the tests that touch memory and one mutex only, with a wait of no time at most. */
        class Small_WorkerDirectory : public WorkerDirectoryFixture
        {

        };

        /** @brief Names the fixture of the tests that run a second thread that waits for a wake. */
        class Medium_WorkerDirectory : public WorkerDirectoryFixture
        {

        };
    } // namespace

    TEST_F(Small_WorkerDirectory, AdmitsNothingFromAPeerItNeverAdded)
    {
        EXPECT_TRUE(admitted_types(directory_, K_STRANGER).empty());
    }

    TEST_F(Small_WorkerDirectory, AdmitsOnlyHelloAndGoodbyeFromAPeerThatOwesHello)
    {
        connect(K_FIRST, at(milliseconds(0)));

        EXPECT_EQ(admitted_types(directory_, K_FIRST),
                  type_bytes({MessageType::Hello, MessageType::Goodbye}));
    }

    TEST_F(Small_WorkerDirectory, AdmitsNoWorkerMessageFromAPeerThatOwesProof)
    {
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(say_hello(K_FIRST).step, HandshakeStep::Accepted);

        EXPECT_EQ(admitted_types(directory_, K_FIRST),
                  type_bytes({MessageType::Hello, MessageType::Proof, MessageType::Goodbye}));
    }

    TEST_F(Small_WorkerDirectory, AdmitsEveryTypeButProofFromARegisteredWorker)
    {
        ASSERT_EQ(register_worker(K_FIRST, {}, 4, at(milliseconds(0))), HandshakeStep::Accepted);

        EXPECT_EQ(admitted_types(directory_, K_FIRST), every_type_but(MessageType::Proof));
    }

    TEST_F(Small_WorkerDirectory, AdmitsNothingFromARemovedPeer)
    {
        ASSERT_EQ(register_worker(K_FIRST, {}, 4, at(milliseconds(0))), HandshakeStep::Accepted);
        ASSERT_TRUE(directory_.remove_peer(K_FIRST).known);

        EXPECT_TRUE(admitted_types(directory_, K_FIRST).empty());
    }

    TEST_F(Small_WorkerDirectory, HandshakeOverdueListsNobodyAtTheLimitAndThePeerOneMillisecondAfter)
    {
        connect(K_FIRST, at(milliseconds(0)));

        std::vector<PeerID> at_limit;
        directory_.take_handshake_overdue(at(K_HANDSHAKE_LIMIT), K_HANDSHAKE_LIMIT, at_limit);
        std::vector<PeerID> past_limit;
        directory_.take_handshake_overdue(at(K_HANDSHAKE_LIMIT + milliseconds(1)),
                                          K_HANDSHAKE_LIMIT, past_limit);

        EXPECT_TRUE(at_limit.empty());
        EXPECT_EQ(values_of(past_limit), (std::vector<std::uint32_t>{1}));
    }

    TEST_F(Small_WorkerDirectory, HandshakeOverdueCountsFromTheConnectOfEachPeer)
    {
        connect(K_FIRST, at(milliseconds(0)));
        connect(K_THIRD, at(milliseconds(400)));

        std::vector<PeerID> overdue;
        directory_.take_handshake_overdue(at(milliseconds(5400)), K_HANDSHAKE_LIMIT, overdue);
        ASSERT_EQ(values_of(overdue), (std::vector<std::uint32_t>{1}));

        directory_.take_handshake_overdue(at(milliseconds(5401)), K_HANDSHAKE_LIMIT, overdue);
        EXPECT_EQ(values_of(overdue), (std::vector<std::uint32_t>{1, 3}));
    }

    TEST_F(Small_WorkerDirectory, HandshakeOverdueIgnoresTheHelloASlowPeerKeepsSending)
    {
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(nonce_of(1)), nonce_of(2)).step,
                  HandshakeStep::Accepted);
        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(nonce_of(3)), nonce_of(4)).step,
                  HandshakeStep::Accepted);

        std::vector<PeerID> overdue;
        directory_.take_handshake_overdue(at(milliseconds(5001)), K_HANDSHAKE_LIMIT, overdue);

        EXPECT_EQ(values_of(overdue), (std::vector<std::uint32_t>{1}));
    }

    TEST_F(Small_WorkerDirectory, HandshakeOverdueNeverListsARegisteredWorker)
    {
        ASSERT_EQ(register_worker(K_SECOND, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);

        std::vector<PeerID> overdue;
        directory_.take_handshake_overdue(at(milliseconds(900000)), K_HANDSHAKE_LIMIT, overdue);

        EXPECT_TRUE(overdue.empty());
    }

    TEST_F(Small_WorkerDirectory, HandshakeOverdueListsEachPeerOnce)
    {
        connect(K_FIRST, at(milliseconds(0)));
        std::vector<PeerID> first_call;
        directory_.take_handshake_overdue(at(milliseconds(5001)), K_HANDSHAKE_LIMIT, first_call);
        ASSERT_EQ(first_call.size(), 1u);

        std::vector<PeerID> second_call;
        directory_.take_handshake_overdue(at(milliseconds(900000)), K_HANDSHAKE_LIMIT, second_call);

        EXPECT_TRUE(second_call.empty());
    }

    TEST_F(Small_WorkerDirectory, RemovePeerReportsAnUnregisteredPeerAsKnownWithItsNameAndEndpoint)
    {
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(say_hello(K_FIRST).step, HandshakeStep::Accepted);

        const RemovedPeer removed = directory_.remove_peer(K_FIRST);

        EXPECT_TRUE(removed.known);
        EXPECT_FALSE(removed.registered);
        EXPECT_EQ(removed.name, "worker");
        EXPECT_EQ(removed.endpoint, "10.0.0.1:7000");
    }

    TEST_F(Small_WorkerDirectory, RemovePeerOfAPeerAlreadyRemovedFindsNothing)
    {
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_TRUE(directory_.remove_peer(K_FIRST).known);

        EXPECT_FALSE(directory_.remove_peer(K_FIRST).known);
    }

    TEST_F(Small_WorkerDirectory, WaitForZeroWorkersIsReachedAtOnce)
    {
        EXPECT_TRUE(directory_.wait_for_workers(0, milliseconds(0)));
    }

    TEST_F(Small_WorkerDirectory, WaitForAWorkerNobodyRegisteredTimesOut)
    {
        EXPECT_FALSE(directory_.wait_for_workers(1, milliseconds(0)));
    }

    TEST_F(Small_WorkerDirectory, WaitForWorkersIsReachedAtOnceOnceTheCountIsAlive)
    {
        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);

        EXPECT_TRUE(directory_.wait_for_workers(1, milliseconds(0)));
    }

    TEST_F(Medium_WorkerDirectory, WaitForWorkersWakesOnRegistration)
    {
        std::atomic<bool> reached{false};
        std::thread waiter([&] { reached = directory_.wait_for_workers(1, K_UNTIL_WOKEN); });

        const HandshakeStep step =
            register_worker(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0)));
        waiter.join();

        ASSERT_EQ(step, HandshakeStep::Accepted);
        EXPECT_TRUE(reached.load());
    }

    TEST_F(Medium_WorkerDirectory, CloseWakesAWaiterWhoseCountWasNotReached)
    {
        std::atomic<bool> reached{true};
        std::thread waiter([&] { reached = directory_.wait_for_workers(2, K_UNTIL_WOKEN); });

        directory_.close();
        waiter.join();

        EXPECT_FALSE(reached.load());
    }

    TEST_F(Small_WorkerDirectory, ClosedDirectoryReachesNoCountItHeldWhenItClosed)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        directory_.close();

        EXPECT_FALSE(directory_.wait_for_workers(2, milliseconds(0)));
    }

    TEST_F(Small_WorkerDirectory, ClosedDirectoryReachesNoCountEvenZero)
    {
        directory_.close();

        EXPECT_FALSE(directory_.wait_for_workers(0, milliseconds(0)));
    }

    TEST_F(Small_WorkerDirectory, HelloReplyCarriesTheMasterProofOfTheNonces)
    {
        connect(K_FIRST, at(milliseconds(0)));

        const HelloReply reply = say_hello(K_FIRST);

        ASSERT_EQ(reply.step, HandshakeStep::Accepted);
        EXPECT_EQ(reply.master_proof, compute_proof(HandshakeRole::Master, secret(),
                                                    worker_nonce_of(K_FIRST),
                                                    master_nonce_of(K_FIRST)));
    }

    TEST_F(Small_WorkerDirectory, ProofMadeUnderAnotherSecretFailsAuthenticationAndRegistersNobody)
    {
        const std::vector<std::uint8_t> other_secret(32, 0xee);
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(say_hello(K_FIRST).step, HandshakeStep::Accepted);

        const HandshakeStep step = directory_.accept_proof(
            K_FIRST,
            proof_with(span<const std::uint8_t>(other_secret.data(), other_secret.size()),
                       worker_nonce_of(K_FIRST), master_nonce_of(K_FIRST), {}, 4),
            at(milliseconds(1)));

        EXPECT_EQ(step, HandshakeStep::AuthenticationFailed);
        EXPECT_EQ(directory_.alive_count(), 0u);
    }

    TEST_F(Small_WorkerDirectory, RefusedProofLeavesThePeerOutOfTheWorkerMessages)
    {
        const std::vector<std::uint8_t> other_secret(32, 0xee);
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(say_hello(K_FIRST).step, HandshakeStep::Accepted);
        ASSERT_EQ(directory_.accept_proof(
                      K_FIRST,
                      proof_with(span<const std::uint8_t>(other_secret.data(), other_secret.size()),
                                 worker_nonce_of(K_FIRST), master_nonce_of(K_FIRST), {}, 4),
                      at(milliseconds(1))),
                  HandshakeStep::AuthenticationFailed);

        EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::TaskResult));
        EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::Proof));
    }

    TEST_F(Small_WorkerDirectory, RefusedProofSpendsItsNoncesSoTheRightProofComesTooLate)
    {
        const std::vector<std::uint8_t> other_secret(32, 0xee);
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(say_hello(K_FIRST).step, HandshakeStep::Accepted);
        ASSERT_EQ(directory_.accept_proof(
                      K_FIRST,
                      proof_with(span<const std::uint8_t>(other_secret.data(), other_secret.size()),
                                 worker_nonce_of(K_FIRST), master_nonce_of(K_FIRST), {}, 4),
                      at(milliseconds(1))),
                  HandshakeStep::AuthenticationFailed);

        EXPECT_EQ(prove(K_FIRST, {}, 4, at(milliseconds(2))), HandshakeStep::OutOfOrder);
    }

    TEST_F(Small_WorkerDirectory, MasterProofSentBackDoesNotPassAsTheWorkerProof)
    {
        connect(K_FIRST, at(milliseconds(0)));
        const HelloReply reply = say_hello(K_FIRST);
        ASSERT_EQ(reply.step, HandshakeStep::Accepted);
        Wire::ProofMessage replayed;
        replayed.worker_proof = to_wire(reply.master_proof);
        replayed.max_jobs = 4;

        EXPECT_EQ(directory_.accept_proof(K_FIRST, replayed, at(milliseconds(1))),
                  HandshakeStep::AuthenticationFailed);
    }

    TEST_F(Small_WorkerDirectory, ProofUnderAnEmptyKeyFailsAuthentication)
    {
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(say_hello(K_FIRST).step, HandshakeStep::Accepted);

        EXPECT_EQ(directory_.accept_proof(
                      K_FIRST,
                      proof_with(span<const std::uint8_t>(), worker_nonce_of(K_FIRST),
                                 master_nonce_of(K_FIRST), {}, 4),
                      at(milliseconds(1))),
                  HandshakeStep::AuthenticationFailed);
    }

    TEST_F(Small_WorkerDirectory, HelloFromAnUnknownPeerAnswersUnknownPeer)
    {
        EXPECT_EQ(say_hello(K_STRANGER).step, HandshakeStep::UnknownPeer);
    }

    TEST_F(Small_WorkerDirectory, ProofFromAnUnknownPeerAnswersUnknownPeer)
    {
        EXPECT_EQ(prove(K_STRANGER, {}, 4, at(milliseconds(0))), HandshakeStep::UnknownPeer);
    }

    TEST_F(Small_WorkerDirectory, SecondHelloAnswersTheMasterProofOfItsOwnNonces)
    {
        const HandshakeNonce second_worker_nonce = nonce_of(0x33);
        const HandshakeNonce second_master_nonce = nonce_of(0x44);
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(say_hello(K_FIRST).step, HandshakeStep::Accepted);

        const HelloReply second = directory_.accept_hello(
            K_FIRST, hello_with(second_worker_nonce), second_master_nonce);

        ASSERT_EQ(second.step, HandshakeStep::Accepted);
        EXPECT_EQ(second.master_proof, compute_proof(HandshakeRole::Master, secret(),
                                                     second_worker_nonce, second_master_nonce));
    }

    TEST_F(Small_WorkerDirectory, ProofForTheNoncesOfTheFirstHelloFailsAfterASecondHello)
    {
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(say_hello(K_FIRST).step, HandshakeStep::Accepted);
        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(nonce_of(0x33)), nonce_of(0x44)).step,
                  HandshakeStep::Accepted);

        EXPECT_EQ(prove(K_FIRST, {}, 4, at(milliseconds(1))), HandshakeStep::AuthenticationFailed);
    }

    TEST_F(Small_WorkerDirectory, ProofForTheNoncesOfTheSecondHelloRegistersTheWorker)
    {
        const HandshakeNonce second_worker_nonce = nonce_of(0x33);
        const HandshakeNonce second_master_nonce = nonce_of(0x44);
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(say_hello(K_FIRST).step, HandshakeStep::Accepted);
        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(second_worker_nonce),
                                          second_master_nonce).step,
                  HandshakeStep::Accepted);

        EXPECT_EQ(directory_.accept_proof(
                      K_FIRST,
                      proof_with(secret(), second_worker_nonce, second_master_nonce, {}, 4),
                      at(milliseconds(1))),
                  HandshakeStep::Accepted);
    }

    TEST_F(Small_WorkerDirectory, ProofFromARegisteredWorkerAnswersOutOfOrderAndChangesNothing)
    {
        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);

        EXPECT_EQ(prove(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(1))),
                  HandshakeStep::OutOfOrder);
        EXPECT_EQ(directory_.alive_count(), 1u);
    }

    TEST_F(Small_WorkerDirectory, TakeSilentLeavesAWorkerThatIsAtTheLimit)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);

        std::vector<PeerID> silent;
        directory_.take_silent(at(milliseconds(500)), K_LIVENESS, silent);

        EXPECT_TRUE(silent.empty());
    }

    TEST_F(Small_WorkerDirectory, TakeSilentListsOnlyTheWorkerWithoutAFrameWithinTheLimit)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        directory_.note_frame(K_SECOND, at(milliseconds(900)));

        std::vector<PeerID> silent;
        directory_.take_silent(at(milliseconds(1000)), K_LIVENESS, silent);

        EXPECT_EQ(values_of(silent), (std::vector<std::uint32_t>{1}));
    }

    TEST_F(Small_WorkerDirectory, TakeSilentStopsCountingTheWorkerAsAlive)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        directory_.note_frame(K_SECOND, at(milliseconds(900)));

        std::vector<PeerID> silent;
        directory_.take_silent(at(milliseconds(1000)), K_LIVENESS, silent);

        EXPECT_EQ(directory_.alive_count(), 1u);
    }

    TEST_F(Small_WorkerDirectory, TakeSilentListsAWorkerOnce)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        directory_.note_frame(K_SECOND, at(milliseconds(900)));
        std::vector<PeerID> first_call;
        directory_.take_silent(at(milliseconds(1000)), K_LIVENESS, first_call);
        ASSERT_EQ(first_call.size(), 1u);

        std::vector<PeerID> second_call;
        directory_.take_silent(at(milliseconds(1000)), K_LIVENESS, second_call);

        EXPECT_TRUE(second_call.empty());
    }

    TEST_F(Small_WorkerDirectory, TakenWorkerLeavesTheWorkerList)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        directory_.note_frame(K_SECOND, at(milliseconds(900)));
        std::vector<PeerID> silent;
        directory_.take_silent(at(milliseconds(1000)), K_LIVENESS, silent);

        std::vector<PeerID> workers;
        directory_.list_workers(workers);

        EXPECT_EQ(values_of(workers), (std::vector<std::uint32_t>{2}));
    }

    TEST_F(Small_WorkerDirectory, KernelsOfATakenWorkerStopCountingAndTheOthersStay)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        ASSERT_TRUE(directory_.supports(K_RARE_OPERATION));
        directory_.note_frame(K_SECOND, at(milliseconds(900)));
        std::vector<PeerID> silent;
        directory_.take_silent(at(milliseconds(1000)), K_LIVENESS, silent);

        EXPECT_FALSE(directory_.supports(K_RARE_OPERATION));
        EXPECT_TRUE(directory_.supports(K_COMMON_OPERATION));
    }

    TEST_F(Small_WorkerDirectory, SupportsNoOperationNoWorkerSent)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);

        EXPECT_FALSE(directory_.supports(K_ABSENT_OPERATION));
    }

    TEST_F(Small_WorkerDirectory, PickSkipsATakenWorker)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        directory_.note_frame(K_SECOND, at(milliseconds(900)));
        std::vector<PeerID> silent;
        directory_.take_silent(at(milliseconds(1000)), K_LIVENESS, silent);

        const std::optional<PeerID> picked = directory_.pick(*node, K_TASK_BYTES);

        ASSERT_TRUE(picked.has_value());
        EXPECT_EQ(picked->value, 2u);
    }

    TEST_F(Small_WorkerDirectory, LateFrameDoesNotBringATakenWorkerBack)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        directory_.note_frame(K_SECOND, at(milliseconds(900)));
        std::vector<PeerID> silent;
        directory_.take_silent(at(milliseconds(1000)), K_LIVENESS, silent);

        directory_.note_frame(K_FIRST, at(milliseconds(1001)));

        EXPECT_EQ(directory_.alive_count(), 1u);
    }

    TEST_F(Small_WorkerDirectory, RemovePeerReportsATakenWorkerAsRegistered)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        directory_.note_frame(K_SECOND, at(milliseconds(900)));
        std::vector<PeerID> silent;
        directory_.take_silent(at(milliseconds(1000)), K_LIVENESS, silent);

        const RemovedPeer removed = directory_.remove_peer(K_FIRST);

        EXPECT_TRUE(removed.known);
        EXPECT_TRUE(removed.registered);
    }

    TEST_F(Small_WorkerDirectory, RemovePeerDoesNotUncountATakenWorkerAgain)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        directory_.note_frame(K_SECOND, at(milliseconds(900)));
        std::vector<PeerID> silent;
        directory_.take_silent(at(milliseconds(1000)), K_LIVENESS, silent);

        static_cast<void>(directory_.remove_peer(K_FIRST));

        EXPECT_EQ(directory_.alive_count(), 1u);
    }

    TEST_F(Small_WorkerDirectory, EverySilentWorkerTakenLeavesNothingToPick)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        std::vector<PeerID> silent;
        directory_.take_silent(at(milliseconds(1000)), K_LIVENESS, silent);
        ASSERT_EQ(values_of(silent), (std::vector<std::uint32_t>{1, 2}));

        EXPECT_FALSE(directory_.pick(*node, K_TASK_BYTES).has_value());
    }

    TEST_F(Small_WorkerDirectory, PickWithNoWorkerReturnsNothing)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();

        EXPECT_FALSE(directory_.pick(*node, K_TASK_BYTES).has_value());
    }

    TEST_F(Small_WorkerDirectory, PickKeepsATaskLocalBelowTheByteThresholdOfThePolicy)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();
        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);

        EXPECT_FALSE(directory_.pick(*node, K_LOCAL_BYTES).has_value());
    }

    TEST_F(Small_WorkerDirectory, PickCountsTheTaskAgainstThePickedWorker)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();
        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_TRUE(directory_.pick(*node, K_TASK_BYTES).has_value());

        std::vector<WorkerSnapshot> workers;
        directory_.snapshot(at(milliseconds(1)), workers);

        ASSERT_EQ(workers.size(), 1u);
        EXPECT_EQ(workers[0].inflight, 1u);
    }

    TEST_F(Small_WorkerDirectory, PickSkipsAWorkerAtItsJobLimit)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();
        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 1, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_EQ(register_worker(K_SECOND, {K_COMMON_OPERATION}, 2, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_EQ(directory_.pick(*node, K_TASK_BYTES)->value, 1u);

        const std::optional<PeerID> picked = directory_.pick(*node, K_TASK_BYTES);

        ASSERT_TRUE(picked.has_value());
        EXPECT_EQ(picked->value, 2u);
    }

    TEST_F(Small_WorkerDirectory, PickReturnsNothingWhenEveryWorkerIsAtItsJobLimit)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();
        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 1, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_EQ(register_worker(K_SECOND, {K_COMMON_OPERATION}, 2, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_TRUE(directory_.pick(*node, K_TASK_BYTES).has_value());
        ASSERT_TRUE(directory_.pick(*node, K_TASK_BYTES).has_value());
        ASSERT_TRUE(directory_.pick(*node, K_TASK_BYTES).has_value());

        EXPECT_FALSE(directory_.pick(*node, K_TASK_BYTES).has_value());
    }

    TEST_F(Small_WorkerDirectory, ReleaseInflightFreesOneSlotAndASecondReleaseLowersNothing)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();
        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 1, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_EQ(directory_.pick(*node, K_TASK_BYTES)->value, 1u);
        ASSERT_FALSE(directory_.pick(*node, K_TASK_BYTES).has_value());

        directory_.release_inflight(K_FIRST);
        directory_.release_inflight(K_FIRST);
        const std::optional<PeerID> refilled = directory_.pick(*node, K_TASK_BYTES);
        const std::optional<PeerID> overfilled = directory_.pick(*node, K_TASK_BYTES);

        ASSERT_TRUE(refilled.has_value());
        EXPECT_EQ(refilled->value, 1u);
        EXPECT_FALSE(overfilled.has_value());
    }

    TEST_F(Small_WorkerDirectory, PickOnAClosedDirectoryReturnsNothing)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();
        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        directory_.close();

        EXPECT_FALSE(directory_.pick(*node, K_TASK_BYTES).has_value());
    }

    TEST_F(Small_WorkerDirectory, PickOffersATaskToTheWorkerThatHoldsItsKernel)
    {
        const sushi_ptr<Internal::Graph::Node> rare = rare_node();
        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_EQ(register_worker(K_SECOND, {K_RARE_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);

        const std::optional<PeerID> picked = directory_.pick(*rare, K_TASK_BYTES);

        ASSERT_TRUE(picked.has_value());
        EXPECT_EQ(picked->value, K_SECOND.value);
    }

    TEST_F(Small_WorkerDirectory, PickDoesNotFallBackToAWorkerWithoutTheKernelWhenTheHolderIsFull)
    {
        const sushi_ptr<Internal::Graph::Node> rare = rare_node();
        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_EQ(register_worker(K_SECOND, {K_RARE_OPERATION}, 1, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_TRUE(directory_.pick(*rare, K_TASK_BYTES).has_value());

        EXPECT_FALSE(directory_.pick(*rare, K_TASK_BYTES).has_value());
    }

    TEST_F(Small_WorkerDirectory, PickOffersNoWorkerATaskWithNoOperation)
    {
        const sushi_ptr<Internal::Graph::Node> untagged = make_sushi<Internal::Graph::Node>();
        ASSERT_EQ(register_worker(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);

        EXPECT_FALSE(directory_.pick(*untagged, K_TASK_BYTES).has_value());
    }

    TEST_F(Small_WorkerDirectory, SnapshotListsOnlyAPeerThatFinishedTheHandshake)
    {
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(register_worker(K_SECOND, {K_COMMON_OPERATION}, 4, at(milliseconds(100))),
                  HandshakeStep::Accepted);

        std::vector<WorkerSnapshot> workers;
        directory_.snapshot(at(milliseconds(1000)), workers);

        ASSERT_EQ(workers.size(), 1u);
        EXPECT_EQ(workers[0].id, 2u);
    }

    TEST_F(Small_WorkerDirectory, SnapshotReportsWhatTheWorkerSent)
    {
        const sushi_ptr<Internal::Graph::Node> node = common_node();
        ASSERT_EQ(register_worker(K_SECOND, {K_COMMON_OPERATION}, 4, at(milliseconds(100))),
                  HandshakeStep::Accepted);
        directory_.note_pong(K_SECOND, 250, 4096);
        directory_.note_heartbeat(K_SECOND, 3, 8192);
        directory_.note_frame(K_SECOND, at(milliseconds(400)));
        ASSERT_TRUE(directory_.pick(*node, K_TASK_BYTES).has_value());

        std::vector<WorkerSnapshot> workers;
        directory_.snapshot(at(milliseconds(1000)), workers);

        ASSERT_EQ(workers.size(), 1u);
        EXPECT_EQ(workers[0].name, "worker");
        EXPECT_EQ(workers[0].endpoint, "10.0.0.2:7000");
        EXPECT_EQ(workers[0].inflight, 1u);
        EXPECT_EQ(workers[0].rtt_micros, 250u);
        EXPECT_EQ(workers[0].free_mem, 4096u);
        EXPECT_EQ(workers[0].queue_depth, 3u);
        EXPECT_EQ(workers[0].cache_bytes, 8192u);
        EXPECT_EQ(workers[0].ms_since_last_frame, 600u);
    }

    TEST_F(Small_WorkerDirectory, SnapshotCarriesTheFactsOfTheWorkersHello)
    {
        ASSERT_EQ(hello_from_gpu_box(K_FIRST, "a"), HandshakeStep::Accepted);
        ASSERT_EQ(prove(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);

        std::vector<WorkerSnapshot> workers;
        directory_.snapshot(at(milliseconds(1)), workers);

        ASSERT_EQ(workers.size(), 1u);
        const NodeFacts& facts = workers[0].facts;
        EXPECT_EQ(facts.name, "gpu-box");
        EXPECT_EQ(facts.operating_system, OperatingSystem::Linux);
        ASSERT_EQ(facts.labels.size(), 1u);
        EXPECT_EQ(facts.labels[0].key, "zone");
        EXPECT_EQ(facts.labels[0].value, "a");
        ASSERT_EQ(facts.devices.size(), 1u);
        EXPECT_EQ(facts.devices[0].kind, DeviceKind::GPU);
        EXPECT_EQ(facts.devices[0].name, "Test GPU");
        EXPECT_EQ(facts.devices[0].memory_bytes, 4096u);
    }

    TEST_F(Small_WorkerDirectory, TargetNamingTheMachineLabelAndDeviceOfAProvenHelloPicksItsWorker)
    {
        ASSERT_EQ(hello_from_gpu_box(K_FIRST, "a"), HandshakeStep::Accepted);
        ASSERT_EQ(prove(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);

        const TargetPick pick = directory_.pick_target(gpu_in_zone("a"), K_COMMON_OPERATION);

        ASSERT_EQ(pick.status, TargetStatus::Picked);
        EXPECT_EQ(pick.worker.value, K_FIRST.value);
    }

    TEST_F(Small_WorkerDirectory, TargetNeedingMoreDeviceMemoryThanTheHelloReportedIsAbsent)
    {
        ASSERT_EQ(hello_from_gpu_box(K_FIRST, "a"), HandshakeStep::Accepted);
        ASSERT_EQ(prove(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        PlacementTarget target;
        target.device_min_memory = 4097;

        EXPECT_EQ(directory_.pick_target(target, K_COMMON_OPERATION).status,
                  TargetStatus::Absent);
    }

    TEST_F(Small_WorkerDirectory, HelloThatHasNotProvedTheSecretReplacesNoFacts)
    {
        ASSERT_EQ(hello_from_gpu_box(K_FIRST, "a"), HandshakeStep::Accepted);
        ASSERT_EQ(prove(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_EQ(hello_from_gpu_box(K_SECOND, "b"), HandshakeStep::Accepted);

        EXPECT_EQ(directory_.pick_target(gpu_in_zone("b"), K_COMMON_OPERATION).status,
                  TargetStatus::Absent);
    }

    TEST_F(Small_WorkerDirectory, ProvenHelloOfTheSameNameReplacesTheFactsOfTheLostWorker)
    {
        ASSERT_EQ(hello_from_gpu_box(K_FIRST, "a"), HandshakeStep::Accepted);
        ASSERT_EQ(prove(K_FIRST, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_TRUE(directory_.remove_peer(K_FIRST).registered);
        ASSERT_EQ(hello_from_gpu_box(K_SECOND, "b"), HandshakeStep::Accepted);
        ASSERT_EQ(prove(K_SECOND, {K_COMMON_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);

        const TargetPick new_zone = directory_.pick_target(gpu_in_zone("b"), K_COMMON_OPERATION);
        const TargetPick old_zone = directory_.pick_target(gpu_in_zone("a"), K_COMMON_OPERATION);

        ASSERT_EQ(new_zone.status, TargetStatus::Picked);
        EXPECT_EQ(new_zone.worker.value, K_SECOND.value);
        EXPECT_EQ(old_zone.status, TargetStatus::Absent);
    }

    TEST_F(Small_WorkerDirectory, IdentityOfAnUnknownPeerIsEmpty)
    {
        EXPECT_FALSE(directory_.identity(K_STRANGER).has_value());
    }

    TEST_F(Small_WorkerDirectory, IdentityOfAPeerThatOwesProofIsEmpty)
    {
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(say_hello(K_FIRST).step, HandshakeStep::Accepted);

        EXPECT_FALSE(directory_.identity(K_FIRST).has_value());
    }

    TEST_F(Small_WorkerDirectory, IdentityCarriesTheBuildOfTheLastHello)
    {
        const HandshakeNonce second_worker_nonce = nonce_of(0x33);
        const HandshakeNonce second_master_nonce = nonce_of(0x44);
        connect(K_FIRST, at(milliseconds(0)));
        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(nonce_of(0x11), "build-a"),
                                          nonce_of(0x22)).step,
                  HandshakeStep::Accepted);
        ASSERT_EQ(directory_.accept_hello(K_FIRST, hello_with(second_worker_nonce, "build-b"),
                                          second_master_nonce).step,
                  HandshakeStep::Accepted);
        ASSERT_EQ(directory_.accept_proof(
                      K_FIRST,
                      proof_with(secret(), second_worker_nonce, second_master_nonce, {}, 4),
                      at(milliseconds(1))),
                  HandshakeStep::Accepted);

        const std::optional<WorkerIdentity> worker = directory_.identity(K_FIRST);

        ASSERT_TRUE(worker.has_value());
        EXPECT_EQ(worker->build_id, "build-b");
    }

    TEST_F(Small_WorkerDirectory, IdentityCarriesTheNameAndEndpointOfARegisteredWorker)
    {
        ASSERT_EQ(register_worker(K_FIRST, {}, 4, at(milliseconds(0))), HandshakeStep::Accepted);

        const std::optional<WorkerIdentity> worker = directory_.identity(K_FIRST);

        ASSERT_TRUE(worker.has_value());
        EXPECT_EQ(worker->name, "worker");
        EXPECT_EQ(worker->endpoint, "10.0.0.1:7000");
    }

    TEST_F(Small_WorkerDirectory, IdentityIsEmptyOnceThePeerIsRemoved)
    {
        ASSERT_EQ(register_worker(K_FIRST, {}, 4, at(milliseconds(0))), HandshakeStep::Accepted);
        ASSERT_TRUE(directory_.remove_peer(K_FIRST).registered);

        EXPECT_FALSE(directory_.identity(K_FIRST).has_value());
    }

    TEST_F(Small_WorkerDirectory, RepeatedHelloFromARegisteredWorkerAnswersAlreadyRegistered)
    {
        ASSERT_EQ(register_worker(K_FIRST, {K_RARE_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_TRUE(directory_.admits(K_FIRST, MessageType::Hello));

        const HelloReply again = directory_.accept_hello(K_FIRST, hello_with(nonce_of(0x77), "build-z"),
                                                         nonce_of(0x88));

        EXPECT_EQ(again.step, HandshakeStep::AlreadyRegistered);
    }

    TEST_F(Small_WorkerDirectory, RepeatedHelloKeepsTheWorkerCountedAndItsKernelsSupported)
    {
        ASSERT_EQ(register_worker(K_FIRST, {K_RARE_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        static_cast<void>(directory_.accept_hello(K_FIRST, hello_with(nonce_of(0x77), "build-z"),
                                                  nonce_of(0x88)));

        EXPECT_EQ(directory_.alive_count(), 1u);
        EXPECT_TRUE(directory_.supports(K_RARE_OPERATION));
    }

    TEST_F(Small_WorkerDirectory, RepeatedHelloDoesNotReopenTheHandshake)
    {
        ASSERT_EQ(register_worker(K_FIRST, {K_RARE_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        static_cast<void>(directory_.accept_hello(K_FIRST, hello_with(nonce_of(0x77), "build-z"),
                                                  nonce_of(0x88)));

        EXPECT_TRUE(directory_.admits(K_FIRST, MessageType::TaskResult));
        EXPECT_FALSE(directory_.admits(K_FIRST, MessageType::Proof));
    }

    TEST_F(Small_WorkerDirectory, RepeatedHelloKeepsATaskThatIsOut)
    {
        const sushi_ptr<Internal::Graph::Node> rare = rare_node();
        ASSERT_EQ(register_worker(K_FIRST, {K_RARE_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        ASSERT_TRUE(directory_.pick(*rare, K_TASK_BYTES).has_value());
        static_cast<void>(directory_.accept_hello(K_FIRST, hello_with(nonce_of(0x77), "build-z"),
                                                  nonce_of(0x88)));

        std::vector<WorkerSnapshot> workers;
        directory_.snapshot(at(milliseconds(1)), workers);

        ASSERT_EQ(workers.size(), 1u);
        EXPECT_EQ(workers[0].inflight, 1u);
    }

    TEST_F(Small_WorkerDirectory, RepeatedHelloLeavesTheIdentityTheWorkerRegisteredWith)
    {
        ASSERT_EQ(register_worker(K_FIRST, {K_RARE_OPERATION}, 4, at(milliseconds(0))),
                  HandshakeStep::Accepted);
        static_cast<void>(directory_.accept_hello(K_FIRST, hello_with(nonce_of(0x77), "build-z"),
                                                  nonce_of(0x88)));

        EXPECT_EQ(directory_.identity(K_FIRST)->build_id, K_BUILD);
    }

    TEST_F(Small_WorkerDirectory, CloseLeavesNoWorkerAlive)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);

        directory_.close();

        EXPECT_EQ(directory_.alive_count(), 0u);
    }

    TEST_F(Small_WorkerDirectory, CloseEndsSupportForEveryOperation)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);

        directory_.close();

        EXPECT_FALSE(directory_.supports(K_RARE_OPERATION));
        EXPECT_FALSE(directory_.supports(K_COMMON_OPERATION));
    }

    TEST_F(Small_WorkerDirectory, CloseEmptiesTheWorkerList)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);

        directory_.close();
        std::vector<PeerID> listed;
        directory_.list_workers(listed);

        EXPECT_TRUE(listed.empty());
    }

    TEST_F(Small_WorkerDirectory, CloseEmptiesTheSnapshot)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);

        directory_.close();
        std::vector<WorkerSnapshot> workers;
        directory_.snapshot(at(milliseconds(1)), workers);

        EXPECT_TRUE(workers.empty());
    }

    TEST_F(Small_WorkerDirectory, ClosedDirectoryRegistersNoPeer)
    {
        directory_.close();

        EXPECT_EQ(register_worker(K_THIRD, {K_COMMON_OPERATION}, 4, at(milliseconds(2))),
                  HandshakeStep::OutOfOrder);
    }

    TEST_F(Small_WorkerDirectory, RemovePeerAfterCloseReportsTheWorkerAsRegistered)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        directory_.close();

        EXPECT_TRUE(directory_.remove_peer(K_FIRST).registered);
    }

    TEST_F(Small_WorkerDirectory, RemovePeerAfterCloseDoesNotUncountTheWorkerTwice)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        directory_.close();

        static_cast<void>(directory_.remove_peer(K_FIRST));

        EXPECT_EQ(directory_.alive_count(), 0u);
    }

    TEST_F(Small_WorkerDirectory, SecondCloseDoesNotUncountTheWorkerTwice)
    {
        ASSERT_EQ(register_two_workers(), HandshakeStep::Accepted);
        directory_.close();

        directory_.close();

        EXPECT_EQ(directory_.alive_count(), 0u);
    }
} // namespace WorkerDirectoryTests
