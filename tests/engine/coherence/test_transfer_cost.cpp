/**************************************************************************/
/* test_transfer_cost.cpp                                                 */
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
 * @file test_transfer_cost.cpp
 * @brief Tests that the uniform transfer cost prices every path by its bytes alone.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/transfer_cost.hpp>

#include "fake_transfer_path.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_TransferCost, TheUniformCostIsTheBytesOnEveryPath)
    {
        const TransferCost cost = uniform_transfer_cost();
        FakeTransferPath open;
        ClosedTransferPath closed;

        EXPECT_EQ(cost(open, MemorySpaceID(1), MemorySpaceID(2), 128), 128.0);
        EXPECT_EQ(cost(closed, MemorySpaceID(3), MemorySpaceID(0), 128), 128.0);
    }
} // namespace
