/**************************************************************************/
/* test_pending_tasks.cpp                                                 */
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
 * @file test_pending_tasks.cpp
 * @brief Tests PendingTasks: recording, completion, reaping and the per-peer views.
 * @see tests/extension/pending_tasks/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/transport.hpp>

#include <SushiRuntime/pending_tasks/pending_tasks.hpp>

using namespace SushiRuntime;

/** @brief Tests PendingTasks: ownership of offloaded nodes, the removals and the three reapers. */
namespace PendingTasksTests
{
    using namespace SushiRuntime::Distributed;

    namespace
    {
        using Internal::Graph::Node;
        using std::chrono::milliseconds;

        /** @brief Names the worker most tests dispatch to. */
        constexpr PeerID K_WORKER{1};

        /** @brief Names a second worker. */
        constexpr PeerID K_OTHER_WORKER{2};

        /** @brief Holds the acknowledgement limit the reaper tests use, in milliseconds. */
        constexpr milliseconds K_ACKNOWLEDGE_TIMEOUT{5000};

        /** @brief Counts the rounds of the two-thread removal test. */
        constexpr int K_RACE_ROUNDS = 200;

        /** @brief Returns a fixed time point @p offset after the tests' zero. */
        std::chrono::steady_clock::time_point at(milliseconds offset)
        {
            return std::chrono::steady_clock::time_point(std::chrono::hours(1)) + offset;
        }

        /** @brief Returns the limits of a cluster whose task deadline is @p task_deadline. */
        OverdueLimits limits_with(milliseconds task_deadline)
        {
            OverdueLimits limits;
            limits.task_deadline = task_deadline;
            limits.acknowledge_timeout = K_ACKNOWLEDGE_TIMEOUT;
            return limits;
        }

        /** @brief Returns an output handle of 64 bytes. */
        BufferHandle output_of(std::uint64_t id, std::uint32_t version)
        {
            BufferHandle handle;
            handle.id = GlobalBufferID{id};
            handle.version = version;
            handle.size_bytes = 64;
            return handle;
        }

        /** @brief Inserts a task with no outputs and returns whether the table took it. */
        bool insert_plain(PendingTasks& table, std::uint64_t task_id, Node& node, PeerID worker,
                          milliseconds offset = milliseconds(0))
        {
            return table.insert(task_id, node, worker, span<const BufferHandle>(), at(offset));
        }

        /** @brief Returns the given tasks in rising order of their ids. */
        std::vector<PendingTask> sorted_by_id(std::vector<PendingTask> tasks)
        {
            std::sort(tasks.begin(), tasks.end(), [](const PendingTask& left, const PendingTask& right)
                      { return left.task_id < right.task_id; });
            return tasks;
        }

        /** @brief Returns how many of the two removals of one task returned it. */
        std::size_t winners_of(const std::optional<PendingTask>& by_result,
                               const std::vector<PendingTask>& by_loss)
        {
            return (by_result.has_value() ? 1u : 0u) + by_loss.size();
        }

        /** @brief Returns the task the winning removal returned. */
        const PendingTask& winner_among(const std::optional<PendingTask>& by_result,
                                        const std::vector<PendingTask>& by_loss)
        {
            return by_result.has_value() ? *by_result : by_loss.front();
        }

        /** @brief Releases two threads at the same moment, once both have arrived. */
        class StartGate final
        {
            public:
                /** @brief Reports arrival, then spins until both threads have arrived. */
                void arrive_and_wait() noexcept
                {
                    arrived_.fetch_add(1, std::memory_order_acq_rel);
                    while (arrived_.load(std::memory_order_acquire) < 2)
                        std::this_thread::yield();
                }

            private:
                /** @brief Counts the threads that have arrived. */
                std::atomic<int> arrived_{0};
        };
    } // namespace

    TEST(Small_PendingTasks, TaskIDsRiseFromOne)
    {
        PendingTasks table;

        EXPECT_EQ(table.allocate_task_id(), 1u);
        EXPECT_EQ(table.allocate_task_id(), 2u);
        EXPECT_EQ(table.allocate_task_id(), 3u);
    }

    TEST(Small_PendingTasks, ADuplicateIDIsRefused)
    {
        PendingTasks table;
        const sushi_ptr<Node> first = make_sushi<Node>();
        const sushi_ptr<Node> second = make_sushi<Node>();
        ASSERT_TRUE(insert_plain(table, 1, *first, K_WORKER));

        EXPECT_FALSE(insert_plain(table, 1, *second, K_WORKER));

        EXPECT_EQ(table.size(), 1u);
    }

    TEST(Small_PendingTasks, InsertAfterCloseIsRefused)
    {
        PendingTasks table;
        const sushi_ptr<Node> first = make_sushi<Node>();
        const sushi_ptr<Node> second = make_sushi<Node>();
        ASSERT_TRUE(insert_plain(table, 1, *first, K_WORKER));
        table.close();

        EXPECT_FALSE(insert_plain(table, 2, *second, K_WORKER));

        EXPECT_EQ(table.size(), 1u);
        EXPECT_EQ(table.count_for_worker(K_WORKER), 1u);
    }

    TEST(Small_PendingTasks, TakeReturnsTheTaskWithItsOutputs)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();
        const std::vector<BufferHandle> outputs = {output_of(7, 3), output_of(8, 1)};
        ASSERT_TRUE(table.insert(5, *node, K_WORKER,
                                 span<const BufferHandle>(outputs.data(), outputs.size()),
                                 at(milliseconds(0))));
        EXPECT_EQ(table.size(), 1u);

        const std::optional<PendingTask> taken = table.take(5, K_WORKER);

        ASSERT_TRUE(taken.has_value());
        EXPECT_EQ(taken->task_id, 5u);
        EXPECT_EQ(taken->node, node.get());
        EXPECT_EQ(taken->worker, K_WORKER);
        ASSERT_EQ(taken->outputs.size(), 2u);
        EXPECT_EQ(taken->outputs[0].id, GlobalBufferID{7});
        EXPECT_EQ(taken->outputs[0].version, 3u);
        EXPECT_EQ(taken->outputs[1].id, GlobalBufferID{8});
        EXPECT_EQ(taken->outputs[1].size_bytes, 64u);
    }

    TEST(Small_PendingTasks, ATakenTaskLeavesThroughNoSecondRemoval)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();
        ASSERT_TRUE(insert_plain(table, 5, *node, K_WORKER));
        ASSERT_TRUE(table.take(5, K_WORKER).has_value());

        EXPECT_FALSE(table.take(5, K_WORKER).has_value());

        EXPECT_EQ(table.size(), 0u);
        EXPECT_EQ(table.count_for_worker(K_WORKER), 0u);
        std::vector<PendingTask> rest;
        table.take_for_worker(K_WORKER, rest);
        table.take_overdue(at(milliseconds(1000000)), limits_with(milliseconds(1)), rest);
        table.drain(rest);
        EXPECT_TRUE(rest.empty());
    }

    TEST(Small_PendingTasks, DrainReturnsEveryNodeOfEveryWorker)
    {
        PendingTasks table;
        const std::vector<sushi_ptr<Node>> nodes = {make_sushi<Node>(), make_sushi<Node>(),
                                                    make_sushi<Node>()};
        ASSERT_TRUE(insert_plain(table, 1, *nodes[0], K_WORKER));
        ASSERT_TRUE(insert_plain(table, 2, *nodes[1], K_OTHER_WORKER));
        ASSERT_TRUE(insert_plain(table, 3, *nodes[2], K_WORKER));
        ASSERT_TRUE(table.acknowledge(3, K_WORKER));
        table.close();
        std::vector<PendingTask> drained;

        table.drain(drained);

        const std::vector<PendingTask> by_id = sorted_by_id(drained);
        ASSERT_EQ(by_id.size(), 3u);
        EXPECT_EQ(by_id[0].task_id, 1u);
        EXPECT_EQ(by_id[0].node, nodes[0].get());
        EXPECT_EQ(by_id[1].task_id, 2u);
        EXPECT_EQ(by_id[1].node, nodes[1].get());
        EXPECT_EQ(by_id[2].task_id, 3u);
        EXPECT_EQ(by_id[2].node, nodes[2].get());
        EXPECT_EQ(table.size(), 0u);
        EXPECT_EQ(table.count_for_worker(K_WORKER), 0u);
        EXPECT_EQ(table.count_for_worker(K_OTHER_WORKER), 0u);
    }

    TEST(Small_PendingTasks, ASecondDrainFindsNothing)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();
        ASSERT_TRUE(insert_plain(table, 1, *node, K_WORKER));
        std::vector<PendingTask> first;
        table.drain(first);
        std::vector<PendingTask> second;

        table.drain(second);

        EXPECT_TRUE(second.empty());
    }

    TEST(Small_PendingTasks, AnIDThatWasNeverDispatchedTakesNothing)
    {
        PendingTasks table;

        EXPECT_FALSE(table.take(9, K_WORKER).has_value());
    }

    TEST(Small_PendingTasks, AResultForATaskAlreadyReturnedIsRefused)
    {
        PendingTasks table;
        const sushi_ptr<Node> lost = make_sushi<Node>();
        ASSERT_TRUE(insert_plain(table, 1, *lost, K_WORKER));
        std::vector<PendingTask> returned;
        table.take_for_worker(K_WORKER, returned);
        ASSERT_EQ(returned.size(), 1u);
        EXPECT_EQ(returned[0].node, lost.get());

        EXPECT_FALSE(table.take(1, K_WORKER).has_value());
        EXPECT_FALSE(table.acknowledge(1, K_WORKER));
    }

    TEST(Small_PendingTasks, ALostTaskLeavesAnotherWorkersTaskInflight)
    {
        PendingTasks table;
        const sushi_ptr<Node> lost = make_sushi<Node>();
        const sushi_ptr<Node> running = make_sushi<Node>();
        ASSERT_TRUE(insert_plain(table, 1, *lost, K_WORKER));
        ASSERT_TRUE(insert_plain(table, 2, *running, K_OTHER_WORKER));
        std::vector<PendingTask> returned;

        table.take_for_worker(K_WORKER, returned);

        EXPECT_EQ(table.size(), 1u);
        EXPECT_EQ(table.count_for_worker(K_OTHER_WORKER), 1u);
    }

    TEST(Small_PendingTasks, ForeignWorkerCannotTake)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();
        const std::vector<BufferHandle> outputs = {output_of(7, 3)};
        ASSERT_TRUE(table.insert(1, *node, K_WORKER,
                                 span<const BufferHandle>(outputs.data(), outputs.size()),
                                 at(milliseconds(0))));

        EXPECT_FALSE(table.take(1, K_OTHER_WORKER).has_value());
        EXPECT_FALSE(table.acknowledge(1, K_OTHER_WORKER));
        EXPECT_FALSE(table.expects_output(1, K_OTHER_WORKER, GlobalBufferID{7}, 3));
        EXPECT_EQ(table.size(), 1u);
        std::vector<PendingTask> returned;
        table.take_for_worker(K_OTHER_WORKER, returned);
        EXPECT_TRUE(returned.empty());
        const std::optional<PendingTask> taken = table.take(1, K_WORKER);
        ASSERT_TRUE(taken.has_value());
        EXPECT_EQ(taken->node, node.get());
    }

    TEST(Small_PendingTasks, ExpectsOutputOnlyAtTheDispatchedVersion)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();
        const std::vector<BufferHandle> outputs = {output_of(7, 3)};
        ASSERT_TRUE(table.insert(1, *node, K_WORKER,
                                 span<const BufferHandle>(outputs.data(), outputs.size()),
                                 at(milliseconds(0))));

        EXPECT_TRUE(table.expects_output(1, K_WORKER, GlobalBufferID{7}, 3));
        EXPECT_FALSE(table.expects_output(1, K_WORKER, GlobalBufferID{7}, 2));
        EXPECT_FALSE(table.expects_output(1, K_WORKER, GlobalBufferID{7}, 4));
        EXPECT_FALSE(table.expects_output(1, K_WORKER, GlobalBufferID{8}, 3));
        EXPECT_FALSE(table.expects_output(2, K_WORKER, GlobalBufferID{7}, 3));
        ASSERT_TRUE(table.take(1, K_WORKER).has_value());
        EXPECT_FALSE(table.expects_output(1, K_WORKER, GlobalBufferID{7}, 3));
    }

    TEST(Small_PendingTasks, AnUnacknowledgedTaskIsReapedOneMillisecondAfterTheLimit)
    {
        PendingTasks table;
        const sushi_ptr<Node> silent = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(0));
        ASSERT_TRUE(insert_plain(table, 1, *silent, K_WORKER));
        std::vector<PendingTask> reaped;
        table.take_overdue(at(K_ACKNOWLEDGE_TIMEOUT), limits, reaped);
        EXPECT_TRUE(reaped.empty()) << "the limit itself is still within the limit";

        table.take_overdue(at(K_ACKNOWLEDGE_TIMEOUT + milliseconds(1)), limits, reaped);

        ASSERT_EQ(reaped.size(), 1u);
        EXPECT_EQ(reaped[0].task_id, 1u);
        EXPECT_EQ(reaped[0].node, silent.get());
        EXPECT_EQ(reaped[0].cause, ReapCause::Unacknowledged);
    }

    TEST(Small_PendingTasks, ADeadlineOfZeroBoundsNoAcceptedTask)
    {
        PendingTasks table;
        const sushi_ptr<Node> accepted = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(0));
        ASSERT_TRUE(insert_plain(table, 2, *accepted, K_WORKER));
        ASSERT_TRUE(table.acknowledge(2, K_WORKER));
        std::vector<PendingTask> reaped;

        table.take_overdue(at(milliseconds(100000000)), limits, reaped);

        EXPECT_TRUE(reaped.empty());
        EXPECT_EQ(table.size(), 1u);
    }

    TEST(Small_PendingTasks, TwoMissesInARowReapATaskAsLostResult)
    {
        PendingTasks table;
        const sushi_ptr<Node> lost = make_sushi<Node>();
        const sushi_ptr<Node> held = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(0));
        ASSERT_TRUE(insert_plain(table, 1, *lost, K_WORKER));
        ASSERT_TRUE(insert_plain(table, 3, *held, K_WORKER));
        ASSERT_TRUE(table.acknowledge(1, K_WORKER));
        ASSERT_TRUE(table.acknowledge(3, K_WORKER));
        std::vector<PendingTask> reaped;
        // The worker holds 3 and up: task 1 misses once, which is one short of lost.
        table.note_heartbeat(K_WORKER, 3);
        table.take_overdue(at(milliseconds(1)), limits, reaped);
        EXPECT_TRUE(reaped.empty());

        table.note_heartbeat(K_WORKER, 3);
        table.take_overdue(at(milliseconds(2)), limits, reaped);

        ASSERT_EQ(reaped.size(), 1u);
        EXPECT_EQ(reaped[0].task_id, 1u);
        EXPECT_EQ(reaped[0].node, lost.get());
        EXPECT_EQ(reaped[0].cause, ReapCause::LostResult);
        EXPECT_EQ(table.count_for_worker(K_WORKER), 1u);
    }

    TEST(Small_PendingTasks, AHeartbeatThatListsTheTaskAgainRestartsItsMisses)
    {
        PendingTasks table;
        const sushi_ptr<Node> first = make_sushi<Node>();
        const sushi_ptr<Node> second = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(0));
        ASSERT_TRUE(insert_plain(table, 1, *first, K_WORKER));
        ASSERT_TRUE(insert_plain(table, 2, *second, K_WORKER));
        ASSERT_TRUE(table.acknowledge(1, K_WORKER));
        ASSERT_TRUE(table.acknowledge(2, K_WORKER));
        std::vector<PendingTask> reaped;
        // Both tasks miss once, then the worker lists task 2 again, so only task 1 has missed twice.
        table.note_heartbeat(K_WORKER, 3);
        table.take_overdue(at(milliseconds(1)), limits, reaped);
        table.note_heartbeat(K_WORKER, 2);
        table.take_overdue(at(milliseconds(2)), limits, reaped);
        ASSERT_EQ(reaped.size(), 1u);
        EXPECT_EQ(reaped[0].task_id, 1u);

        // Task 2 starts a second run of misses and needs two more to be lost.
        table.note_heartbeat(K_WORKER, 3);
        table.take_overdue(at(milliseconds(3)), limits, reaped);
        EXPECT_EQ(reaped.size(), 1u);
        table.note_heartbeat(K_WORKER, 3);
        table.take_overdue(at(milliseconds(4)), limits, reaped);

        ASSERT_EQ(reaped.size(), 2u);
        EXPECT_EQ(reaped[1].task_id, 2u);
        EXPECT_EQ(reaped[1].cause, ReapCause::LostResult);
    }

    TEST(Small_PendingTasks, AWorkerThatSentNoHeartbeatLosesNothing)
    {
        PendingTasks table;
        const sushi_ptr<Node> mine = make_sushi<Node>();
        const sushi_ptr<Node> silent = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(0));
        ASSERT_TRUE(insert_plain(table, 1, *mine, K_WORKER));
        ASSERT_TRUE(insert_plain(table, 4, *silent, K_OTHER_WORKER));
        ASSERT_TRUE(table.acknowledge(1, K_WORKER));
        ASSERT_TRUE(table.acknowledge(4, K_OTHER_WORKER));
        std::vector<PendingTask> reaped;

        table.note_heartbeat(K_WORKER, 3);
        table.note_heartbeat(K_WORKER, 3);
        table.take_overdue(at(milliseconds(1)), limits, reaped);

        EXPECT_EQ(table.count_for_worker(K_OTHER_WORKER), 1u);
    }

    TEST(Small_PendingTasks, AHeartbeatLeavesAnUnacknowledgedTaskToItsOwnLimit)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(0));
        ASSERT_TRUE(insert_plain(table, 1, *node, K_WORKER));
        table.note_heartbeat(K_WORKER, 5);
        table.note_heartbeat(K_WORKER, 5);
        table.note_heartbeat(K_WORKER, 5);
        std::vector<PendingTask> reaped;

        table.take_overdue(at(K_ACKNOWLEDGE_TIMEOUT), limits, reaped);

        EXPECT_TRUE(reaped.empty()) << "the dispatch may still be on its way to the worker";
    }

    TEST(Small_PendingTasks, ChunkRestartsTheDeadline)
    {
        PendingTasks table;
        const sushi_ptr<Node> busy = make_sushi<Node>();
        const sushi_ptr<Node> idle = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(1000));
        ASSERT_TRUE(insert_plain(table, 1, *busy, K_WORKER));
        ASSERT_TRUE(insert_plain(table, 2, *idle, K_OTHER_WORKER));
        ASSERT_TRUE(table.acknowledge(1, K_WORKER));
        ASSERT_TRUE(table.acknowledge(2, K_OTHER_WORKER));
        table.note_chunk(K_WORKER, at(milliseconds(900)));
        std::vector<PendingTask> reaped;
        table.take_overdue(at(milliseconds(1000)), limits, reaped);
        EXPECT_TRUE(reaped.empty());
        table.take_overdue(at(milliseconds(1001)), limits, reaped);
        ASSERT_EQ(reaped.size(), 1u);
        EXPECT_EQ(reaped[0].task_id, 2u);
        EXPECT_EQ(reaped[0].cause, ReapCause::Deadline);
        table.take_overdue(at(milliseconds(1900)), limits, reaped);
        EXPECT_EQ(reaped.size(), 1u);

        table.take_overdue(at(milliseconds(1901)), limits, reaped);

        ASSERT_EQ(reaped.size(), 2u);
        EXPECT_EQ(reaped[1].task_id, 1u);
        EXPECT_EQ(reaped[1].node, busy.get());
        EXPECT_EQ(reaped[1].cause, ReapCause::Deadline);
    }

    TEST(Small_PendingTasks, EarliestDeadlinePublishesOnlyOwnDeadlines)
    {
        PendingTasks table;
        const sushi_ptr<Node> ruled = make_sushi<Node>();
        const sushi_ptr<Node> plain = make_sushi<Node>();
        const auto none = std::chrono::steady_clock::time_point::max();
        const auto tick = std::chrono::steady_clock::duration(1);
        EXPECT_EQ(table.earliest_deadline(), none);
        ASSERT_TRUE(insert_plain(table, 2, *plain, K_WORKER));
        EXPECT_EQ(table.earliest_deadline(), none) << "the cluster's deadline is not published";

        ASSERT_TRUE(table.insert(1, *ruled, K_WORKER, span<const BufferHandle>(),
                                 at(milliseconds(0)), milliseconds(50)));

        EXPECT_EQ(table.earliest_deadline(), at(milliseconds(50)) + tick);
    }

    TEST(Small_PendingTasks, EarliestDeadlineIsAtOrBeforeTheMovedDeadlineAfterAChunk)
    {
        PendingTasks table;
        const sushi_ptr<Node> ruled = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(1000));
        const auto tick = std::chrono::steady_clock::duration(1);
        ASSERT_TRUE(table.insert(1, *ruled, K_WORKER, span<const BufferHandle>(),
                                 at(milliseconds(0)), milliseconds(50)));
        // A chunk moves the deadline on; the bound published may stay earlier until the next pass.
        table.note_chunk(K_WORKER, at(milliseconds(40)));
        std::vector<PendingTask> reaped;

        table.take_overdue(at(milliseconds(45)), limits, reaped);

        EXPECT_TRUE(reaped.empty());
        EXPECT_NE(table.earliest_deadline(), std::chrono::steady_clock::time_point::max());
        EXPECT_LE(table.earliest_deadline(), at(milliseconds(90)) + tick);
    }

    TEST(Small_PendingTasks, ATakeLeavesNoEarliestDeadline)
    {
        PendingTasks table;
        const sushi_ptr<Node> ruled = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(1000));
        ASSERT_TRUE(table.insert(1, *ruled, K_WORKER, span<const BufferHandle>(),
                                 at(milliseconds(0)), milliseconds(50)));
        ASSERT_TRUE(table.take(1, K_WORKER).has_value());
        std::vector<PendingTask> reaped;

        table.take_overdue(at(milliseconds(46)), limits, reaped);

        EXPECT_EQ(table.earliest_deadline(), std::chrono::steady_clock::time_point::max());
    }

    TEST(Small_PendingTasks, OwnDeadlineReplacesTheClusterDeadline)
    {
        PendingTasks table;
        const sushi_ptr<Node> ruled = make_sushi<Node>();
        const sushi_ptr<Node> plain = make_sushi<Node>();
        const sushi_ptr<Node> patient = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(1000));
        ASSERT_TRUE(table.insert(1, *ruled, K_WORKER, span<const BufferHandle>(),
                                 at(milliseconds(0)), milliseconds(100)));
        ASSERT_TRUE(insert_plain(table, 2, *plain, K_WORKER));
        ASSERT_TRUE(table.insert(3, *patient, K_WORKER, span<const BufferHandle>(),
                                 at(milliseconds(0)), milliseconds(5000)));
        ASSERT_TRUE(table.acknowledge(1, K_WORKER));
        ASSERT_TRUE(table.acknowledge(2, K_WORKER));
        ASSERT_TRUE(table.acknowledge(3, K_WORKER));
        std::vector<PendingTask> reaped;
        table.take_overdue(at(milliseconds(101)), limits, reaped);
        ASSERT_EQ(reaped.size(), 1u) << "the task's own 100 ms deadline applies";
        EXPECT_EQ(reaped[0].task_id, 1u);
        EXPECT_EQ(reaped[0].cause, ReapCause::Deadline);
        table.take_overdue(at(milliseconds(1001)), limits, reaped);
        ASSERT_EQ(reaped.size(), 2u) << "a task with no deadline keeps the cluster's";
        EXPECT_EQ(reaped[1].task_id, 2u);

        table.take_overdue(at(milliseconds(5001)), limits, reaped);

        ASSERT_EQ(reaped.size(), 3u) << "a longer own deadline outlasts the cluster's";
        EXPECT_EQ(reaped[2].task_id, 3u);
    }

    TEST(Small_PendingTasks, TakeForWorkerForgetsItsChunkTime)
    {
        PendingTasks table;
        const sushi_ptr<Node> first = make_sushi<Node>();
        const sushi_ptr<Node> second = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(1000));
        ASSERT_TRUE(insert_plain(table, 1, *first, K_WORKER));
        table.note_chunk(K_WORKER, at(milliseconds(50000)));
        std::vector<PendingTask> returned;
        table.take_for_worker(K_WORKER, returned);
        ASSERT_EQ(returned.size(), 1u);
        ASSERT_TRUE(insert_plain(table, 2, *second, K_WORKER, milliseconds(100)));
        ASSERT_TRUE(table.acknowledge(2, K_WORKER));
        std::vector<PendingTask> reaped;

        table.take_overdue(at(milliseconds(1101)), limits, reaped);

        ASSERT_EQ(reaped.size(), 1u) << "the deadline runs from the dispatch, not from the old chunk";
        EXPECT_EQ(reaped[0].task_id, 2u);
        EXPECT_EQ(reaped[0].cause, ReapCause::Deadline);
    }

    /** @brief Runs a test once per round of the two-thread removal. */
    class Medium_PendingTasksRace : public ::testing::TestWithParam<int>
    {
    };

    TEST_P(Medium_PendingTasksRace, TwoRemovalsOfOneTaskHaveOneWinner)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();
        const std::uint64_t task_id = table.allocate_task_id();
        ASSERT_TRUE(insert_plain(table, task_id, *node, K_WORKER));
        StartGate gate;
        std::optional<PendingTask> by_result;
        std::vector<PendingTask> by_loss;
        std::thread result_thread([&]
        {
            gate.arrive_and_wait();
            by_result = table.take(task_id, K_WORKER);
        });
        std::thread loss_thread([&]
        {
            gate.arrive_and_wait();
            table.take_for_worker(K_WORKER, by_loss);
        });

        result_thread.join();
        loss_thread.join();

        ASSERT_EQ(winners_of(by_result, by_loss), 1u);
        EXPECT_EQ(winner_among(by_result, by_loss).node, node.get());
        EXPECT_EQ(winner_among(by_result, by_loss).task_id, task_id);
        EXPECT_EQ(table.size(), 0u);
    }

    INSTANTIATE_TEST_SUITE_P(Rounds, Medium_PendingTasksRace, ::testing::Range(0, K_RACE_ROUNDS));
} // namespace PendingTasksTests
