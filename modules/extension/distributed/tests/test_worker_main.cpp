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
 * @author Mustafa Garip
 */

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
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
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/distributed/endpoint.hpp>
#include <SushiRuntime/distributed/transport.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>
#include <SushiRuntime/distributed/worker_main.hpp>

#include "environment.hpp"
#include "handshake.hpp"
#include "stop_signal.hpp"
#include "wire_frame.hpp"
#include "wire_messages.hpp"
#include "worker_options_parser.hpp"
#include "worker_session.hpp"
#include "test_context.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Bounds every wait on a condition, so a broken worker fails a test and hangs none. */
    constexpr std::chrono::seconds K_WAIT{60};

    /** @brief Names the variables a parser test pretends the process has. */
    using FakeEnvironment = std::map<std::string, std::string>;

    /** @brief Returns a reader over a table, so no test changes the process's environment. */
    EnvironmentReader reader_of(FakeEnvironment variables)
    {
        return [variables = std::move(variables)](const char* name) -> std::optional<std::string>
        {
            const auto found = variables.find(name);
            if (found == variables.end())
                return std::nullopt;
            return found->second;
        };
    }

    /** @brief Parses a command line, given without the program's name, over a fake environment. */
    WorkerOptionsResult parse(const std::vector<const char*>& arguments,
                              FakeEnvironment variables = FakeEnvironment())
    {
        return parse_worker_options(
            span<const char* const>(arguments.data(), arguments.size()),
            reader_of(std::move(variables)));
    }

    /** @brief Holds one command line the parser must refuse and a part of its message. */
    struct RefusedCase
    {
        std::vector<const char*> arguments; /**< The flags. */
        FakeEnvironment variables;          /**< The environment. */
        std::string expected;               /**< Text the message must hold. */
    };

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
                                                ? std::uint16_t{3}
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

    /** @brief Holds the time a served worker waits for a master no test starts. */
    constexpr std::chrono::milliseconds K_NO_MASTER_TIMEOUT{50};

    /** @brief Holds a connect timeout no test of a started master reaches. */
    constexpr std::chrono::milliseconds K_MASTER_TIMEOUT = K_WAIT;

    /**
     * @brief Serves the master of a fabric with serve_worker() on a thread of its own.
     *
     * The worker joins without a secret and does not reconnect. The destructor asks for the
     * stop, so a test that fails early still joins the thread.
     */
    class ServedWorker final
    {
        public:
            /** @brief Starts serving; a worker endpoint is added to the fabric. */
            ServedWorker(InProcessFabric& fabric, std::chrono::milliseconds connect_timeout)
                : exit_code_(std::async(
                      std::launch::async,
                      [this, transport = InProcessTransport::make_worker(
                                 fabric, link_options(connect_timeout))]
                      {
                          return serve_worker(context_, transport, make_sushi<KernelRegistry>(),
                                              agent_options(), stop_signal_);
                      }))
            {

            }

            /** @brief Asks for the stop; the future then waits for the serving thread. */
            ~ServedWorker()
            {
                stop_signal_.request();
            }

            ServedWorker(const ServedWorker&) = delete;
            ServedWorker& operator=(const ServedWorker&) = delete;
            ServedWorker(ServedWorker&&) = delete;
            ServedWorker& operator=(ServedWorker&&) = delete;

            /** @brief Sets the stop flag as the first signal of a worker process does. */
            void request_stop() noexcept
            {
                stop_signal_.request();
            }

            /** @brief Returns the exit code serve_worker() gave, or nothing after K_WAIT. */
            std::optional<WorkerExitCode> exit_code()
            {
                if (exit_code_.wait_for(K_WAIT) != std::future_status::ready)
                    return std::nullopt;
                return exit_code_.get();
            }

        private:
            /** @brief Returns the settings of an endpoint that gives up where TCP would. */
            static InProcessWorkerOptions link_options(std::chrono::milliseconds connect_timeout)
            {
                InProcessWorkerOptions options;
                options.reconnect = false;
                options.connect_timeout = connect_timeout;
                return options;
            }

            /** @brief Returns the settings of an agent whose heartbeat a test can wait for. */
            static WorkerAgentOptions agent_options()
            {
                WorkerAgentOptions options;
                options.name = "test-worker";
                options.heartbeat_interval = std::chrono::milliseconds(20);
                return options;
            }

            /// @brief The flag serve_worker() reads; a test sets it with request_stop().
            StopSignal stop_signal_;

            /// @brief The runtime the agent's kernels would run on.
            const std::shared_ptr<Internal::Execution::RuntimeContext> context_owner_ =
                SushiRuntime::Test::make_context();

            /// @brief The context context_owner_ holds.
            Internal::Execution::RuntimeContext& context_ = *context_owner_;

            /// @brief The exit code of the serving thread; declared last, so it ends first.
            std::future<WorkerExitCode> exit_code_;
    };

    /** @brief Returns the options of a worker that joins a master on loopback without a secret. */
    WorkerOptions options_for(const Endpoint& master)
    {
        WorkerOptions options;
        options.master = master;
        options.name = "test-worker";
        options.connect_timeout = std::chrono::seconds(20);
        options.reconnect = false;
        return options;
    }
} // namespace

/** @brief Tests that the master alone gives the defaults of the flag table. */
TEST(Unit_WorkerOptionsParserTest, MasterAloneGivesTheDefaults)
{
    const WorkerOptionsResult result = parse({"--master", "node7:7000"});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    EXPECT_EQ(result.options.master.host, "node7");
    EXPECT_EQ(result.options.master.port, 7000);
    EXPECT_TRUE(result.options.secret_file.empty());
    EXPECT_TRUE(result.options.name.empty());
    EXPECT_EQ(result.options.max_jobs, 1024u);
    EXPECT_EQ(result.options.cache_bytes, 0u);
    EXPECT_EQ(result.options.heartbeat_interval, std::chrono::milliseconds(1000));
    EXPECT_EQ(result.options.pull_idle_timeout, std::chrono::milliseconds(5000));
    EXPECT_EQ(result.options.connect_timeout, std::chrono::seconds(60));
    EXPECT_TRUE(result.options.reconnect);
    EXPECT_TRUE(result.options.log_level.empty());
}

/** @brief Tests every flag, half written `--flag value` and half `--flag=value`. */
TEST(Unit_WorkerOptionsParserTest, EveryFlagIsRead)
{
    const WorkerOptionsResult result = parse(
        {"--master", "[::1]:7001", "--secret-file=keys/cluster.key", "--name", "gpu-box",
         "--max-jobs=8", "--cache-bytes", "4096", "--heartbeat-ms=250",
         "--pull-idle-timeout-ms", "0", "--connect-timeout-s=0", "--no-reconnect",
         "--log-level", "debug"});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    EXPECT_EQ(result.options.master.host, "::1");
    EXPECT_EQ(result.options.master.port, 7001);
    EXPECT_EQ(result.options.secret_file, std::filesystem::path("keys/cluster.key"));
    EXPECT_EQ(result.options.name, "gpu-box");
    EXPECT_EQ(result.options.max_jobs, 8u);
    EXPECT_EQ(result.options.cache_bytes, 4096u);
    EXPECT_EQ(result.options.heartbeat_interval, std::chrono::milliseconds(250));
    EXPECT_EQ(result.options.pull_idle_timeout, std::chrono::milliseconds(0));
    EXPECT_EQ(result.options.connect_timeout, std::chrono::seconds(0));
    EXPECT_FALSE(result.options.reconnect);
    EXPECT_EQ(result.options.log_level, "debug");
}

/** @brief Tests that each variable of the table fills the setting no flag gives. */
TEST(Unit_WorkerOptionsParserTest, EnvironmentFillsWhatNoFlagGives)
{
    const WorkerOptionsResult result = parse(
        {}, {{"SR_MASTER", "10.0.0.5:7002"},
             {"SR_CLUSTER_SECRET_FILE", "from_environment.key"},
             {"SR_WORKER_NAME", "named-by-variable"},
             {"SR_WORKER_MAX_JOBS", "16"},
             {"SR_WORKER_CACHE_BYTES", "1048576"},
             {"SR_WORKER_HEARTBEAT_MS", "500"},
             {"SR_WORKER_PULL_IDLE_TIMEOUT_MS", "9000"},
             {"SR_WORKER_CONNECT_TIMEOUT_S", "5"}});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    EXPECT_EQ(result.options.master.host, "10.0.0.5");
    EXPECT_EQ(result.options.master.port, 7002);
    EXPECT_EQ(result.options.secret_file, std::filesystem::path("from_environment.key"));
    EXPECT_EQ(result.options.name, "named-by-variable");
    EXPECT_EQ(result.options.max_jobs, 16u);
    EXPECT_EQ(result.options.cache_bytes, 1048576u);
    EXPECT_EQ(result.options.heartbeat_interval, std::chrono::milliseconds(500));
    EXPECT_EQ(result.options.pull_idle_timeout, std::chrono::milliseconds(9000));
    EXPECT_EQ(result.options.connect_timeout, std::chrono::seconds(5));
    EXPECT_TRUE(result.options.reconnect);
}

/** @brief Tests that a flag beats its variable and that a variable behind a flag is unread. */
TEST(Unit_WorkerOptionsParserTest, FlagBeatsEnvironmentBeatsDefault)
{
    const WorkerOptionsResult result = parse(
        {"--master", "flag-host:1", "--max-jobs", "3"},
        {{"SR_MASTER", "variable-host:2"},
         {"SR_WORKER_MAX_JOBS", "not a number"},
         {"SR_WORKER_HEARTBEAT_MS", "40"}});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    EXPECT_EQ(result.options.master.host, "flag-host");
    EXPECT_EQ(result.options.max_jobs, 3u);
    EXPECT_EQ(result.options.heartbeat_interval, std::chrono::milliseconds(40));
    EXPECT_EQ(result.options.pull_idle_timeout, std::chrono::milliseconds(5000));
}

/** @brief Tests that a variable set to nothing leaves the default in place. */
TEST(Unit_WorkerOptionsParserTest, EmptyVariableCountsAsUnset)
{
    const WorkerOptionsResult result =
        parse({"--master", "node7:7000"}, {{"SR_WORKER_MAX_JOBS", ""}, {"SR_WORKER_NAME", ""}});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    EXPECT_EQ(result.options.max_jobs, 1024u);
    EXPECT_TRUE(result.options.name.empty());
}

/** @brief Tests that `--help` wins without a master and that its text names every flag. */
TEST(Unit_WorkerOptionsParserTest, HelpListsEveryFlagAndVariable)
{
    const WorkerOptionsResult result = parse({"--help"});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Help);
    for (const char* word :
         {"--master", "--secret-file", "--name", "--max-jobs", "--cache-bytes", "--heartbeat-ms",
          "--pull-idle-timeout-ms", "--connect-timeout-s", "--no-reconnect", "--log-level",
          "--version", "--help", "SR_MASTER", "SR_CLUSTER_SECRET_FILE", "SR_WORKER_NAME",
          "SR_WORKER_MAX_JOBS", "SR_WORKER_CACHE_BYTES", "SR_WORKER_HEARTBEAT_MS",
          "SR_WORKER_PULL_IDLE_TIMEOUT_MS", "SR_WORKER_CONNECT_TIMEOUT_S"})
        EXPECT_NE(result.message.find(word), std::string::npos) << word;
}

/** @brief Tests that `--version` is reported without a master. */
TEST(Unit_WorkerOptionsParserTest, VersionNeedsNoMaster)
{
    EXPECT_EQ(parse({"--version"}).status, WorkerOptionsStatus::Version);
}

/** @brief Tests each command line the parser refuses and what its one line names. */
TEST(Unit_WorkerOptionsParserTest, EachRefusalNamesWhatWasWrong)
{
    const std::vector<RefusedCase> cases = {
        {{}, {}, "no master given"},
        {{"--master"}, {}, "--master needs a value"},
        {{"--master", "node7"}, {}, "--master: "},
        {{"--master", "node7:99999"}, {}, "--master: "},
        {{"--master", "node7:0"}, {}, "port 0"},
        {{}, {{"SR_MASTER", "::1:7000"}}, "SR_MASTER: "},
        {{"--master", "a:1", "--frobnicate"}, {}, "unknown argument '--frobnicate'"},
        {{"--master", "a:1", "stray"}, {}, "unknown argument 'stray'"},
        {{"--master", "a:1", "--no-reconnect=yes"}, {}, "--no-reconnect takes no value"},
        {{"--master", "a:1", "--max-jobs", "0"}, {}, "--max-jobs: '0' is not a whole number"},
        {{"--master", "a:1", "--max-jobs", "4294967296"}, {}, "--max-jobs: "},
        {{"--master", "a:1", "--max-jobs", "12x"}, {}, "--max-jobs: '12x'"},
        {{"--master", "a:1", "--max-jobs", "-1"}, {}, "--max-jobs: '-1'"},
        {{"--master", "a:1", "--max-jobs="}, {}, "--max-jobs: ''"},
        {{"--master", "a:1", "--cache-bytes", "many"}, {}, "--cache-bytes: 'many'"},
        {{"--master", "a:1", "--heartbeat-ms", "0"}, {}, "--heartbeat-ms: '0'"},
        {{"--master", "a:1", "--pull-idle-timeout-ms", "1.5"}, {}, "--pull-idle-timeout-ms: "},
        {{"--master", "a:1", "--connect-timeout-s", "soon"}, {}, "--connect-timeout-s: 'soon'"},
        {{"--master", "a:1"}, {{"SR_WORKER_HEARTBEAT_MS", "fast"}},
         "SR_WORKER_HEARTBEAT_MS: 'fast'"},
    };

    for (const RefusedCase& refused : cases)
    {
        const WorkerOptionsResult result = parse(refused.arguments, refused.variables);

        EXPECT_EQ(result.status, WorkerOptionsStatus::Invalid) << refused.expected;
        EXPECT_NE(result.message.find(refused.expected), std::string::npos)
            << "expected '" << refused.expected << "' in '" << result.message << "'";
        EXPECT_EQ(result.message.find('\n'), std::string::npos) << result.message;
    }
}

/** @brief Tests that a name HELLO cannot carry is refused with both lengths. */
TEST(Unit_WorkerOptionsParserTest, NameAbove255BytesIsRefused)
{
    const std::string name(256, 'n');

    const WorkerOptionsResult result = parse({"--master", "a:1", "--name", name.c_str()});

    EXPECT_EQ(result.status, WorkerOptionsStatus::Invalid);
    EXPECT_NE(result.message.find("256 bytes"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("255"), std::string::npos) << result.message;
}

/** @brief Tests that a set variable is returned and an unset one is nothing. */
TEST(Unit_EnvironmentTest, ReadsASetVariableAndReportsAnUnsetOne)
{
    const std::optional<std::string> path = read_environment("PATH");
    ASSERT_TRUE(path.has_value());
    EXPECT_FALSE(path->empty());

    EXPECT_FALSE(read_environment("SR_TEST_VARIABLE_THAT_NOBODY_SETS").has_value());
}

/** @brief Tests that the flag starts clear, request() sets it and a later object clears it. */
TEST(Unit_StopSignalTest, RequestSetsTheFlagAndANewObjectClearsIt)
{
    {
        StopSignal stop_signal;
        EXPECT_FALSE(stop_signal.is_requested());

        stop_signal.request();

        EXPECT_TRUE(stop_signal.is_requested());
    }

    const StopSignal next;
    EXPECT_FALSE(next.is_requested());
}

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
    InProcessFabric fabric;
    FakeMaster master(MasterScript::Register, InProcessTransport::make_master(fabric));
    ASSERT_TRUE(master.started().is_ok()) << master.started().message;
    ServedWorker worker(fabric, K_MASTER_TIMEOUT);

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
    InProcessFabric fabric;
    FakeMaster master(MasterScript::RejectAuthentication,
                      InProcessTransport::make_master(fabric));
    ASSERT_TRUE(master.started().is_ok()) << master.started().message;
    ServedWorker worker(fabric, K_MASTER_TIMEOUT);

    const std::optional<WorkerExitCode> exit_code = worker.exit_code();
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, WorkerExitCode::RejectedAuthentication);
}

/** @brief Tests that REJECT(version) ends the worker with exit code 4. */
TEST(Integration_WorkerMainTest, VersionRejectExitsFour)
{
    InProcessFabric fabric;
    FakeMaster master(MasterScript::RejectVersion, InProcessTransport::make_master(fabric));
    ASSERT_TRUE(master.started().is_ok()) << master.started().message;
    ServedWorker worker(fabric, K_MASTER_TIMEOUT);

    const std::optional<WorkerExitCode> exit_code = worker.exit_code();
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, WorkerExitCode::ProtocolMismatch);
}

/** @brief Tests that a fabric with no master ends the worker with exit code 2. */
TEST(Integration_WorkerMainTest, MasterNotReachedExitsTwo)
{
    InProcessFabric fabric;
    ServedWorker worker(fabric, K_NO_MASTER_TIMEOUT);

    const std::optional<WorkerExitCode> exit_code = worker.exit_code();
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, WorkerExitCode::MasterUnreachable);
}

/** @brief Tests that losing the master with reconnecting off ends the worker with exit code 5. */
TEST(Integration_WorkerMainTest, MasterLostWithoutReconnectExitsFive)
{
    InProcessFabric fabric;
    FakeMaster master(MasterScript::RegisterThenClose, InProcessTransport::make_master(fabric));
    ASSERT_TRUE(master.started().is_ok()) << master.started().message;
    ServedWorker worker(fabric, K_MASTER_TIMEOUT);

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
