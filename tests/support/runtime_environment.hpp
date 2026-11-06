/**************************************************************************/
/* runtime_environment.hpp                                                */
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
 * @file runtime_environment.hpp
 * @brief Holds the one runtime and the one device queue of a test program, and the fixture base that hands them out.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <optional>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

/**
 * @brief Test helpers shared by the modules of this repository.
 */
namespace TestSupport
{

/**
 * @brief Owns the runtime and the device queue of the program, made on first use and released after the last test.
 */
class RuntimeEnvironment final : public ::testing::Environment
{
    public:
        /**
         * @brief Returns the runtime of the program, creating it on the first call.
         * @return The runtime every medium test of the program shares.
         */
        [[nodiscard]] static SushiRuntime::Runtime& runtime()
        {
            std::optional<SushiRuntime::Runtime>& held = held_runtime();
            if (!held)
                held.emplace(SushiRuntime::Runtime::create().value());
            return *held;
        }

        /**
         * @brief Returns the device queue of the program, creating it on the first call.
         * @return The queue every medium test that submits its own work shares.
         */
        [[nodiscard]] static sycl::queue& queue()
        {
            std::optional<sycl::queue>& held = held_queue();
            if (!held)
                held.emplace();
            return *held;
        }

        /**
         * @brief Releases the queue and the runtime after the last test of the program.
         */
        void TearDown() override
        {
            held_queue().reset();
            held_runtime().reset();
        }

    private:
        /** @brief Returns the slot of the program's runtime. */
        static std::optional<SushiRuntime::Runtime>& held_runtime()
        {
            static std::optional<SushiRuntime::Runtime> slot;
            return slot;
        }

        /** @brief Returns the slot of the program's queue. */
        static std::optional<sycl::queue>& held_queue()
        {
            static std::optional<sycl::queue> slot;
            return slot;
        }
};

/**
 * @brief Registers the environment once per program, whatever number of units include this header.
 */
inline ::testing::Environment* const K_RUNTIME_ENVIRONMENT =
    ::testing::AddGlobalTestEnvironment(new RuntimeEnvironment());

/**
 * @brief Fixture base of every medium test that needs the runtime or the device.
 */
class RuntimeTest : public ::testing::Test
{
    protected:
        /** @brief Returns the runtime of the program. */
        [[nodiscard]] static SushiRuntime::Runtime& runtime() { return RuntimeEnvironment::runtime(); }

        /** @brief Returns the device queue of the program. */
        [[nodiscard]] static sycl::queue& queue() { return RuntimeEnvironment::queue(); }
};

}
