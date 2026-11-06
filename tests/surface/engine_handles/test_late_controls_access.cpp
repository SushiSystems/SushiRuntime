/**************************************************************************/
/* test_late_controls_access.cpp                                          */
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
 * @file test_late_controls_access.cpp
 * @brief Tests engine_controls() and surface_controls(): one object, two names, one owner.
 * @see tests/surface/engine_handles/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/task/late_bindings.hpp>

#include <SushiRuntime/engine_handles/late_controls_access.hpp>

using namespace SushiRuntime;

/** @brief Tests that the surface share and the engine share name one binding and one owner. */
TEST(Small_LateControlsAccess, BothSharesNameOneBinding)
{
    auto bindings = std::make_shared<Internal::Graph::LateBindings>();
    const Internal::Graph::LateBindings* const binding = bindings.get();
    const std::weak_ptr<Internal::Graph::LateBindings> watch = bindings;
    const std::shared_ptr<Detail::LateControls> controls = Detail::surface_controls(bindings);
    bindings.reset();
    EXPECT_FALSE(watch.expired()) << "the surface share does not own the binding";

    const std::shared_ptr<Internal::Graph::LateBindings> back = Detail::engine_controls(controls);
    EXPECT_EQ(back.get(), binding);
    EXPECT_EQ(Detail::engine_controls(controls.get()), binding);

    back->count.store(5);
    EXPECT_EQ(binding->count.load(), 5u);
}
