/**************************************************************************/
/* recorder_fixtures.hpp                                                  */
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
 * @file recorder_fixtures.hpp
 * @brief Declares the fixtures the recorder tests share: the program's runtime and a host runtime that clear the placement rules after each test.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/host_runtime.hpp"
#include "support/runtime_environment.hpp"

/** @brief Fixture base of the recorder tests: the program's runtime, with the placement rules cleared after each test. */
class RecorderTest : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Clears the rules, so no later test places a node by this test's rule. */
        void TearDown() override { (void)runtime().clear_placement_rules(); }
};

/** @brief Gives each small recorder test a host runtime and clears its placement rules afterwards. */
class RecorderHostTest : public TestSupport::HostRuntimeTest
{
    protected:
        /** @brief Clears the rules, so no later test places a node by this test's rule. */
        void TearDown() override
        {
            (void)runtime().clear_placement_rules();
        }
};
