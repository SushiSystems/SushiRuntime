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
 * @file test_emit_task.cpp
 * @brief Tests task_node(): a task description whose work receives the queue.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <variant>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_task.hpp>

#include "description_runner.hpp"
#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Returns the work of a task that stores 7 in @p value on the queue it is handed. */
    Detail::QueueWork store_seven(int* value)
    {
        return [value](sycl::queue& queue, const std::vector<sycl::event>& dependencies)
        {
            return queue.submit([&](sycl::handler& handler)
            {
                handler.depends_on(dependencies);
                handler.single_task([value] { *value = 7; });
            });
        };
    }
} // namespace

/** @brief Tests the kind, the written region and the work type of a task description. */
TEST(Small_EmitTask, ATaskNodeDescribesItsKindWriteAndWork)
{
    int target = 0;

    const Detail::NodeDescription description =
        Detail::task_node({}, {BufferRegion(&target)}, store_seven(&target), 1);

    EXPECT_EQ(description.kind, Detail::NodeKind::Task);
    ASSERT_EQ(description.writes.size(), 1u);
    EXPECT_EQ(description.writes[0].base, &target);
    EXPECT_TRUE(std::holds_alternative<Detail::QueueWork>(description.work));
}

/** @brief Fixture of the test that runs a task description on the program's runtime. */
class Medium_EmitTask : public TestSupport::RuntimeTest
{
};

/** @brief Tests that the work of a task description runs on the queue it is handed. */
TEST_F(Medium_EmitTask, ATaskNodeRunsOnTheQueue)
{
    auto value = runtime().buffer<int>(1).value();
    TestSupport::fill_with(value, 0);
    const Detail::NodeDescription description =
        Detail::task_node({}, {BufferRegion(value.data())}, store_seven(value.data()), 1);

    Testing::run_description(runtime(), description);

    EXPECT_EQ(TestSupport::contents(value), std::vector<int>{7});
}
