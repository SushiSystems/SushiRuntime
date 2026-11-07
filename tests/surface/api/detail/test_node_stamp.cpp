/**************************************************************************/
/* test_node_stamp.cpp                                                    */
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
 * @file test_node_stamp.cpp
 * @brief Tests that a node accepts parameters up to the limit and refuses one past it.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/host_runtime.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Elements the node of every row launches over. */
    constexpr std::size_t K_WIDTH = 8;

    /** @brief Names a row: how many parameters the node states and what the graph does with it. */
    struct ParameterRow
    {
        const char* name;            /**< The row's name in a report. */
        std::size_t count;           /**< Parameters stated in the options. */
        bool refused;                /**< Whether the graph refuses the add. */
        std::size_t recorded_nodes;  /**< Nodes the graph holds afterwards. */
    };

    /** @brief Returns the name a row carries in a report. */
    std::string row_name(const ::testing::TestParamInfo<ParameterRow>& info)
    {
        return info.param.name;
    }
} // namespace

/** @brief Fixture of the rows that add a node stating parameters to a graph of a host runtime. */
class Small_NodeParameterLimit : public TestSupport::HostRuntimeTest,
                                  public ::testing::WithParamInterface<ParameterRow>
{
};

/** @brief Tests that parameters up to the limit are accepted and one past it is refused. */
TEST_P(Small_NodeParameterLimit, ParametersUpToTheLimitAreAcceptedAndOnePastIsRefused)
{
    const Runtime& rt = runtime();
    auto values = rt.buffer<std::uint32_t>(K_WIDTH).value();
    std::uint32_t* words = values.data();
    const std::array<std::uint64_t, NodeOptions::K_MAX_PARAMETERS + 1> parameters{};
    NodeOptions options;
    options.parameters = span<const std::uint64_t>(parameters.data(), GetParam().count);
    auto graph = rt.graph();

    graph.add(K_WIDTH, Reads(), Writes(values), [words](std::size_t i) { words[i] = 0u; }, options);

    EXPECT_EQ(graph.refusal() != nullptr, GetParam().refused);
    EXPECT_EQ(graph.size(), GetParam().recorded_nodes);
}

INSTANTIATE_TEST_SUITE_P(
    Rows, Small_NodeParameterLimit,
    ::testing::Values(ParameterRow{"AtTheLimit", NodeOptions::K_MAX_PARAMETERS, false, 1},
                      ParameterRow{"OnePastTheLimit", NodeOptions::K_MAX_PARAMETERS + 1, true, 0}),
    row_name);
