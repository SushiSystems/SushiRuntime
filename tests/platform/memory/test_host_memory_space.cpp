/**************************************************************************/
/* test_host_memory_space.cpp                                             */
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
 * @file test_host_memory_space.cpp
 * @brief Tests that a host memory space reports its NUMA node and the reserve its probe measured.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/memory/host_memory_space.hpp>

#include "fake_memory_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Memory;

namespace
{
    TEST(Small_HostMemorySpace, ReportsTheMeasuredReserveAndNode)
    {
        const FakeMemoryProbe probe(4096, 1024);
        const HostMemorySpace space(
            HostMemorySpaceDescription{MemorySpaceID(0), 0, 1, {MemorySpaceID(2)}}, probe);

        EXPECT_EQ(probe.probe_count(), 1);
        EXPECT_EQ(space.kind_name(), "host");
        EXPECT_EQ(space.capacity_bytes(), 4096U);
        EXPECT_EQ(space.reserve_bytes(), 3072U);
        EXPECT_EQ(space.numa_node(), 1U);
        ASSERT_EQ(space.direct_targets().size(), 1U);
        EXPECT_EQ(space.direct_targets()[0], MemorySpaceID(2));
    }

    TEST(Small_HostMemorySpace, AnswersTheKindQueries)
    {
        const FakeMemoryProbe probe(4096, 1024);
        const HostMemorySpace space(HostMemorySpaceDescription{MemorySpaceID(0), 0, 1, {}}, probe);

        EXPECT_EQ(space.is_host_memory(), true);
        EXPECT_EQ(space.is_file_backed(), false);
    }
} // namespace
