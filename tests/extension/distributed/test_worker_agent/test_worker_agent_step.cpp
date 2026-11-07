/**************************************************************************/
/* test_worker_agent_step.cpp                                             */
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
 * @file test_worker_agent_step.cpp
 * @brief Tests WorkerAgent driven by step() on a host runtime: the heartbeat and the master sessions.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/distributed/worker_agent.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/api/runtime/host_runtime_config.hpp>
#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>
#include <SushiRuntime/transport/message_type.hpp>

#include "support/executor_fabric.hpp"
#include "support/host_runtime.hpp"
#include "support/worker_wire.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using namespace TestSupport::WorkerWire;

namespace
{
    /** @brief Holds the interval between two heartbeats of the agents these cases build. */
    constexpr std::chrono::milliseconds K_HEARTBEAT{5};

    /** @brief Holds a step of the clock that passes one heartbeat interval. */
    constexpr std::uint64_t K_PAST_HEARTBEAT_NS = 10'000'000;

    /** @brief Holds a step of the clock that passes the delay before a worker reconnects. */
    constexpr std::uint64_t K_PAST_RECONNECT_NS = 2'000'000;

    /** @brief Holds the cache budget of the agents these cases build. */
    constexpr std::uint64_t K_CACHE_BYTES = 1024 * 1024;

    /**
     * @brief Runs a worker agent that owns no thread on a host runtime, and plays its master over
     *        an ExecutorFabric: the case steps the agent and the fabric in turn.
     */
    class Small_WorkerAgentStep : public TestSupport::HostRuntimeTest
    {
        protected:
            /** @brief Builds the registry the agent runs. */
            Small_WorkerAgentStep() : kernels_(make_sushi<KernelRegistry>())
            {

            }

            /** @brief Closes the connection so that stopping the agent waits for nobody. */
            void TearDown() override
            {
                fabric_.lose();
            }

            /** @brief Builds the agent on the fabric's worker endpoint and starts it. */
            void start()
            {
                WorkerAgentOptions options;
                options.name = "worker-a";
                options.max_jobs = 16;
                options.cache_bytes = K_CACHE_BYTES;
                options.heartbeat_interval = K_HEARTBEAT;
                options.own_threads = false;
                options.clock = fabric_.shared_clock();
                const std::vector<std::uint8_t> secret = secret_of(K_SECRET_BYTE);
                options.secret =
                    SharedSecret::from_bytes(span<const std::uint8_t>(secret.data(), secret.size()))
                        .value();
                agent_.emplace(runtime(), fabric_.worker(), kernels_, std::move(options));
                fabric_.start_master();
                ASSERT_TRUE(agent_->start().is_ok());
                fabric_.settle();
            }

            /** @brief Answers the newest HELLO with a good CHALLENGE and a WELCOME. */
            void register_master(std::uint64_t session_id)
            {
                const std::vector<RecordedFrame> hellos =
                    fabric_.master_events().frames_of(MessageType::Hello);
                ASSERT_FALSE(hellos.empty());
                const std::uint64_t registrations = agent_->statistics().registrations;

                fabric_.deliver(challenge_of(secret_of(K_SECRET_BYTE), session_id,
                                             nonce_in(decode<Wire::HelloMessage>(hellos.back()))));
                fabric_.deliver(welcome_of(fabric_.master_peer(), 0, 4096));

                ASSERT_EQ(agent_->statistics().registrations, registrations + 1);
            }

            /** @brief Sends a dispatch, runs its batch and lets the replies through. */
            void run(const Wire::TaskDispatchMessage& dispatch)
            {
                fabric_.deliver(dispatch);
                (void)agent_->step();
                fabric_.settle();
            }

            /** @brief Moves the clock past the reconnect delay after the master closed. */
            void lose_the_master()
            {
                fabric_.lose();
                fabric_.advance(K_PAST_RECONNECT_NS);
            }

            /// @brief The master's end and the clock the agent reads.
            TestSupport::ExecutorFabric fabric_;

            /// @brief The kernels the agent runs.
            sushi_ptr<KernelRegistry> kernels_;

            /// @brief The agent under test; built by start().
            std::optional<WorkerAgent> agent_;
    };

    /** @brief Returns whether @p transport was never started, then stops it. */
    bool was_never_started(Transport& transport)
    {
        RecordingTransportEvents probe;
        const bool is_fresh = transport.start(probe).status == TransportStatus::Ok;
        transport.stop();
        return is_fresh;
    }

    /** @brief Returns a host kernel that counts its runs and writes nothing. */
    KernelRegistry::HostKernel counting_host_kernel(int& calls)
    {
        return [&calls](span<const BufferView>, span<const BufferView>, const NodeOptions&)
        {
            ++calls;
        };
    }
} // namespace

/** @brief Tests that an idle worker's HEARTBEAT names the task id it waits for next. */
TEST_F(Small_WorkerAgentStep, IdleHeartbeatCarriesTheNextTaskID)
{
    int calls = 0;
    const OperationID op(17);
    kernels_->register_host_kernel(op, counting_host_kernel(calls));
    start();
    register_master(7);
    run(dispatch_of(41, op, {}, {}));
    const std::size_t before = fabric_.master_events().frame_count(MessageType::Heartbeat);

    fabric_.advance(K_PAST_HEARTBEAT_NS);
    (void)agent_->step();
    fabric_.settle();

    ASSERT_GT(fabric_.master_events().frame_count(MessageType::Heartbeat), before);
    const Wire::HeartbeatMessage heartbeat = decode<Wire::HeartbeatMessage>(
        fabric_.master_events().frames_of(MessageType::Heartbeat).back());
    EXPECT_EQ(heartbeat.oldest_held_task_id, 42u);
    EXPECT_EQ(heartbeat.queue_depth, 0u);
}

/** @brief Tests that a new master session clears the cache and the ids seen, a reconnect not. */
TEST_F(Small_WorkerAgentStep, NewSessionClearsTheCache)
{
    int calls = 0;
    const OperationID op(35);
    kernels_->register_host_kernel(op, counting_host_kernel(calls));
    start();
    register_master(7);
    run(dispatch_of(5, op, {}, {handle_of(1, 1, 32)}));
    ASSERT_EQ(agent_->statistics().cache_bytes, 32u);

    lose_the_master();
    register_master(7);
    EXPECT_EQ(agent_->statistics().cache_bytes, 32u)
        << "the cache was cleared for the session it was filled in";
    fabric_.deliver(dispatch_of(5, op, {}, {handle_of(1, 2, 32)}));
    run(dispatch_of(6, op, {}, {}));
    EXPECT_EQ(calls, 2) << "a dispatch repeated in one session ran twice";

    lose_the_master();
    register_master(8);
    EXPECT_EQ(agent_->statistics().cache_bytes, 0u);
    run(dispatch_of(5, op, {}, {}));
    EXPECT_EQ(calls, 3) << "a new session kept the ids of the old one";
    EXPECT_EQ(agent_->statistics().reconnects, 2u);
}

/** @brief Tests that a worker name HELLO cannot carry is refused before the transport starts. */
TEST(Small_WorkerAgentStart, Regression_ANameAbove255BytesIsRefusedBeforeConnecting)
{
    Runtime runtime = Runtime::create_host().value();
    TestSupport::ExecutorFabric fabric;
    WorkerAgentOptions options;
    options.name = std::string(256, 'n');
    options.own_threads = false;
    WorkerAgent agent(runtime, fabric.worker(), make_sushi<KernelRegistry>(), std::move(options));

    const TransportResult started = agent.start();

    EXPECT_EQ(started.status, TransportStatus::InvalidArgument);
    EXPECT_NE(started.message.find("worker name"), std::string::npos) << started.message;
    EXPECT_NE(started.message.find("255"), std::string::npos) << started.message;
    EXPECT_TRUE(was_never_started(*fabric.worker())) << "the transport was started before the refusal";
}

/** @brief Tests that a label value HELLO cannot carry is refused, naming the label. */
TEST(Small_WorkerAgentStart, Regression_ALabelValueAbove255BytesIsRefusedBeforeConnecting)
{
    HostRuntimeConfig config;
    config.labels = {NodeLabel{"zone", std::string(300, 'v')}};
    Runtime runtime = Runtime::create_host(config).value();
    TestSupport::ExecutorFabric fabric;
    WorkerAgentOptions options;
    options.name = "worker-a";
    options.own_threads = false;
    WorkerAgent agent(runtime, fabric.worker(), make_sushi<KernelRegistry>(), std::move(options));

    const TransportResult started = agent.start();

    EXPECT_EQ(started.status, TransportStatus::InvalidArgument);
    EXPECT_NE(started.message.find("label 'zone'"), std::string::npos) << started.message;
    EXPECT_NE(started.message.find("300 bytes"), std::string::npos) << started.message;
    EXPECT_TRUE(was_never_started(*fabric.worker())) << "the transport was started before the refusal";
}
