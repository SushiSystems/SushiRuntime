/**************************************************************************/
/* host_runtime.hpp                                                       */
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
 * @file host_runtime.hpp
 * @brief Declares HostRuntimeTest, the fixture base that gives each small test its own host runtime.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/sushiruntime.hpp>

namespace TestSupport
{
    /** @brief The seed of every host runtime a fixture makes, so a failing order replays. */
    inline constexpr std::uint64_t K_HOST_RUNTIME_SEED = 0x5EED;

    /**
     * @brief Fixture base of a small test that runs graphs: one host runtime per test, with the
     *        executor and the clock the test steps.
     */
    class HostRuntimeTest : public ::testing::Test
    {
        protected:
            /** @brief Makes the test's runtime on the fixture's executor and clock, before any SetUp(). */
            HostRuntimeTest() : runtime_(make_runtime(executor_, clock_))
            {

            }

            /** @brief Returns the test's host runtime. */
            [[nodiscard]] SushiRuntime::Runtime& runtime() { return *runtime_; }

            /** @brief Returns the executor every node of the runtime runs on. */
            [[nodiscard]] SushiRuntime::SeededExecutor& executor() { return *executor_; }

            /** @brief Returns the clock every recorded dispatch is stamped from. */
            [[nodiscard]] SushiRuntime::ManualClock& clock() { return *clock_; }

        private:
            /** @brief Returns a host runtime on @p executor and @p clock. */
            [[nodiscard]] static SushiRuntime::Runtime make_runtime(
                const std::shared_ptr<SushiRuntime::SeededExecutor>& executor,
                const std::shared_ptr<SushiRuntime::ManualClock>& clock)
            {
                SushiRuntime::HostRuntimeConfig config;
                config.executor = executor;
                config.clock = clock;
                return SushiRuntime::Runtime::create_host(config).value();
            }

            /** @brief Runs the runtime's nodes, in the order the fixture's seed draws. */
            std::shared_ptr<SushiRuntime::SeededExecutor> executor_ =
                std::make_shared<SushiRuntime::SeededExecutor>(K_HOST_RUNTIME_SEED);

            /** @brief Times the runtime's dispatches; it moves only when the test advances it. */
            std::shared_ptr<SushiRuntime::ManualClock> clock_ =
                std::make_shared<SushiRuntime::ManualClock>();

            /** @brief The test's runtime; declared after the executor and the clock it is made on. */
            std::optional<SushiRuntime::Runtime> runtime_;
    };
} // namespace TestSupport
