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

#include "support/recording_transport.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using std::chrono::milliseconds;

namespace
{
    /** @brief Holds the peer the master connects as. */
    const PeerID K_MASTER{1};

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
        welcome.worker_id = K_MASTER.value;
        welcome.liveness_timeout_ms = liveness_timeout_ms;
        welcome.ping_interval_ms = 250;
        welcome.chunk_bytes = chunk_bytes;
        return welcome;
    }

    /** @brief Decodes the payload of a sent frame. */
    template <typename Message>
    Message decode(const TestSupport::SentFrame& frame)
    {
        Message message;
        const Wire::DecodeResult result = Message::decode(
            span<const std::byte>(frame.payload.data(), frame.payload.size()), message);
        EXPECT_TRUE(result.ok()) << "payload failed to decode at offset " << result.offset;
        return message;
    }

    /** @brief Returns the frames of one type the transport was given, in order. */
    std::vector<TestSupport::SentFrame> frames_of(const TestSupport::RecordingTransport& transport,
                                                  MessageType type)
    {
        std::vector<TestSupport::SentFrame> frames;
        for (const TestSupport::SentFrame& frame : transport.sent())
            if (frame.type == type)
                frames.push_back(frame);
        return frames;
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

    /** @brief Holds a link over a recording transport, with nonces 1, 2, 3 and so on. */
    class LinkRig
    {
        public:
            /** @brief Builds the link on @p clock with @p options. */
            LinkRig(const Clock& clock, MasterLinkOptions options)
                : secret_(secret_of(K_SECRET_BYTE)),
                  link_(transport_, span<const std::uint8_t>(secret_.data(), secret_.size()),
                        std::move(options), [this]() -> Result<HandshakeNonce>
                        {
                            return nonce_of(++drawn_);
                        },
                        clock)
            {
            }

            /** @brief Returns the HELLO frames sent so far, decoded. */
            std::vector<Wire::HelloMessage> hellos() const
            {
                std::vector<Wire::HelloMessage> hellos;
                for (const TestSupport::SentFrame& frame : frames_of(transport_, MessageType::Hello))
                    hellos.push_back(decode<Wire::HelloMessage>(frame));
                return hellos;
            }

            /** @brief Connects, answers the newest HELLO with a good CHALLENGE and a WELCOME. */
            LinkChange register_master(std::uint64_t session_id, std::uint32_t liveness_ms)
            {
                link_.on_connected(K_MASTER);
                link_.on_challenge(K_MASTER, challenge_of(secret_, session_id,
                                                          nonce_in(hellos().back())));
                return link_.on_welcome(K_MASTER, welcome_of(liveness_ms, 4096));
            }

            std::vector<std::uint8_t> secret_;           /**< The key both sides hold. */
            TestSupport::RecordingTransport transport_;  /**< Records what the link sends. */
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
    rig_.link_.on_connected(K_MASTER);

    ASSERT_EQ(rig_.hellos().size(), 1u);
    EXPECT_EQ(rig_.hellos()[0].node_name, "worker-a");
    EXPECT_EQ(rig_.hellos()[0].build_id, "build-1");
    EXPECT_FALSE(rig_.link_.master().has_value()) << "the link is not registered yet";
}

/** @brief Tests that the worker proves itself only after it checked the master's proof. */
TEST_F(Small_MasterLink, TheWorkerProvesItselfOnlyAfterTheMasterProved)
{
    rig_.link_.on_connected(K_MASTER);
    const HandshakeNonce worker_nonce = nonce_in(rig_.hellos()[0]);
    ASSERT_TRUE(frames_of(rig_.transport_, MessageType::Proof).empty());

    EXPECT_EQ(rig_.link_.on_challenge(K_MASTER, challenge_of(rig_.secret_, 7, worker_nonce)),
              LinkChange::None);

    const std::vector<TestSupport::SentFrame> proofs = frames_of(rig_.transport_, MessageType::Proof);
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

    EXPECT_EQ(rig_.link_.on_welcome(K_MASTER, welcome_of(5000, 4096)),
              LinkChange::RegisteredNewSession);
    EXPECT_EQ(rig_.link_.registrations(), 1u);
    EXPECT_EQ(rig_.link_.chunk_bytes(), 4096u);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::Running);
    ASSERT_TRUE(rig_.link_.master().has_value());
    EXPECT_EQ(*rig_.link_.master(), K_MASTER);
}

/** @brief Tests that a master proving another key ends the link before the worker proves anything. */
TEST_F(Small_MasterLink, AMasterProvingAnotherKeyEndsTheLink)
{
    rig_.link_.on_connected(K_MASTER);

    const LinkChange change = rig_.link_.on_challenge(
        K_MASTER, challenge_of(secret_of(0x43), 7, nonce_in(rig_.hellos()[0])));

    EXPECT_EQ(change, LinkChange::Ended);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::RejectedAuthentication);
    ASSERT_EQ(rig_.transport_.closed().size(), 1u);
    EXPECT_EQ(rig_.transport_.closed()[0], K_MASTER);
    EXPECT_TRUE(frames_of(rig_.transport_, MessageType::Proof).empty())
        << "PROOF went to an unproven master";
    EXPECT_EQ(rig_.hellos().size(), 1u);
}

/** @brief Tests that an ended link stops its transport once, so it connects no more. */
TEST_F(Small_MasterLink, AnEndedLinkStopsItsTransportOnce)
{
    rig_.link_.on_connected(K_MASTER);
    ASSERT_EQ(rig_.link_.on_challenge(
                  K_MASTER, challenge_of(secret_of(0x43), 7, nonce_in(rig_.hellos()[0]))),
              LinkChange::Ended);

    rig_.link_.step();
    rig_.link_.step();

    EXPECT_EQ(rig_.transport_.stops(), 1u);
}

/** @brief Tests that a connection made after the link ended is closed and gets no HELLO. */
TEST_F(Small_MasterLink, AConnectionAfterTheEndIsClosed)
{
    rig_.link_.on_connected(K_MASTER);
    ASSERT_EQ(rig_.link_.on_challenge(
                  K_MASTER, challenge_of(secret_of(0x43), 7, nonce_in(rig_.hellos()[0]))),
              LinkChange::Ended);

    rig_.link_.on_connected(PeerID{2});

    EXPECT_EQ(rig_.hellos().size(), 1u);
    EXPECT_EQ(rig_.transport_.closed().back(), PeerID{2});
}

/** @brief Tests that a worker answers PING only once WELCOME registered it. */
TEST_F(Small_MasterLink, PingGetsNoPongBeforeRegistration)
{
    Wire::PingMessage early;
    early.token = 98;
    Wire::PingMessage registered;
    registered.token = 99;

    // The master registered this worker on PROOF, but its WELCOME has not arrived.
    rig_.link_.on_connected(K_MASTER);
    rig_.link_.on_challenge(K_MASTER, challenge_of(rig_.secret_, 7, nonce_in(rig_.hellos()[0])));
    rig_.link_.on_ping(K_MASTER, early);
    EXPECT_TRUE(frames_of(rig_.transport_, MessageType::Pong).empty())
        << "a link that is not registered told the master it is alive";

    rig_.link_.on_welcome(K_MASTER, welcome_of(0, 4096));
    rig_.link_.on_ping(K_MASTER, registered);

    const std::vector<TestSupport::SentFrame> pongs = frames_of(rig_.transport_, MessageType::Pong);
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

    rig.link_.on_ping(K_MASTER, ping);

    ASSERT_EQ(frames_of(rig.transport_, MessageType::Pong).size(), 1u);
    EXPECT_EQ(decode<Wire::PongMessage>(frames_of(rig.transport_, MessageType::Pong)[0]).free_mem,
              12345u);
}

/** @brief Tests that a CHALLENGE for a HELLO the link forgot is ignored, not taken as wrong. */
TEST_F(Small_MasterLink, ALateChallengeForAForgottenHelloIsIgnored)
{
    constexpr std::size_t K_HELLOS = 5;
    rig_.link_.on_connected(K_MASTER);
    for (std::size_t sent = 1; sent < K_HELLOS; ++sent)
    {
        advance(milliseconds(10));
        rig_.link_.step();
    }
    ASSERT_EQ(rig_.hellos().size(), K_HELLOS);
    // The link remembers four nonces; the fifth HELLO forgets the nonce of the first.
    const HandshakeNonce first = nonce_in(rig_.hellos()[0]);

    const LinkChange change = rig_.link_.on_challenge(K_MASTER, challenge_of(rig_.secret_, 7, first));

    EXPECT_EQ(change, LinkChange::None) << "a CHALLENGE for a forgotten HELLO was taken for a wrong secret";
    EXPECT_EQ(rig_.link_.end(), LinkEnd::Running);
    EXPECT_TRUE(frames_of(rig_.transport_, MessageType::Proof).empty());
}

/** @brief Tests that a CHALLENGE for an earlier HELLO still remembered is ignored. */
TEST_F(Small_MasterLink, AChallengeForAnEarlierHelloIsIgnored)
{
    rig_.link_.on_connected(K_MASTER);
    advance(milliseconds(10));
    rig_.link_.step();
    ASSERT_EQ(rig_.hellos().size(), 2u);

    const LinkChange change = rig_.link_.on_challenge(
        K_MASTER, challenge_of(rig_.secret_, 7, nonce_in(rig_.hellos()[0])));

    EXPECT_EQ(change, LinkChange::None);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::Running);
    EXPECT_TRUE(frames_of(rig_.transport_, MessageType::Proof).empty());
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
    EXPECT_TRUE(frames_of(rig.transport_, MessageType::Heartbeat).empty());

    advance(milliseconds(10));
    rig.link_.step();

    const std::vector<TestSupport::SentFrame> beats = frames_of(rig.transport_, MessageType::Heartbeat);
    ASSERT_EQ(beats.size(), 1u);
    EXPECT_EQ(decode<Wire::HeartbeatMessage>(beats[0]).queue_depth, 3u);
    EXPECT_EQ(decode<Wire::HeartbeatMessage>(beats[0]).oldest_held_task_id, 42u);

    rig.link_.step();
    EXPECT_EQ(frames_of(rig.transport_, MessageType::Heartbeat).size(), 1u)
        << "the next heartbeat is an interval away";
}

/** @brief Tests that a master silent past its liveness timeout is closed, and may come back. */
TEST_F(Small_MasterLink, ASilentMasterIsClosedAndTheLinkMayReconnect)
{
    rig_.register_master(7, 40);

    advance(milliseconds(41));
    rig_.link_.step();

    ASSERT_EQ(rig_.transport_.closed().size(), 1u) << "a silent master was never closed";
    EXPECT_EQ(rig_.transport_.closed()[0], K_MASTER);
    EXPECT_EQ(rig_.link_.on_lost(K_MASTER, PeerLoss{PeerLossCause::ClosedLocally, 0, ""}),
              LinkChange::Disconnected);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::Running) << "a lost master ended a link that may reconnect";
}

/** @brief Tests that a frame from the master keeps its silence from being counted. */
TEST_F(Small_MasterLink, AFrameFromTheMasterRestartsTheSilence)
{
    rig_.register_master(7, 40);

    advance(milliseconds(30));
    rig_.link_.note_frame(K_MASTER);
    advance(milliseconds(30));
    rig_.link_.step();

    EXPECT_TRUE(rig_.transport_.closed().empty()) << "60 ms after the start is only 30 ms of silence";
}

/** @brief Tests that a new master session is told from a known one, and counted as a reconnect. */
TEST_F(Small_MasterLink, ANewSessionIsToldFromAKnownOne)
{
    EXPECT_EQ(rig_.register_master(7, 0), LinkChange::RegisteredNewSession);
    rig_.link_.on_lost(K_MASTER, PeerLoss{});

    EXPECT_EQ(rig_.register_master(7, 0), LinkChange::Registered)
        << "the same session after a reconnect is not a new one";
    rig_.link_.on_lost(K_MASTER, PeerLoss{});

    EXPECT_EQ(rig_.register_master(8, 0), LinkChange::RegisteredNewSession);
    EXPECT_EQ(rig_.link_.registrations(), 3u);
    EXPECT_EQ(rig_.link_.reconnects(), 2u);
}

/** @brief Tests that the worker's own close after GOODBYE does not end it as MasterLost. */
TEST_F(Small_MasterLink, ALossAfterGoodbyeIsTheWorkersOwn)
{
    rig_.register_master(7, 0);

    rig_.link_.say_goodbye();

    const std::vector<TestSupport::SentFrame> goodbyes = frames_of(rig_.transport_, MessageType::Goodbye);
    ASSERT_EQ(goodbyes.size(), 1u);
    EXPECT_EQ(decode<Wire::GoodbyeMessage>(goodbyes[0]).reason, Wire::GoodbyeReason::Shutdown);
    EXPECT_EQ(rig_.link_.on_lost(K_MASTER, PeerLoss{}), LinkChange::None);
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
    rig_.link_.on_connected(K_MASTER);
    EXPECT_EQ(rig_.link_.on_reject(K_MASTER, busy), LinkChange::None);
    EXPECT_EQ(rig_.link_.end(), LinkEnd::Running);

    Wire::RejectMessage version;
    version.reason = Wire::RejectReason::Version;
    EXPECT_EQ(rig_.link_.on_reject(K_MASTER, version), LinkChange::Ended);
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
    registered.link_.on_lost(K_MASTER, PeerLoss{});
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
    MasterLinkOptions options = link_options();
    options.heartbeat_interval = milliseconds(2);
    LinkRig rig(clock, std::move(options));
    rig.register_master(7, 0);

    rig.link_.start();
    for (int attempt = 0; attempt < 2000 &&
                          frames_of(rig.transport_, MessageType::Heartbeat).size() < 2; ++attempt)
        std::this_thread::sleep_for(milliseconds(1));
    rig.link_.stop();

    EXPECT_GE(frames_of(rig.transport_, MessageType::Heartbeat).size(), 2u)
        << "the thread never sent two heartbeats";
    EXPECT_EQ(rig.link_.end(), LinkEnd::Stopped);
}
