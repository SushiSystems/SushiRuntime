/**************************************************************************/
/* test_system_memory_probe.cpp                                           */
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
 * @file test_system_memory_probe.cpp
 * @brief Tests that SystemMemoryProbe reads the machine's memory from the operating system.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/memory/system_memory_probe.hpp>

using namespace SushiRuntime::Internal::Memory;

namespace
{
    TEST(Small_SystemMemoryProbe, ReportsSomeMemoryAndNoMoreFreeThanTotal)
    {
        const ProbedMemory measured = SystemMemoryProbe{}.probe();

        EXPECT_GT(measured.total_bytes, 0U);
        EXPECT_LE(measured.free_bytes, measured.total_bytes);
    }
} // namespace
