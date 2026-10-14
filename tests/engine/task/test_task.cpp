/**************************************************************************/
/* test_task.cpp                                                          */
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
 * @file test_task.cpp
 * @brief Tests Task: the defaults a subclass inherits and the two members it must write.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <optional>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/ready_sink.hpp>
#include <SushiRuntime/task/task.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Scheduler::Task;

namespace
{
    /** @brief Writes the two pure members and nothing else. */
    class PlainTask final : public Task
    {
        public:
            std::optional<sushi_ptr<Task>> split() override
            {
                return std::nullopt;
            }

            bool is_splittable() const override
            {
                return false;
            }
    };
}

/** @brief Tests the defaults: a kernel task, unnamed, priority 0, device work, not a graph node. */
TEST(Unit_Task, ASubclassInheritsTheDefaults)
{
    const sushi_ptr<PlainTask> task = make_sushi<PlainTask>();
    EXPECT_STREQ(task->name(), "unnamed_task");
    EXPECT_EQ(task->execution_mode(), Task::ExecutionMode::Kernel);
    EXPECT_EQ(task->priority(), 0);
    EXPECT_TRUE(task->submits_device_work());
    EXPECT_FALSE(task->is_graph_node());

    Internal::Scheduler::ReadySink ready;
    task->on_complete(ready);
    EXPECT_TRUE(ready.empty()) << "a plain task readied a successor it does not have";
}
