/**************************************************************************/
/* test_matrix_distance_model.cpp                                         */
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
 * @file test_matrix_distance_model.cpp
 * @brief Tests MatrixDistanceModel: a distance is the entry of the row of its source node.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/topology/matrix_distance_model.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

namespace
{
    /** @brief One matrix, and the distance it must give between two nodes. */
    struct DistanceRow
    {
        /** @brief The row name gtest prints. */
        const char* name;
        /** @brief The matrix handed to the model. */
        std::vector<std::vector<int>> matrix;
        /** @brief The node the distance is read from. */
        int from;
        /** @brief The node the distance is read to. */
        int to;
        /** @brief The entry of row @p from, column @p to. */
        int expected;
    };
} // namespace

class Small_MatrixDistanceModel : public ::testing::TestWithParam<DistanceRow>
{
};

/** @brief Tests that a distance is the matrix entry of the row of its source node. */
TEST_P(Small_MatrixDistanceModel, ReadsTheSourceRow)
{
    const DistanceRow& row = GetParam();
    const MatrixDistanceModel model{row.matrix};

    EXPECT_EQ(model.node_count(), static_cast<int>(row.matrix.size()));
    EXPECT_EQ(model.distance(row.from, row.to), row.expected);
}

INSTANTIATE_TEST_SUITE_P(
    Matrices, Small_MatrixDistanceModel,
    ::testing::Values(
        DistanceRow{"AcpiLocalOfNodeZero", {{10, 21}, {21, 10}}, 0, 0, 10},
        DistanceRow{"AcpiLocalOfNodeOne", {{10, 21}, {21, 10}}, 1, 1, 10},
        DistanceRow{"AcpiRemoteForward", {{10, 21}, {21, 10}}, 0, 1, 21},
        DistanceRow{"AcpiRemoteBackward", {{10, 21}, {21, 10}}, 1, 0, 21},
        DistanceRow{"AsymmetricForward", {{10, 12}, {20, 10}}, 0, 1, 12},
        DistanceRow{"AsymmetricBackward", {{10, 12}, {20, 10}}, 1, 0, 20},
        DistanceRow{"ThreeNodesCheapHop", {{10, 16, 32}, {16, 10, 16}, {32, 16, 10}}, 0, 1, 16},
        DistanceRow{"ThreeNodesTwoHops", {{10, 16, 32}, {16, 10, 16}, {32, 16, 10}}, 0, 2, 32}),
    [](const ::testing::TestParamInfo<DistanceRow>& info) { return std::string(info.param.name); });
