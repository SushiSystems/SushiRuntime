/**************************************************************************/
/* test_task_description.cpp                                              */
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
 * @file test_task_description.cpp
 * @brief Tests describe_task() and used_parameter_count(): which parameters travel with a task.
 * @see tests/extension/offload_coordination/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/task/task_types.hpp>

#include <SushiRuntime/offload_coordination/task_description.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Holds the parameters a task carries and how many of them must travel. */
    struct ParameterRow
    {
        const char* meaning;                    /**< Names the row in a test report. */
        std::vector<std::uint64_t> parameters;  /**< The first parameters of the task. */
        std::size_t sent;                       /**< How many of them must be sent. */
    };

    /** @brief Names a row by its meaning in a test report. */
    std::string name_of(const ::testing::TestParamInfo<ParameterRow>& info)
    {
        return info.param.meaning;
    }

    const ParameterRow K_PARAMETER_ROWS[] = {
        {"TrailingZerosAreDropped", {5, 0, 7, 0, 0}, 3},
        {"NoParameterSendsNone", {}, 0},
    };

    /** @brief Returns the metadata of a task named "advect" with the given first parameters. */
    Internal::Graph::TaskMetadata metadata_with(const std::vector<std::uint64_t>& parameters)
    {
        Internal::Graph::TaskMetadata metadata;
        metadata.name = "advect";
        metadata.op_id = OperationID(9);
        for (std::size_t index = 0; index < parameters.size(); ++index)
            metadata.params[index] = parameters[index];
        return metadata;
    }
} // namespace

/** @brief Runs a test once per parameter list. */
class Small_TaskDescription : public ::testing::TestWithParam<ParameterRow>
{
};

/** @brief Tests that trailing zero parameters are not sent and a zero inside the list is. */
TEST_P(Small_TaskDescription, OnlyTheParametersUpToTheLastNonZeroTravel)
{
    const ParameterRow& row = GetParam();
    const Internal::Graph::TaskMetadata metadata = metadata_with(row.parameters);

    const NodeOptions options = describe_task(metadata);

    EXPECT_EQ(used_parameter_count(metadata), row.sent);
    ASSERT_EQ(options.parameters.size(), row.sent);
    EXPECT_TRUE(std::equal(options.parameters.begin(), options.parameters.end(),
                           row.parameters.begin()));
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_TaskDescription, ::testing::ValuesIn(K_PARAMETER_ROWS),
                         name_of);

/** @brief Tests that the description carries the task's name and operation. */
TEST(Small_TaskDescriptionFields, TheNameAndTheOperationTravel)
{
    const NodeOptions options = describe_task(metadata_with({}));

    EXPECT_EQ(options.name, "advect");
    EXPECT_EQ(options.operation, OperationID(9));
}
