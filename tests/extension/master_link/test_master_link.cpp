/**************************************************************************/
/* test_master_link.cpp                                                   */
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
 * @file test_master_link.cpp
 * @brief Tests MasterLink: its handshake, watch and heartbeat on a ManualClock, and its thread.
 * @see tests/extension/master_link/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/link_security/handshake.hpp>
#include <SushiRuntime/master_link/master_link.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

#include "support/executor_fabric.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using std::chrono::milliseconds;

namespace
{
    /** @brief Holds the name the master gives the worker in WELCOME. */
    constexpr std::uint32_t K_WORKER_ID = 1;

    /** @brief Holds a step of the fabric's clock that passes the delay before a worker reconnects. */
    constexpr std::uint64_t K_RECONNECT_NS = 2'000'000;

    /** @brief Holds the byte every key of these tests is filled with. */
    constexpr std::uint8_t K_SECRET_BYTE = 0x42;

    /** @brief Holds the time every manual clock starts at, in nanoseconds. */
    constexpr std::uint64_t K_START_NS = 3'600'000'000'000ull;

    /** @brief Returns 32 key bytes that all hold @p value. */
    std::vector<std::uint8_t> secret_of(std::uint8_t value)
    {
        return std::vector<std::uint8_t>(32, value);
    }

    /** @brief Returns a nonce whose bytes all hold @p value. */
    HandshakeNonce nonce_of(std::uint8_t value)
    {
        HandshakeNonce nonce{};
        nonce.bytes.fill(value);
        return nonce;
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
        const HandshakeNonce master_nonce = nonce_of(0x22);
        const Sha256Digest proof =
            compute_proof(HandshakeRole::Master,
                          span<const std::uint8_t>(secret.data(), secret.size()), worker_nonce,
                          master_nonce);

        Wire::ChallengeMessage challenge;
        std::memcpy(challenge.master_nonce.data(), master_nonce.bytes.data(),
                    challenge.master_nonce.size());
        std::memcpy(challenge.master_proof.data(), proof.data(), challenge.master_proof.size());
        challenge.session_id = session_id;
        return challenge;
    }

    /** @brief Returns a WELCOME with a liveness timeout and a chunk size. */
    Wire::WelcomeMessage welcome_of(std::uint32_t liveness_timeout_ms, std::uint32_t chunk_bytes)
    {
        Wire::WelcomeMessage welcome;
        welcome.worker_id = K_WORKER_ID;
        welcome.liveness_timeout_ms = liveness_timeout_ms;
        welcome.ping_interval_ms = 250;
        welcome.chunk_bytes = chunk_bytes;
        return welcome;
    }

    /** @brief Decodes the payload of a sent frame. */
    template <typename Message>
    Message decode(const RecordedFrame& frame)
    {
        Message message;
        const Wire::DecodeResult result = Message::decode(
            span<const std::byte>(frame.payload.data(), frame.payload.size()), message);
        EXPECT_TRUE(result.ok()) << "payload failed to decode at offset " << result.offset;
        return message;
    }

    /** @brief Returns the options of a worker named "worker-a" with two ops. */
    MasterLinkOptions link_options()
    {
        MasterLinkOptions options;
        options.name = "worker-a";
        options.build_id = "build-1";
        options.ops = {OperationID(7), OperationID(9)};
        options.max_jobs = 16;
        options.cache_budget = 4096;
        options.heartbeat_interval = milliseconds(10);
        return options;
    }

    /** @brief Holds a link over an in-process transport whose master end records, with nonces 1, 2, 3 and so on. */
    class LinkRig
    {
        public:
            /** @brief Builds the link on @p clock with @p options. */
            LinkRig(const Clock& clock, MasterLinkOptions options)
                : secret_(secret_of(K_SECRET_BYTE)),
                  link_(*fabric_.worker(), span<const std::uint8_t>(secret_.data(), secret_.size()),
                        std::move(options), [this]() -> Result<HandshakeNonce>
                        {
                            return nonce_of(++drawn_);
                        },
                        clock)
            {
                fabric_.start_master();
                fabric_.start_worker_recording();
            }

            /** @brief Returns the name the worker end gives its connection to the master. */
            PeerID peer()
            {
                return fabric_.worker_events().connected().back();
            }

            /** @brief Returns the frames of one type that reached the master end, in order. */
            std::vector<RecordedFrame> sent(MessageType type)
            {
                fabric_.settle();
                return fabric_.master_events().frames_of(type);
            }

            /** @brief Returns how many connections the master end saw closed. */
            std::size_t closed_count()
            {
                fabric_.settle();
                return fabric_.master_events().losses().size();
            }

            /** @brief Returns the HELLO frames sent so far, decoded. */
            std::vector<Wire::HelloMessage> hellos()
            {
                std::vector<Wire::HelloMessage> hellos;
                for (const RecordedFrame& frame : sent(MessageType::Hello))
                    hellos.push_back(decode<Wire::HelloMessage>(frame));
                return hellos;
            }

            /** @brief Connects, answers the newest HELLO with a good CHALLENGE and a WELCOME. */
            LinkChange register_master(std::uint64_t session_id, std::uint32_t liveness_ms)
            {
                link_.on_connected(peer());
                link_.on_challenge(peer(), challenge_of(secret_, session_id,
                                                          nonce_in(hellos().back())));
                return link_.on_welcome(peer(), welcome_of(liveness_ms, 4096));
            }

            std::vector<std::uint8_t> secret_;           /**< The key both sides hold. */
            TestSupport::ExecutorFabric fabric_;         /**< Carries what the link sends to a master end that records. */
            std::uint8_t drawn_ = 0;                     /**< Counts the nonces drawn. */
            MasterLink link_;                            /**< The link under test. */
    };

    /** @brief Builds a link on a clock the test moves. */
    class Small_MasterLink : public ::testing::Test
    {
        protected:
            /** @brief Builds the rig with the default options. */
            Small_MasterLink() : clock_(K_START_NS), rig_(clock_, link_options())
            {
            }

            /** @brief Moves the clock by @p step. */
            void advance(milliseconds step)
            {
                clock_.advance(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(step).count()));
            }

            ManualClock clock_;  /**< Moves only when the test says. */
            LinkRig rig_;        /**< The link and its transport. */
    };
} // namespace

/** @brief Tests that a new connection opens the handshake with a HELLO about this worker. */
TEST_F(Small_MasterLink, AConnectionOpensTheHandshakeWithHello)
{
    rig_.link_.on_connected(rig_.peer());

    ASSERT_EQ(rig_.hellos().size(), 1u);
    EXPECT_EQ(rig_.hellos()[0].node_name, "worker-a");
    EXPECT_EQ(rig_.hellos()[0].build_id, "build-1");
    EXPECT_FALSE(rig_.link_.master().has_value()) << "the link is not registered yet";
}

/** @brief Tests that the worker proves itself only after it checked the master's proof. */
TEST_F(Small_MasterLink, TheWorkerProvesItselfOnlyAfterTheMasterProved)
{
    rig_.link_.on_connected(rig_.peer());
    const HandshakeNonce worker_nonce = nonce_in(rig_.hellos()[0]);
    ASSERT_TRUE(rig_.sent(MessageType::Proof).empty());

    EXPECT_EQ(rig_.link_.on_challenge(rig_.peer(), challenge_of(rig_.secret_, 7, worker_nonce)),
              LinkChange::None);

    const std::vector<RecordedFrame> proofs = rig_.sent(MessageType::Proof);
    ASSERT_EQ(proofs.size(), 1u);
    const Wire::ProofMessage proof = decode<Wire::ProofMessage>(proofs[0]);
    const Sha256Digest expected =
        compute_proof(HandshakeRole::Worker,
                      span<const std::uint8_t>(rig_.secret_.data(), rig_.secret_.size()),
                      worker_nonce, nonce_of(0x22));
    EXPECT_EQ(std::memcmp(proof.worker_proof.data(), expected.data(), expected.size()), 0);
    EXPECT_EQ(proof.ops.size(), 2u);
    EXPECT_EQ(proof.max_jobs, 16u);
    EXPECT_EQ(proof.cache_budget, 4096u);
    EXPECT_EQ(rig_.link_.registrations(), 0u);

    EXPECT_EQ(rig_.link_.on_welcome(rig_.peer(), welcome_of(5000, 4096)),
              LinkChange::RegisteredNewSession);
    EXPECT_EQ(rig_.link_.registrations(), 1u);
    EXPECT_EQ(rig_.link_.chunk_bytes(), 4096u);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::Running);
    ASSERT_TRUE(rig_.link_.master().has_value());
    EXPECT_EQ(*rig_.link_.master(), rig_.peer());
}

/** @brief Tests that a master proving another key ends the link before the worker proves anything. */
TEST_F(Small_MasterLink, AMasterProvingAnotherKeyEndsTheLink)
{
    rig_.link_.on_connected(rig_.peer());

    const LinkChange change = rig_.link_.on_challenge(
        rig_.peer(), challenge_of(secret_of(0x43), 7, nonce_in(rig_.hellos()[0])));

    EXPECT_EQ(change, LinkChange::Ended);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::RejectedAuthentication);
    EXPECT_EQ(rig_.closed_count(), 1u) << "the connection of the unproven master stayed open";
    EXPECT_TRUE(rig_.sent(MessageType::Proof).empty())
        << "PROOF went to an unproven master";
    EXPECT_EQ(rig_.hellos().size(), 1u);
}

/** @brief Tests that an ended link stops its transport, so it connects no more. */
TEST_F(Small_MasterLink, AnEndedLinkStopsItsTransport)
{
    rig_.link_.on_connected(rig_.peer());
    ASSERT_EQ(rig_.link_.on_challenge(
                  rig_.peer(), challenge_of(secret_of(0x43), 7, nonce_in(rig_.hellos()[0]))),
              LinkChange::Ended);

    rig_.link_.step();
    rig_.link_.step();

    EXPECT_EQ(rig_.fabric_.worker()->send(rig_.peer(), MessageType::Ping, {}), SendStatus::Stopped);
    rig_.fabric_.advance(K_RECONNECT_NS);
    EXPECT_EQ(rig_.fabric_.master_events().connected().size(), 1u) << "the stopped transport connected again";
}

/** @brief Tests that a connection made after the link ended gets no HELLO and the master end sees it closed. */
TEST_F(Small_MasterLink, AConnectionAfterTheEndIsClosed)
{
    rig_.link_.on_connected(rig_.peer());
    ASSERT_EQ(rig_.link_.on_challenge(
                  rig_.peer(), challenge_of(secret_of(0x43), 7, nonce_in(rig_.hellos()[0]))),
              LinkChange::Ended);

    rig_.link_.on_connected(rig_.peer());

    EXPECT_EQ(rig_.hellos().size(), 1u);
    EXPECT_GE(rig_.closed_count(), 1u) << "the master end still sees the connection open";
}

/** @brief Tests that a worker answers PING only once WELCOME registered it. */
TEST_F(Small_MasterLink, PingGetsNoPongBeforeRegistration)
{
    Wire::PingMessage early;
    early.token = 98;
    Wire::PingMessage registered;
    registered.token = 99;

    // The master registered this worker on PROOF, but its WELCOME has not arrived.
    rig_.link_.on_connected(rig_.peer());
    rig_.link_.on_challenge(rig_.peer(), challenge_of(rig_.secret_, 7, nonce_in(rig_.hellos()[0])));
    rig_.link_.on_ping(rig_.peer(), early);
    EXPECT_TRUE(rig_.sent(MessageType::Pong).empty())
        << "a link that is not registered told the master it is alive";

    rig_.link_.on_welcome(rig_.peer(), welcome_of(0, 4096));
    rig_.link_.on_ping(rig_.peer(), registered);

    const std::vector<RecordedFrame> pongs = rig_.sent(MessageType::Pong);
    ASSERT_EQ(pongs.size(), 1u);
    EXPECT_EQ(decode<Wire::PongMessage>(pongs[0]).token, 99u);
}

/** @brief Tests that the PONG carries the free memory the probe reports. */
TEST_F(Small_MasterLink, ThePongCarriesTheFreeMemoryOfTheProbe)
{
    MasterLinkOptions options = link_options();
    options.free_memory_probe = []
    {
        return std::uint64_t{12345};
    };
    LinkRig rig(clock_, std::move(options));
    rig.register_master(7, 0);
    Wire::PingMessage ping;
    ping.token = 5;

    rig.link_.on_ping(rig.peer(), ping);

    ASSERT_EQ(rig.sent(MessageType::Pong).size(), 1u);
    EXPECT_EQ(decode<Wire::PongMessage>(rig.sent(MessageType::Pong)[0]).free_mem,
              12345u);
}

/** @brief Tests that a CHALLENGE for a HELLO the link forgot is ignored, not taken as wrong. */
TEST_F(Small_MasterLink, ALateChallengeForAForgottenHelloIsIgnored)
{
    constexpr std::size_t K_HELLOS = 5;
    rig_.link_.on_connected(rig_.peer());
    for (std::size_t sent = 1; sent < K_HELLOS; ++sent)
    {
        advance(milliseconds(10));
        rig_.link_.step();
    }
    ASSERT_EQ(rig_.hellos().size(), K_HELLOS);
    // The link remembers four nonces; the fifth HELLO forgets the nonce of the first.
    const HandshakeNonce first = nonce_in(rig_.hellos()[0]);

    const LinkChange change = rig_.link_.on_challenge(rig_.peer(), challenge_of(rig_.secret_, 7, first));

    EXPECT_EQ(change, LinkChange::None) << "a CHALLENGE for a forgotten HELLO was taken for a wrong secret";
    EXPECT_EQ(rig_.link_.end(), LinkEnd::Running);
    EXPECT_TRUE(rig_.sent(MessageType::Proof).empty());
}

/** @brief Tests that a CHALLENGE for an earlier HELLO still remembered is ignored. */
TEST_F(Small_MasterLink, AChallengeForAnEarlierHelloIsIgnored)
{
    rig_.link_.on_connected(rig_.peer());
    advance(milliseconds(10));
    rig_.link_.step();
    ASSERT_EQ(rig_.hellos().size(), 2u);

    const LinkChange change = rig_.link_.on_challenge(
        rig_.peer(), challenge_of(rig_.secret_, 7, nonce_in(rig_.hellos()[0])));

    EXPECT_EQ(change, LinkChange::None);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::Running);
    EXPECT_TRUE(rig_.sent(MessageType::Proof).empty());
}

/** @brief Tests that a registered link sends a HEARTBEAT every interval, carrying the probe. */
TEST_F(Small_MasterLink, ARegisteredLinkSendsAHeartbeatEveryInterval)
{
    MasterLinkOptions options = link_options();
    options.heartbeat_probe = []
    {
        Wire::HeartbeatMessage heartbeat;
        heartbeat.queue_depth = 3;
        heartbeat.oldest_held_task_id = 42;
        return heartbeat;
    };
    LinkRig rig(clock_, std::move(options));
    rig.register_master(7, 0);

    rig.link_.step();
    EXPECT_TRUE(rig.sent(MessageType::Heartbeat).empty());

    advance(milliseconds(10));
    rig.link_.step();

    const std::vector<RecordedFrame> beats = rig.sent(MessageType::Heartbeat);
    ASSERT_EQ(beats.size(), 1u);
    EXPECT_EQ(decode<Wire::HeartbeatMessage>(beats[0]).queue_depth, 3u);
    EXPECT_EQ(decode<Wire::HeartbeatMessage>(beats[0]).oldest_held_task_id, 42u);

    rig.link_.step();
    EXPECT_EQ(rig.sent(MessageType::Heartbeat).size(), 1u)
        << "the next heartbeat is an interval away";
}

/** @brief Tests that a master silent past its liveness timeout is closed, and may come back. */
TEST_F(Small_MasterLink, ASilentMasterIsClosedAndTheLinkMayReconnect)
{
    rig_.register_master(7, 40);

    advance(milliseconds(41));
    rig_.link_.step();

    ASSERT_EQ(rig_.closed_count(), 1u) << "a silent master was never closed";
    EXPECT_EQ(rig_.link_.on_lost(rig_.peer(), PeerLoss{PeerLossCause::ClosedLocally, 0, ""}),
              LinkChange::Disconnected);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::Running) << "a lost master ended a link that may reconnect";
}

/** @brief Tests that a frame from the master keeps its silence from being counted. */
TEST_F(Small_MasterLink, AFrameFromTheMasterRestartsTheSilence)
{
    rig_.register_master(7, 40);

    advance(milliseconds(30));
    rig_.link_.note_frame(rig_.peer());
    advance(milliseconds(30));
    rig_.link_.step();

    EXPECT_EQ(rig_.closed_count(), 0u) << "60 ms after the start is only 30 ms of silence";
}

/** @brief Tests that a new master session is told from a known one, and counted as a reconnect. */
TEST_F(Small_MasterLink, ANewSessionIsToldFromAKnownOne)
{
    EXPECT_EQ(rig_.register_master(7, 0), LinkChange::RegisteredNewSession);
    rig_.link_.on_lost(rig_.peer(), PeerLoss{});

    EXPECT_EQ(rig_.register_master(7, 0), LinkChange::Registered)
        << "the same session after a reconnect is not a new one";
    rig_.link_.on_lost(rig_.peer(), PeerLoss{});

    EXPECT_EQ(rig_.register_master(8, 0), LinkChange::RegisteredNewSession);
    EXPECT_EQ(rig_.link_.registrations(), 3u);
    EXPECT_EQ(rig_.link_.reconnects(), 2u);
}

/** @brief Tests that the worker's own close after GOODBYE does not end it as MasterLost. */
TEST_F(Small_MasterLink, ALossAfterGoodbyeIsTheWorkersOwn)
{
    rig_.register_master(7, 0);

    rig_.link_.say_goodbye();

    const std::vector<RecordedFrame> goodbyes = rig_.sent(MessageType::Goodbye);
    ASSERT_EQ(goodbyes.size(), 1u);
    EXPECT_EQ(decode<Wire::GoodbyeMessage>(goodbyes[0]).reason, Wire::GoodbyeReason::Shutdown);
    EXPECT_EQ(rig_.link_.on_lost(rig_.peer(), PeerLoss{}), LinkChange::None);
    EXPECT_EQ(rig_.link_.on_connect_abandoned(), LinkChange::None);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::Running);

    rig_.link_.stop();
    EXPECT_EQ(rig_.link_.end(), LinkEnd::Stopped);
}

/** @brief Tests that REJECT for a secret or a version ends the link and Busy does not. */
TEST_F(Small_MasterLink, ARejectForTheSecretOrTheVersionEndsTheLink)
{
    Wire::RejectMessage busy;
    busy.reason = Wire::RejectReason::Busy;
    rig_.link_.on_connected(rig_.peer());
    EXPECT_EQ(rig_.link_.on_reject(rig_.peer(), busy), LinkChange::None);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::Running);

    Wire::RejectMessage version;
    version.reason = Wire::RejectReason::Version;
    EXPECT_EQ(rig_.link_.on_reject(rig_.peer(), version), LinkChange::Ended);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::ProtocolMismatch);
}

/** @brief Tests that a transport that gave up ends the link by whether it ever registered. */
TEST_F(Small_MasterLink, AnAbandonedConnectEndsByWhetherTheLinkEverRegistered)
{
    EXPECT_EQ(rig_.link_.on_connect_abandoned(), LinkChange::Ended);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::MasterUnreachable);

    ManualClock clock(K_START_NS);
    LinkRig registered(clock, link_options());
    registered.register_master(7, 0);
    registered.link_.on_lost(registered.peer(), PeerLoss{});
    EXPECT_EQ(registered.link_.on_connect_abandoned(), LinkChange::Ended);
    EXPECT_EQ(registered.link_.end(), LinkEnd::MasterLost);
}

/** @brief Tests that HELLO cannot carry a fact above 255 bytes, and names the field. */
TEST_F(Small_MasterLink, AFactAbove255BytesIsNamed)
{
    MasterLinkOptions options = link_options();
    options.name = std::string(256, 'n');
    LinkRig rig(clock_, std::move(options));

    const std::optional<std::string> problem = rig.link_.unsendable_fact();

    ASSERT_TRUE(problem.has_value());
    EXPECT_NE(problem->find("the worker name"), std::string::npos);
    EXPECT_FALSE(rig_.link_.unsendable_fact().has_value());
}

/** @brief Tests that the thread sends heartbeats on the machine clock and stop() joins it. */
TEST(Medium_MasterLinkThread, TheThreadSendsHeartbeatsUntilStopped)
{
    SteadyClock clock;
    InProcessFabric fabric;
    RecordingTransportEvents master_events;
    RecordingTransportEvents worker_events;
    const std::shared_ptr<InProcessTransport> master = InProcessTransport::make_master(fabric);
    const std::shared_ptr<InProcessTransport> worker = InProcessTransport::make_worker(fabric);
    ASSERT_TRUE(master->start(master_events).is_ok());
    ASSERT_TRUE(worker->start(worker_events).is_ok());
    ASSERT_TRUE(worker_events.wait_for_connections(1, std::chrono::seconds(30)));
    const PeerID peer = worker_events.connected().back();
    const std::vector<std::uint8_t> secret = secret_of(K_SECRET_BYTE);
    std::uint8_t drawn = 0;
    MasterLinkOptions options = link_options();
    options.heartbeat_interval = milliseconds(2);
    MasterLink link(*worker, span<const std::uint8_t>(secret.data(), secret.size()),
                    std::move(options), [&drawn]() -> Result<HandshakeNonce>
                    {
                        return nonce_of(++drawn);
                    },
                    clock);
    link.on_connected(peer);
    ASSERT_TRUE(master_events.wait_for_frames(MessageType::Hello, 1, std::chrono::seconds(30)));
    link.on_challenge(peer, challenge_of(secret, 7,
                                         nonce_in(decode<Wire::HelloMessage>(
                                             master_events.frames_of(MessageType::Hello)[0]))));
    link.on_welcome(peer, welcome_of(0, 4096));

    link.start();
    const bool is_beating = master_events.wait_for_frames(MessageType::Heartbeat, 2,
                                                         std::chrono::seconds(30));
    link.stop();
    worker->stop();
    master->stop();

    EXPECT_TRUE(is_beating) << "the thread never sent two heartbeats";
    EXPECT_EQ(link.end(), LinkEnd::Stopped);
}
