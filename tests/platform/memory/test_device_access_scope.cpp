/**************************************************************************/
/* test_device_access_scope.cpp                                           */
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
 * @file test_device_access_scope.cpp
 * @brief Tests that DeviceAccessScope holds a gate open for exactly its lifetime.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/device_access_scope.hpp>
#include <SushiRuntime/memory/guarded_device_pages.hpp>

using SushiRuntime::Internal::Memory::DeviceAccessScope;
using SushiRuntime::Internal::Memory::GuardedDevicePages;

/** @brief Tests that a scope opens its gate while it lives. */
TEST(Small_DeviceAccessScope, OpensTheGateWhileItLives)
{
    GuardedDevicePages gate;

    const DeviceAccessScope scope(&gate);

    EXPECT_TRUE(gate.is_open());
}

/** @brief Tests that a scope closes its gate when it ends. */
TEST(Small_DeviceAccessScope, ClosesTheGateWhenItEnds)
{
    GuardedDevicePages gate;
    std::optional<DeviceAccessScope> scope;
    scope.emplace(&gate);

    scope.reset();

    EXPECT_FALSE(gate.is_open());
}
