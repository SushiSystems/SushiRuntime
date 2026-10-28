/**************************************************************************/
/* test_task_submission.cpp                                               */
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
 * @file test_task_submission.cpp
 * @brief Tests submit_task() and submit_task_discarding() on host and kernel tasks.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <optional>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/dispatch_events.hpp>
#include <SushiRuntime/task/small_event_vector.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/task/task_submission.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Graph::SmallEventVector;
using SushiRuntime::Internal::Scheduler::Task;

namespace
{
    /** @brief Records what a submission handed it and hands one event over. */
    class RecordingTask final : public Task
    {
        public:
            /** @brief Builds a task of @p mode. */
            explicit RecordingTask(ExecutionMode mode) : mode_(mode) {}

            ExecutionMode execution_mode() const override { return mode_; }

            void execute(sycl::handler& h) override
            {
                ++runs;
                h.single_task([]() {});
            }

            sycl::event execute_host(sycl::queue& queue, const std::vector<sycl::event>& dependencies) override
            {
                ++runs;
                seen_dependencies = dependencies.size();
                const sycl::event event = queue.submit([&dependencies](sycl::handler& h)
                {
                    h.depends_on(dependencies);
                    h.single_task([]() {});
                });
                Internal::DispatchEvents::DispatchEventScope::note(event);
                return event;
            }

            std::optional<sushi_ptr<Task>> split() override { return std::nullopt; }

            bool is_splittable() const override { return false; }

            int runs = 0;                       /**< Times either body ran. */
            std::size_t seen_dependencies = 0;  /**< Dependencies the host body received. */

        private:
            ExecutionMode mode_; /**< The mode the task reports. */
    };

    /** @brief Builds a vector of @p count completed events from @p queue. */
    SmallEventVector completed_events(sycl::queue& queue, std::size_t count)
    {
        std::vector<sycl::event> events;
        for (std::size_t i = 0; i < count; ++i)
            events.push_back(queue.submit([](sycl::handler& h) { h.single_task([]() {}); }));
        SmallEventVector small;
        small = events;
        return small;
    }
}

/** @brief Tests that a host task receives every dependency and its handed-over event lands in the sink. */
TEST(Medium_TaskSubmission, AHostTaskGetsItsDependenciesAndHandsItsEventOver)
{
    sycl::queue queue;
    const SmallEventVector dependencies = completed_events(queue, 3);
    const sushi_ptr<RecordingTask> task = make_sushi<RecordingTask>(Task::ExecutionMode::Host);
    std::vector<sycl::event> handed_over;

    sycl::event event = Internal::Graph::submit_task(queue, *task, &dependencies, &handed_over);
    event.wait();
    EXPECT_EQ(task->runs, 1);
    EXPECT_EQ(task->seen_dependencies, 3u);
    EXPECT_EQ(handed_over.size(), 1u) << "the scope did not collect the handed-over event";
}

/** @brief Tests that a null sink opens no scope, so a handed-over event reaches no one. */
TEST(Medium_TaskSubmission, ANullSinkCollectsNothing)
{
    sycl::queue queue;
    const sushi_ptr<RecordingTask> task = make_sushi<RecordingTask>(Task::ExecutionMode::Host);
    std::vector<sycl::event> outer;
    Internal::DispatchEvents::DispatchEventScope enclosing(outer);

    Internal::Graph::submit_task(queue, *task, nullptr, nullptr).wait();
    EXPECT_EQ(task->seen_dependencies, 0u);
    EXPECT_EQ(outer.size(), 1u) << "a null sink must leave the enclosing scope in force";
}

/** @brief Tests that a kernel task runs its command group behind its dependencies and returns an event. */
TEST(Medium_TaskSubmission, AKernelTaskReturnsAnEvent)
{
    sycl::queue queue;
    const SmallEventVector dependencies = completed_events(queue, 2);
    const sushi_ptr<RecordingTask> task = make_sushi<RecordingTask>(Task::ExecutionMode::Kernel);
    std::vector<sycl::event> handed_over;

    sycl::event event = Internal::Graph::submit_task(queue, *task, &dependencies, &handed_over);
    event.wait();
    EXPECT_EQ(task->runs, 1);
    EXPECT_TRUE(handed_over.empty()) << "a kernel task handed an event over";
}

/** @brief Tests that the discarding path runs both kinds of task once. */
TEST(Medium_TaskSubmission, TheDiscardingPathRunsEitherKind)
{
    sycl::queue queue;
    const SmallEventVector dependencies = completed_events(queue, 1);
    const sushi_ptr<RecordingTask> host = make_sushi<RecordingTask>(Task::ExecutionMode::Host);
    const sushi_ptr<RecordingTask> kernel = make_sushi<RecordingTask>(Task::ExecutionMode::Kernel);

    Internal::Graph::submit_task_discarding(queue, *host, &dependencies);
    Internal::Graph::submit_task_discarding(queue, *kernel, nullptr);
    queue.wait();
    EXPECT_EQ(host->runs, 1);
    EXPECT_EQ(host->seen_dependencies, 1u);
    EXPECT_EQ(kernel->runs, 1);
}
