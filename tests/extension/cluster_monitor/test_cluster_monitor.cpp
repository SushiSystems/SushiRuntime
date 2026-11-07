/**************************************************************************/
/* test_cluster_monitor.cpp                                               */
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
 * @file test_cluster_monitor.cpp
 * @brief Tests ClusterMonitor: one pass over a ManualClock, and the thread that repeats it.
 * @see tests/extension/cluster_monitor/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <SushiRuntime/cluster_monitor/cluster_monitor.hpp>
#include <SushiRuntime/cluster_load/offload_policy.hpp>
#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/in_process_transport/in_process_transport.hpp>
#include <SushiRuntime/transport/recording_transport_events.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/refusing_offload_sink.hpp>

#include "support/executor_fabric.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using SushiRuntime::Internal::Graph::RefusingOffloadSink;
using std::chrono::milliseconds;

namespace
{
    /** @brief Holds the time every test starts its clock at, in nanoseconds. */
    constexpr std::uint64_t K_START_NS = 3'600'000'000'000ull;

    /** @brief Returns the steady-clock point the manual clock reads at its start. */
    std::chrono::steady_clock::time_point start_point()
    {
        return std::chrono::steady_clock::time_point(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::nanoseconds(K_START_NS)));
    }

    /** @brief Builds a monitor over real bricks, an in-process transport that a worker end records and a clock the test moves. */
    class Small_ClusterMonitor : public ::testing::Test
    {
        protected:
            /** @brief Builds the bricks with the default timings: liveness 5 s, handshake 5 s. */
            Small_ClusterMonitor()
                : directory_(SushiRuntime::span<const std::uint8_t>(), std::make_shared<LeastLoadedOffloadPolicy>()),
                  exchange_(registry_, 1024),
                  clock_(K_START_NS),
                  monitor_(directory_, pending_, held_, registry_, exchange_, *fabric_.master(),
                           ClusterMonitorOptions{}, clock_)
            {
                fabric_.start_master();
                fabric_.start_worker_recording();
            }

            /** @brief Returns the name the master gives its connection to the worker end. */
            [[nodiscard]] PeerID worker_peer() const
            {
                return fabric_.master_peer();
            }

            /** @brief Returns the frames the monitor sent that reached the worker end. */
            [[nodiscard]] std::size_t frames_sent()
            {
                fabric_.settle();
                return fabric_.worker_events().frames().size();
            }

            /** @brief Returns how many connections the worker end saw closed by the monitor. */
            [[nodiscard]] std::size_t peers_closed()
            {
                fabric_.settle();
                return fabric_.worker_events().losses().size();
            }

            /** @brief Moves the clock by @p step and runs one pass. */
            void pass_after(milliseconds step)
            {
                clock_.advance(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(step).count()));
                monitor_.run_pass(sink_);
            }

            WorkerDirectory directory_;           /**< Holds the sessions and handshake limits. */
            PendingTasks pending_;                /**< Holds the tasks out on a worker. */
            HeldNodes held_;                      /**< Holds the nodes a rule keeps back. */
            BufferRegistry registry_;             /**< Holds the shared buffers' versions. */
            BufferExchange exchange_;             /**< Forgets the tasks taken back. */
            TestSupport::ExecutorFabric fabric_;  /**< Joins the monitor to a worker end that records. */
            RefusingOffloadSink sink_;            /**< Counts the nodes handed back. */
            ManualClock clock_;                   /**< Moves only when the test says. */
            ClusterMonitor monitor_;              /**< The unit under test. */
    };
} // namespace

/** @brief Tests that a pass over empty bricks sends and closes nothing. */
TEST_F(Small_ClusterMonitor, AnEmptyPassSendsAndClosesNothing)
{
    pass_after(milliseconds(0));

    EXPECT_EQ(frames_sent(), 0u);
    EXPECT_EQ(peers_closed(), 0u);
    EXPECT_EQ(monitor_.statistics().redispatched, 0u);
}

/** @brief Tests that a peer is closed once the clock passes its handshake limit, and not before. */
TEST_F(Small_ClusterMonitor, APeerPastTheHandshakeLimitIsClosed)
{
    directory_.add_peer(worker_peer(), "peer", start_point());

    pass_after(milliseconds(4999));
    EXPECT_EQ(peers_closed(), 0u) << "4999 ms is inside the 5000 ms limit";

    pass_after(milliseconds(2));
    EXPECT_EQ(peers_closed(), 1u) << "the one connection of the worker end was not closed";
}

/** @brief Tests that a task nobody acknowledged is taken back, counted and handed to the sink. */
TEST_F(Small_ClusterMonitor, AnUnacknowledgedTaskIsTakenBackAndCounted)
{
    const sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
    node->add_ref();
    ASSERT_TRUE(pending_.insert(7, *node, PeerID{2}, {}, start_point()));

    pass_after(milliseconds(4999));
    EXPECT_EQ(pending_.size(), 1u) << "inside the acknowledgement limit the task stays";

    pass_after(milliseconds(2));
    EXPECT_EQ(pending_.size(), 0u);
    EXPECT_EQ(sink_.refusals(), 1) << "the node went back to the sink once";
    const ClusterMonitorStatistics statistics = monitor_.statistics();
    EXPECT_EQ(statistics.tasks_reaped_unacknowledged, 1u);
    EXPECT_EQ(statistics.tasks_reaped_deadline, 0u);
    EXPECT_EQ(statistics.redispatched, 1u);
}

/** @brief Tests that a new liveness timeout is the limit the next pass applies. */
TEST_F(Small_ClusterMonitor, TheLivenessSetterChangesTheNextPass)
{
    const sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
    node->add_ref();
    ASSERT_TRUE(pending_.insert(7, *node, PeerID{2}, {}, start_point()));
    monitor_.set_liveness_timeout(milliseconds(50));

    pass_after(milliseconds(51));

    EXPECT_EQ(monitor_.statistics().tasks_reaped_unacknowledged, 1u);
    EXPECT_EQ(monitor_.liveness_timeout(), milliseconds(50));
}

/** @brief Tests that the period follows the tightest timeout. */
TEST_F(Small_ClusterMonitor, ThePeriodFollowsTheTightestTimeout)
{
    EXPECT_EQ(monitor_.period(), milliseconds(250));

    monitor_.set_liveness_timeout(milliseconds(100));
    EXPECT_EQ(monitor_.period(), milliseconds(25));

    monitor_.set_task_deadline(milliseconds(20));
    EXPECT_EQ(monitor_.period(), milliseconds(5));
}

/** @brief Tests that the started thread repeats the pass on the machine clock until it is stopped. */
TEST(Medium_ClusterMonitorThread, TheThreadClosesAPeerPastTheHandshakeLimitAndStops)
{
    WorkerDirectory directory(SushiRuntime::span<const std::uint8_t>(), std::make_shared<LeastLoadedOffloadPolicy>());
    PendingTasks pending;
    HeldNodes held;
    BufferRegistry registry;
    BufferExchange exchange(registry, 1024);
    InProcessFabric fabric;
    RecordingTransportEvents master_events;
    RecordingTransportEvents worker_events;
    const std::shared_ptr<InProcessTransport> master = InProcessTransport::make_master(fabric);
    const std::shared_ptr<InProcessTransport> worker = InProcessTransport::make_worker(fabric);
    ASSERT_TRUE(master->start(master_events).is_ok());
    ASSERT_TRUE(worker->start(worker_events).is_ok());
    ASSERT_TRUE(master_events.wait_for_connections(1, std::chrono::seconds(30)));
    RefusingOffloadSink sink;
    SteadyClock clock;
    ClusterMonitorOptions options;
    options.period = milliseconds(1);
    options.handshake_timeout = milliseconds(5);
    ClusterMonitor monitor(directory, pending, held, registry, exchange, *master, options, clock);
    directory.add_peer(master_events.connected()[0], "peer", std::chrono::steady_clock::now());

    monitor.start(sink);
    const bool is_closed = worker_events.wait_for_losses(1, std::chrono::seconds(30));
    monitor.stop();
    worker->stop();
    master->stop();

    EXPECT_TRUE(is_closed) << "the thread never ran a pass that closed the peer";
}
