/**************************************************************************/
/* test_emit_task.cpp                                                     */
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
 * @file test_emit_task.cpp
 * @brief Tests task_node(): a task description whose work receives the queue.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_task.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests the fields of the description and that its work runs on the queue. */
TEST(Medium_EmitTask, ATaskNodeRunsOnTheQueue)
{
    auto runtime = Runtime::create().value();
    int* const value = Testing::shared_integers(runtime, 1, 0);
    ASSERT_NE(value, nullptr);
    const Detail::NodeDescription description = Detail::task_node(
        {}, {BufferRegion(value)},
        [value](sycl::queue& queue, const std::vector<sycl::event>& dependencies)
        {
            return queue.submit([&](sycl::handler& handler)
            {
                handler.depends_on(dependencies);
                handler.single_task([value] { *value = 7; });
            });
        }, 1);
    EXPECT_EQ(description.kind, Detail::NodeKind::Task);
    EXPECT_TRUE(std::holds_alternative<Detail::QueueWork>(description.work));
    Testing::run_description(runtime, description);
    EXPECT_EQ(*value, 7);
    sycl::free(value, Testing::queue_of(runtime));
}
