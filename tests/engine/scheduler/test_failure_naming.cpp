/**************************************************************************/
/* test_failure_naming.cpp                                                */
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
 * @file test_failure_naming.cpp
 * @brief Tests describe_task() and name_failure(): a task by name, a node by name and index.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task.hpp>

#include <SushiRuntime/scheduler/failure_naming.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Scheduler;

namespace
{
    /** @brief Names itself and does nothing else. */
    class NamedTask final : public Task
    {
        public:
            const char* name() const noexcept override { return "copy"; }
            std::optional<sushi_ptr<Task>> split() override { return std::nullopt; }
            bool is_splittable() const override { return false; }
    };
}

/** @brief Tests that a plain task is described by its name. */
TEST(Small_FailureNaming, ATaskIsDescribedByItsName)
{
    const sushi_ptr<NamedTask> task = make_sushi<NamedTask>();
    EXPECT_EQ(describe_task(*task), "task 'copy'");
}

/** @brief Tests that a node is described by its name and its registration index. */
TEST(Small_FailureNaming, ANodeIsDescribedByNameAndIndex)
{
    Internal::Graph::Node node;
    node.metadata.name = "advect";
    node.registration_index = 7;
    EXPECT_EQ(describe_task(node), "node 'advect' (registration index 7)");
}

/** @brief Tests that a named failure keeps the code and puts the task before the cause. */
TEST(Small_FailureNaming, AFailureKeepsItsCodeAndNamesTheTask)
{
    Internal::Graph::Node node;
    node.metadata.name = "advect";
    const Error named = name_failure(node, Error{ErrorCode::KernelFailed, "out of range"});
    EXPECT_EQ(named.code, ErrorCode::KernelFailed);
    EXPECT_EQ(named.message, "node 'advect' (registration index 0) failed: out of range");
}
