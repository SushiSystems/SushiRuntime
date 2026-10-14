/**************************************************************************/
/* test_worker_main.cpp                                                   */
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
 * @file test_worker_main.cpp
 * @brief Tests the worker's option parser, its stop flag and each exit code of a worker.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>
#include <SushiRuntime/distributed/worker_main.hpp>
#include <SushiRuntime/transport/endpoint.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/link_security/handshake.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>


using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Bounds every wait on a condition, so a broken worker fails a test and hangs none. */
    constexpr std::chrono::seconds K_WAIT{60};

    /** @brief Writes a file in the working directory and removes it when the test ends. */
    class ScratchFile final
    {
        public:
            /** @brief Writes @p size bytes to a file called @p name. */
            ScratchFile(const std::string& name, std::size_t size)
                : path_(std::filesystem::current_path() / name)
            {
                std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
                const std::vector<char> bytes(size, 'k');
                stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            }

            /** @brief Removes the file. */
            ~ScratchFile()
            {
                std::error_code ignored;
                std::filesystem::remove(path_, ignored);
            }

            ScratchFile(const ScratchFile&) = delete;
            ScratchFile& operator=(const ScratchFile&) = delete;
            ScratchFile(ScratchFile&&) = delete;
            ScratchFile& operator=(ScratchFile&&) = delete;

            /** @brief Returns where the file is. */
            const std::filesystem::path& path() const noexcept
            {
                return path_;
            }

        private:
            /** @brief Holds the absolute path of the file. */
            std::filesystem::path path_;
    };

    /** @brief Names what a fake master does with a worker that says HELLO. */
    enum class MasterScript : std::uint8_t
    {
        RejectAuthentication, /**< Answers HELLO with REJECT(authentication). */
        RejectVersion,        /**< Answers HELLO with REJECT(version). */
        Register,             /**< Proves the empty secret and welcomes the worker. */
        RegisterThenClose,    /**< Welcomes the worker, then closes its connection. */
    };

    /**
     * @brief Plays the master's frames of one script over a master transport it starts.
     *
     * It verifies no proof and dispatches nothing; the secret is empty on both sides.
     */
    class FakeMaster final : public TransportEvents
    {
        public:
            /**
             * @brief Starts a master endpoint and answers its workers.
             * @param transport Not null and not started.
             */
            FakeMaster(MasterScript script, std::shared_ptr<Transport> transport)
                : script_(script), transport_(std::move(transport))
            {
                started_ = transport_->start(*this);
            }

            /** @brief Stops the transport and joins its threads. */
            ~FakeMaster()
            {
                transport_->stop();
            }

            FakeMaster(const FakeMaster&) = delete;
            FakeMaster& operator=(const FakeMaster&) = delete;
            FakeMaster(FakeMaster&&) = delete;
            FakeMaster& operator=(FakeMaster&&) = delete;

            /** @brief Returns how the transport started. */
            const TransportResult& started() const noexcept
            {
                return started_;
            }

            /** @brief Waits until a welcomed worker has sent a HEARTBEAT, so it is registered. */
            bool wait_for_heartbeat()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT, [&] { return heartbeat_; });
            }

            /** @brief Waits until a worker has said GOODBYE. */
            bool wait_for_goodbye()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT, [&] { return goodbye_; });
            }

            /** @brief Does nothing; the script starts at HELLO. */
            void on_peer_connected(PeerID, const PeerDescription&) override
            {

            }

            /** @brief Answers HELLO and PROOF as the script says; notes HEARTBEAT and GOODBYE. */
            void on_frame(PeerID peer, MessageType type, std::uint8_t,
                          span<const std::byte> payload) override
            {
                if (type == MessageType::Hello)
                    answer_hello(peer, payload);
                else if (type == MessageType::Proof)
                    answer_proof(peer);
                else if (type == MessageType::Heartbeat)
                    note([&] { heartbeat_ = true; });
                else if (type == MessageType::Goodbye)
                    note([&] { goodbye_ = true; });
            }

            /** @brief Does nothing; the tests watch the worker's exit code. */
            void on_peer_lost(PeerID, const PeerLoss&) override
            {

            }

            /** @brief Does nothing; a master transport never calls it. */
            void on_connect_abandoned(const std::string&) override
            {

            }

        private:
            /** @brief Encodes a message and queues it as one control frame. */
            template <typename Message>
            void send(PeerID peer, const Message& message)
            {
                std::vector<std::byte> payload;
                ASSERT_EQ(message.encode(payload), Wire::CodecStatus::Ok);
                (void)transport_->send(peer, Message::K_TYPE,
                                       span<const std::byte>(payload.data(), payload.size()));
            }

            /** @brief Changes a flag under the mutex and wakes the test. */
            template <typename Change>
            void note(Change change)
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    change();
                }
                changed_.notify_all();
            }

            /** @brief Refuses the worker, or sends CHALLENGE proved with the empty secret. */
            void answer_hello(PeerID peer, span<const std::byte> payload)
            {
                Wire::HelloMessage hello;
                ASSERT_TRUE(Wire::HelloMessage::decode(payload, hello).ok());

                if (script_ == MasterScript::RejectAuthentication ||
                    script_ == MasterScript::RejectVersion)
                {
                    Wire::RejectMessage reject;
                    reject.reason = script_ == MasterScript::RejectVersion
                                        ? Wire::RejectReason::Version
                                        : Wire::RejectReason::Authentication;
                    reject.sender_version = script_ == MasterScript::RejectVersion
                                                ? std::uint16_t{2}
                                                : Wire::K_PROTOCOL_VERSION;
                    reject.text = "refused by the test's master";
                    send(peer, reject);
                    return;
                }

                HandshakeNonce worker_nonce{};
                std::memcpy(worker_nonce.bytes.data(), hello.worker_nonce.data(),
                            worker_nonce.bytes.size());
                HandshakeNonce master_nonce{};
                master_nonce.bytes.fill(0x5a);
                const Sha256Digest proof = compute_proof(
                    HandshakeRole::Master, span<const std::uint8_t>(), worker_nonce, master_nonce);

                Wire::ChallengeMessage challenge;
                std::memcpy(challenge.master_nonce.data(), master_nonce.bytes.data(),
                            challenge.master_nonce.size());
                std::memcpy(challenge.master_proof.data(), proof.data(),
                            challenge.master_proof.size());
                challenge.session_id = 7;
                send(peer, challenge);
            }

            /** @brief Welcomes the worker; the closing script then ends the connection. */
            void answer_proof(PeerID peer)
            {
                Wire::WelcomeMessage welcome;
                welcome.worker_id = peer.value;
                welcome.liveness_timeout_ms = 600000;
                welcome.ping_interval_ms = 250;
                welcome.chunk_bytes = 262144;
                send(peer, welcome);

                if (script_ == MasterScript::RegisterThenClose)
                    transport_->close_peer(peer);
            }

            /// @brief What the master does with a worker.
            MasterScript script_;

            /// @brief The master endpoint.
            std::shared_ptr<Transport> transport_;

            /// @brief How the transport started.
            TransportResult started_;

            /// @brief Guards the two flags below.
            std::mutex mutex_;

            /// @brief Wakes a test that waits for a flag.
            std::condition_variable changed_;

            /// @brief Whether a HEARTBEAT arrived.
            bool heartbeat_ = false;

            /// @brief Whether GOODBYE arrived.
            bool goodbye_ = false;
    };

    /** @brief Holds the connect timeout of a worker whose master is never started. */
    constexpr std::chrono::seconds K_NO_MASTER_TIMEOUT{1};

    /** @brief Holds a connect timeout no test of a started master reaches. */
    constexpr std::chrono::seconds K_MASTER_TIMEOUT{20};

    /** @brief Returns the options of a worker that joins a master on loopback without a secret. */
    WorkerOptions options_for(const Endpoint& master,
                              std::chrono::seconds connect_timeout = K_MASTER_TIMEOUT)
    {
        WorkerOptions options;
        options.master = master;
        options.name = "test-worker";
        options.connect_timeout = connect_timeout;
        options.reconnect = false;
        return options;
    }

    /**
     * @brief Runs run_worker() against a master endpoint on a thread of its own.
     *
     * The worker joins without a secret and does not reconnect, so it ends when its master
     * refuses it or goes away. The destructor stops the master first, so a test that fails
     * early still joins the thread.
     */
    class ServedWorker final
    {
        public:
            /**
             * @brief Starts the worker.
             * @param master The endpoint the worker connects to.
             * @param master_transport The master's transport, stopped by the destructor; may be
             *        null when no master is started.
             */
            ServedWorker(const Endpoint& master, std::chrono::seconds connect_timeout,
                         std::shared_ptr<Transport> master_transport = nullptr)
                : master_transport_(std::move(master_transport)),
                  exit_code_(std::async(std::launch::async,
                                        [options = options_for(master, connect_timeout)]
                                        {
                                            return run_worker(options,
                                                              make_sushi<KernelRegistry>());
                                        }))
            {

            }

            /** @brief Stops the master; the future then waits for the worker's thread. */
            ~ServedWorker()
            {
                if (master_transport_ != nullptr)
                    master_transport_->stop();
            }

            ServedWorker(const ServedWorker&) = delete;
            ServedWorker& operator=(const ServedWorker&) = delete;
            ServedWorker(ServedWorker&&) = delete;
            ServedWorker& operator=(ServedWorker&&) = delete;

            /**
             * @brief Raises SIGINT, the first signal a worker process serves.
             *
             * Called only while run_worker() runs, so its handler, not the default, receives it.
             */
            static void request_stop() noexcept
            {
                std::raise(SIGINT);
            }

            /** @brief Returns the exit code run_worker() gave, or nothing after K_WAIT. */
            std::optional<WorkerExitCode> exit_code()
            {
                if (exit_code_.wait_for(K_WAIT) != std::future_status::ready)
                    return std::nullopt;
                return static_cast<WorkerExitCode>(exit_code_.get());
            }

        private:
            /// @brief The master's transport, or null.
            std::shared_ptr<Transport> master_transport_;

            /// @brief The exit code of the worker's thread; declared last, so it ends first.
            std::future<int> exit_code_;
    };
} // namespace

/** @brief Tests that a secret file that does not exist exits 1 and names the file. */
TEST(Unit_WorkerMainTest, UnreadableSecretFileExitsOne)
{
    WorkerOptions options = options_for(Endpoint{"127.0.0.1", 1});
    options.secret_file = "sr_test_worker_secret_that_is_not_there.key";

    testing::internal::CaptureStderr();
    const int exit_code = run_worker(options, make_sushi<KernelRegistry>());
    const std::string line = testing::internal::GetCapturedStderr();

    EXPECT_EQ(exit_code, static_cast<int>(WorkerExitCode::BadArguments));
    EXPECT_NE(line.find("sr_test_worker_secret_that_is_not_there.key"), std::string::npos)
        << line;
}

/** @brief Tests that a secret file of 31 bytes exits 1 and names the length it needs. */
TEST(Unit_WorkerMainTest, ShortSecretFileExitsOne)
{
    const ScratchFile file("sr_test_worker_secret_short.key", 31);
    WorkerOptions options = options_for(Endpoint{"127.0.0.1", 1});
    options.secret_file = file.path();

    testing::internal::CaptureStderr();
    const int exit_code = run_worker(options, make_sushi<KernelRegistry>());
    const std::string line = testing::internal::GetCapturedStderr();

    EXPECT_EQ(exit_code, static_cast<int>(WorkerExitCode::BadArguments));
    EXPECT_NE(line.find("31 bytes"), std::string::npos) << line;
    EXPECT_NE(line.find("at least 32"), std::string::npos) << line;
}

/** @brief Tests that a log level outside the five names exits 1 and lists them. */
TEST(Unit_WorkerMainTest, UnknownLogLevelExitsOne)
{
    WorkerOptions options = options_for(Endpoint{"127.0.0.1", 1});
    options.log_level = "chatty";

    testing::internal::CaptureStderr();
    const int exit_code = run_worker(options, make_sushi<KernelRegistry>());
    const std::string line = testing::internal::GetCapturedStderr();

    EXPECT_EQ(exit_code, static_cast<int>(WorkerExitCode::BadArguments));
    EXPECT_NE(line.find("'chatty'"), std::string::npos) << line;
    EXPECT_NE(line.find("off, error, warn, info, debug"), std::string::npos) << line;
}

/** @brief Tests that the command line overload prints the parser's line and exits 1. */
TEST(Unit_WorkerMainTest, BadFlagExitsOne)
{
    char program[] = "worker";
    char flag[] = "--frobnicate";
    char* arguments[] = {program, flag, nullptr};

    testing::internal::CaptureStderr();
    const int exit_code = run_worker(2, arguments, make_sushi<KernelRegistry>());
    const std::string line = testing::internal::GetCapturedStderr();

    EXPECT_EQ(exit_code, static_cast<int>(WorkerExitCode::BadArguments));
    EXPECT_NE(line.find("unknown argument '--frobnicate'"), std::string::npos) << line;
}

/** @brief Tests that `--version` prints the protocol version and exits 0. */
TEST(Unit_WorkerMainTest, VersionPrintsTheProtocolAndExitsZero)
{
    char program[] = "worker";
    char flag[] = "--version";
    char* arguments[] = {program, flag, nullptr};

    testing::internal::CaptureStdout();
    const int exit_code = run_worker(2, arguments, make_sushi<KernelRegistry>());
    const std::string line = testing::internal::GetCapturedStdout();

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(line.find("protocol v" + std::to_string(Wire::K_PROTOCOL_VERSION)),
              std::string::npos)
        << line;
    EXPECT_NE(line.find("build "), std::string::npos) << line;
}

/** @brief Tests that a registered worker stops in order when the stop flag is set: code 0. */
TEST(Integration_WorkerMainTest, SignalStopsWithExitZero)
{
    const std::shared_ptr<TCPTransport> listener = TCPTransport::make_master();
    FakeMaster master(MasterScript::Register, listener);
    ASSERT_TRUE(master.started().is_ok()) << master.started().message;
    ServedWorker worker(listener->local_endpoint(), K_MASTER_TIMEOUT, listener);

    ASSERT_TRUE(master.wait_for_heartbeat());
    worker.request_stop();

    const std::optional<WorkerExitCode> exit_code = worker.exit_code();
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, WorkerExitCode::StoppedBySignal);
    EXPECT_TRUE(master.wait_for_goodbye());
}

/** @brief Tests that REJECT(authentication) ends the worker with exit code 3. */
TEST(Integration_WorkerMainTest, AuthenticationRejectExitsThree)
{
    const std::shared_ptr<TCPTransport> listener = TCPTransport::make_master();
    FakeMaster master(MasterScript::RejectAuthentication, listener);
    ASSERT_TRUE(master.started().is_ok()) << master.started().message;
    ServedWorker worker(listener->local_endpoint(), K_MASTER_TIMEOUT, listener);

    const std::optional<WorkerExitCode> exit_code = worker.exit_code();
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, WorkerExitCode::RejectedAuthentication);
}

/** @brief Tests that REJECT(version) ends the worker with exit code 4. */
TEST(Integration_WorkerMainTest, VersionRejectExitsFour)
{
    const std::shared_ptr<TCPTransport> listener = TCPTransport::make_master();
    FakeMaster master(MasterScript::RejectVersion, listener);
    ASSERT_TRUE(master.started().is_ok()) << master.started().message;
    ServedWorker worker(listener->local_endpoint(), K_MASTER_TIMEOUT, listener);

    const std::optional<WorkerExitCode> exit_code = worker.exit_code();
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, WorkerExitCode::ProtocolMismatch);
}

/** @brief Tests that a master nobody listens for ends the worker with exit code 2. */
TEST(Integration_WorkerMainTest, MasterNotReachedExitsTwo)
{
    ServedWorker worker(Endpoint{"127.0.0.1", 1}, K_NO_MASTER_TIMEOUT);

    const std::optional<WorkerExitCode> exit_code = worker.exit_code();
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, WorkerExitCode::MasterUnreachable);
}

/** @brief Tests that losing the master with reconnecting off ends the worker with exit code 5. */
TEST(Integration_WorkerMainTest, MasterLostWithoutReconnectExitsFive)
{
    const std::shared_ptr<TCPTransport> listener = TCPTransport::make_master();
    FakeMaster master(MasterScript::RegisterThenClose, listener);
    ASSERT_TRUE(master.started().is_ok()) << master.started().message;
    ServedWorker worker(listener->local_endpoint(), K_MASTER_TIMEOUT, listener);

    const std::optional<WorkerExitCode> exit_code = worker.exit_code();
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, WorkerExitCode::MasterLost);
}

/** @brief Tests that run_worker() joins a master over TCP and returns the code of its end. */
TEST(Integration_WorkerMainTest, RunWorkerOverTCPReturnsTheCodeOfItsEnd)
{
    const std::shared_ptr<TCPTransport> listener = TCPTransport::make_master();
    FakeMaster master(MasterScript::RegisterThenClose, listener);
    ASSERT_TRUE(master.started().is_ok()) << master.started().message;

    std::future<int> worker = std::async(
        std::launch::async, [options = options_for(listener->local_endpoint())]
        {
            return run_worker(options, make_sushi<KernelRegistry>());
        });

    ASSERT_EQ(worker.wait_for(K_WAIT), std::future_status::ready);
    EXPECT_EQ(worker.get(), static_cast<int>(WorkerExitCode::MasterLost));
}
