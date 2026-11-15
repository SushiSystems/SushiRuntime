/**************************************************************************/
/* test_operation_kind.cpp                                                */
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
 * @file test_operation_kind.cpp
 * @brief Tests OperationKind, the consumer-named class of work a node's operations belong to.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/core/operation_kind.hpp>

using namespace SushiRuntime;

/** @brief Tests that a default kind is the generic kind. */
TEST(Small_OperationKind, ADefaultKindIsGeneric)
{
    const OperationKind kind;

    EXPECT_TRUE(kind.is_generic());
    EXPECT_EQ(kind, OperationKinds::K_GENERIC);
}

/** @brief Tests that a kind is the FNV-1a hash of its name, the literal included. */
TEST(Small_OperationKind, AKindIsTheHashOfItsName)
{
    EXPECT_EQ("gemm"_kind.value, OperationID::hash("gemm", 4));
    EXPECT_EQ("gemm"_kind, OperationKind::named("gemm", 4));
    EXPECT_EQ(OperationKinds::K_MEMORY_BOUND, "memory_bound"_kind);
    EXPECT_EQ(OperationKinds::K_COMPUTE_BOUND, "compute_bound"_kind);
    EXPECT_FALSE("gemm"_kind.is_generic());
}

/** @brief Tests that distinct names give distinct kinds and that kinds order by their hash. */
TEST(Small_OperationKind, DistinctNamesGiveDistinctKinds)
{
    EXPECT_NE(OperationKinds::K_MEMORY_BOUND, OperationKinds::K_COMPUTE_BOUND);
    EXPECT_EQ(OperationKinds::K_MEMORY_BOUND < OperationKinds::K_COMPUTE_BOUND,
              OperationKinds::K_MEMORY_BOUND.value < OperationKinds::K_COMPUTE_BOUND.value);
}

/** @brief Tests that a kind is a four-byte trivially copyable value. */
TEST(Small_OperationKind, IsAFourByteValue)
{
    EXPECT_TRUE(std::is_trivially_copyable<OperationKind>::value);
    EXPECT_EQ(sizeof(OperationKind), 4u);
}
