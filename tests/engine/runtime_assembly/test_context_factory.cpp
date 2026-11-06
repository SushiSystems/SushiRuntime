/**************************************************************************/
/* test_context_factory.cpp                                               */
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
 * @file test_context_factory.cpp
 * @brief Tests make_context(): a context over every device, with its settings.
 * @see tests/engine/runtime_assembly/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/execution/runtime_context.hpp>

#include <SushiRuntime/runtime_assembly/context_factory.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Test;

/** @brief Tests that the context is built and holds at least one device. */
TEST(Medium_ContextFactory, BuildsAContextWithADevice)
{
    const std::shared_ptr<Internal::Execution::RuntimeContext> context = make_context();
    ASSERT_NE(context, nullptr);
    EXPECT_FALSE(context->devices().devices().empty());
}
