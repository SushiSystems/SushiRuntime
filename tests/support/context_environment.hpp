/**************************************************************************/
/* context_environment.hpp                                               */
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
 * @file context_environment.hpp
 * @brief Holds the one device RuntimeContext of an engine test program.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>

namespace TestSupport
{
/**
 * @brief Owns the device context every medium engine test of a program shares, and releases it after the last test.
 */
class ContextEnvironment final : public ::testing::Environment
{
    public:
        /**
         * @brief Returns the device context of the program, building it on the first call.
         * @pre A context can be built; the process stops, naming the error, when none can.
         */
        [[nodiscard]] static SushiRuntime::Internal::Execution::RuntimeContext& context()
        {
            std::shared_ptr<SushiRuntime::Internal::Execution::RuntimeContext>& held = held_context();
            if (!held)
                held = SushiRuntime::Test::make_context();
            return *held;
        }

        /** @brief Releases the context after the last test of the program. */
        void TearDown() override
        {
            held_context().reset();
        }

    private:
        /** @brief Returns the slot of the program's context. */
        static std::shared_ptr<SushiRuntime::Internal::Execution::RuntimeContext>& held_context()
        {
            static std::shared_ptr<SushiRuntime::Internal::Execution::RuntimeContext> slot;
            return slot;
        }
};

/**
 * @brief Registers the environment once per program, whatever number of units include this header.
 */
inline ::testing::Environment* const K_CONTEXT_ENVIRONMENT =
    ::testing::AddGlobalTestEnvironment(new ContextEnvironment());
} // namespace TestSupport
