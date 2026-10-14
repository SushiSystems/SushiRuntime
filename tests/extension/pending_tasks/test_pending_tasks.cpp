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
 * @file test_pending_tasks.cpp
 * @brief Tests PendingTasks: recording, completion, reaping and the per-peer views.
 * @see tests/extension/pending_tasks/README.md
 * @author Mustafa Garip
 */

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

        /** @brief Returns the ids of the given tasks in rising order. */
        std::vector<std::uint64_t> sorted_ids(const std::vector<PendingTask>& tasks)
        {
            std::vector<std::uint64_t> ids;
            ids.reserve(tasks.size());
            for (const PendingTask& task : tasks)
                ids.push_back(task.task_id);
            std::sort(ids.begin(), ids.end());
            return ids;
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

    TEST(Unit_PendingTasks, TaskIDsRiseFromOne)
    {
        PendingTasks table;

        EXPECT_EQ(table.allocate_task_id(), 1u);
        EXPECT_EQ(table.allocate_task_id(), 2u);
        EXPECT_EQ(table.allocate_task_id(), 3u);
    }

    TEST(Unit_PendingTasks, InsertAfterCloseRefused)
    {
        PendingTasks table;
        const sushi_ptr<Node> first = make_sushi<Node>();
        const sushi_ptr<Node> second = make_sushi<Node>();

        ASSERT_TRUE(insert_plain(table, 1, *first, K_WORKER));
        EXPECT_FALSE(insert_plain(table, 1, *second, K_WORKER)) << "an id in the table is refused";

        table.close();

        EXPECT_FALSE(insert_plain(table, 2, *second, K_WORKER));
        EXPECT_EQ(table.size(), 1u);
        EXPECT_EQ(table.count_for_worker(K_WORKER), 1u);

        std::vector<PendingTask> drained;
        table.drain(drained);
        ASSERT_EQ(drained.size(), 1u);
        EXPECT_EQ(drained[0].node, first.get());
    }

    TEST(Unit_PendingTasks, TakeIsExactlyOnce)
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

        EXPECT_FALSE(table.take(5, K_WORKER).has_value());
        EXPECT_EQ(table.size(), 0u);
        EXPECT_EQ(table.count_for_worker(K_WORKER), 0u);

        std::vector<PendingTask> rest;
        table.take_for_worker(K_WORKER, rest);
        table.take_overdue(at(milliseconds(1000000)), limits_with(milliseconds(1)), rest);
        table.drain(rest);
        EXPECT_TRUE(rest.empty()) << "a taken task leaves through no second removal";
    }

    TEST(Unit_PendingTasks, DrainReturnsEveryNode)
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

        ASSERT_EQ(sorted_ids(drained), (std::vector<std::uint64_t>{1, 2, 3}));
        for (const PendingTask& task : drained)
            EXPECT_EQ(task.node, nodes[task.task_id - 1].get());

        EXPECT_EQ(table.size(), 0u);
        EXPECT_EQ(table.count_for_worker(K_WORKER), 0u);
        EXPECT_EQ(table.count_for_worker(K_OTHER_WORKER), 0u);

        table.drain(drained);
        EXPECT_EQ(drained.size(), 3u) << "a second drain finds nothing";
    }

    TEST(Unit_PendingTasks, LateResultLeavesInflightAlone)
    {
        PendingTasks table;
        const sushi_ptr<Node> lost = make_sushi<Node>();
        const sushi_ptr<Node> running = make_sushi<Node>();

        EXPECT_FALSE(table.take(9, K_WORKER).has_value()) << "an id that was never dispatched";

        ASSERT_TRUE(insert_plain(table, 1, *lost, K_WORKER));
        ASSERT_TRUE(insert_plain(table, 2, *running, K_OTHER_WORKER));

        std::vector<PendingTask> returned;
        table.take_for_worker(K_WORKER, returned);
        ASSERT_EQ(returned.size(), 1u);
        EXPECT_EQ(returned[0].node, lost.get());

        EXPECT_FALSE(table.take(1, K_WORKER).has_value()) << "the result of a task already returned";
        EXPECT_FALSE(table.acknowledge(1, K_WORKER));
        EXPECT_EQ(table.size(), 1u);
        EXPECT_EQ(table.count_for_worker(K_OTHER_WORKER), 1u);
    }

    TEST(Unit_PendingTasks, ForeignWorkerCannotTake)
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

    TEST(Unit_PendingTasks, ExpectsOutputOnlyAtTheDispatchedVersion)
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

    TEST(Unit_PendingTasks, UnacknowledgedTaskReapedWithDeadlineZero)
    {
        PendingTasks table;
        const sushi_ptr<Node> silent = make_sushi<Node>();
        const sushi_ptr<Node> accepted = make_sushi<Node>();
        const OverdueLimits limits = limits_with(milliseconds(0));

        ASSERT_TRUE(insert_plain(table, 1, *silent, K_WORKER));
        ASSERT_TRUE(insert_plain(table, 2, *accepted, K_WORKER));
        ASSERT_TRUE(table.acknowledge(2, K_WORKER));

        std::vector<PendingTask> reaped;
        table.take_overdue(at(K_ACKNOWLEDGE_TIMEOUT), limits, reaped);
        EXPECT_TRUE(reaped.empty()) << "the limit itself is still within the limit";

        table.take_overdue(at(K_ACKNOWLEDGE_TIMEOUT + milliseconds(1)), limits, reaped);
        ASSERT_EQ(reaped.size(), 1u);
        EXPECT_EQ(reaped[0].task_id, 1u);
        EXPECT_EQ(reaped[0].node, silent.get());
        EXPECT_EQ(reaped[0].cause, ReapCause::Unacknowledged);

        table.take_overdue(at(milliseconds(100000000)), limits, reaped);
        EXPECT_EQ(reaped.size(), 1u) << "deadline 0 puts no bound on an accepted task";
        EXPECT_EQ(table.size(), 1u);
    }

    TEST(Unit_PendingTasks, AcknowledgedTaskBelowOldestHeldReaped)
    {
        PendingTasks table;
        const std::vector<sushi_ptr<Node>> nodes = {make_sushi<Node>(), make_sushi<Node>(),
                                                    make_sushi<Node>(), make_sushi<Node>()};
        const OverdueLimits limits = limits_with(milliseconds(0));

        ASSERT_TRUE(insert_plain(table, 1, *nodes[0], K_WORKER));
        ASSERT_TRUE(insert_plain(table, 2, *nodes[1], K_WORKER));
        ASSERT_TRUE(insert_plain(table, 3, *nodes[2], K_WORKER));
        ASSERT_TRUE(insert_plain(table, 4, *nodes[3], K_OTHER_WORKER));
        ASSERT_TRUE(table.acknowledge(1, K_WORKER));
        ASSERT_TRUE(table.acknowledge(2, K_WORKER));
        ASSERT_TRUE(table.acknowledge(3, K_WORKER));
        ASSERT_TRUE(table.acknowledge(4, K_OTHER_WORKER));

        std::vector<PendingTask> reaped;

        // The worker holds 3 and up: tasks 1 and 2 miss once, which is one short of lost.
        table.note_heartbeat(K_WORKER, 3);
        table.take_overdue(at(milliseconds(1)), limits, reaped);
        EXPECT_TRUE(reaped.empty());

        // The worker lists 2 again, so only task 1 has missed twice in a row.
        table.note_heartbeat(K_WORKER, 2);
        table.take_overdue(at(milliseconds(2)), limits, reaped);
        ASSERT_EQ(reaped.size(), 1u);
        EXPECT_EQ(reaped[0].task_id, 1u);
        EXPECT_EQ(reaped[0].node, nodes[0].get());
        EXPECT_EQ(reaped[0].cause, ReapCause::LostResult);

        // Task 2 starts a second run of misses; task 4 belongs to a worker that sent nothing.
        table.note_heartbeat(K_WORKER, 3);
        table.take_overdue(at(milliseconds(3)), limits, reaped);
        EXPECT_EQ(reaped.size(), 1u);

        table.note_heartbeat(K_WORKER, 3);
        table.take_overdue(at(milliseconds(4)), limits, reaped);
        ASSERT_EQ(reaped.size(), 2u);
        EXPECT_EQ(reaped[1].task_id, 2u);
        EXPECT_EQ(reaped[1].cause, ReapCause::LostResult);

        EXPECT_EQ(table.count_for_worker(K_WORKER), 1u);
        EXPECT_EQ(table.count_for_worker(K_OTHER_WORKER), 1u);
    }

    TEST(Unit_PendingTasks, HeartbeatLeavesAnUnacknowledgedTaskToItsOwnLimit)
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

        table.take_overdue(at(K_ACKNOWLEDGE_TIMEOUT + milliseconds(1)), limits, reaped);
        ASSERT_EQ(reaped.size(), 1u);
        EXPECT_EQ(reaped[0].cause, ReapCause::Unacknowledged);
    }

    TEST(Unit_PendingTasks, ChunkRestartsTheDeadline)
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

    TEST(Unit_PendingTasks, TakeForWorkerForgetsItsChunkTime)
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

    TEST(Unit_PendingTasks, TwoRemovalsOfOneTaskHaveOneWinner)
    {
        PendingTasks table;
        const sushi_ptr<Node> node = make_sushi<Node>();

        for (int round = 0; round < K_RACE_ROUNDS; ++round)
        {
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

            const std::size_t winners = (by_result.has_value() ? 1u : 0u) + by_loss.size();
            ASSERT_EQ(winners, 1u) << "round " << round;
            const PendingTask& won = by_result.has_value() ? *by_result : by_loss.front();
            EXPECT_EQ(won.node, node.get());
            EXPECT_EQ(won.task_id, task_id);
            ASSERT_EQ(table.size(), 0u);
        }
    }
} // namespace PendingTasksTests
