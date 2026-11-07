/**************************************************************************/
/* runtime_contract.hpp                                                   */
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
 * @file runtime_contract.hpp
 * @brief Declares RuntimeContract, the typed suite a host runtime and a device runtime both pass.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

namespace TestSupport
{
    /**
     * @brief Fixture of the contract; Maker::runtime(slot) returns the runtime under test.
     * @tparam Maker A type whose static runtime(std::optional<Runtime>&) fills the slot or
     *         returns a runtime the program shares.
     */
    template <class Maker>
    class RuntimeContract : public ::testing::Test
    {
        protected:
            /** @brief Returns the runtime under test. */
            [[nodiscard]] SushiRuntime::Runtime& runtime() { return Maker::runtime(slot_); }

        private:
            /** @brief Holds a runtime the maker builds for one case. */
            std::optional<SushiRuntime::Runtime> slot_;
    };

    TYPED_TEST_SUITE_P(RuntimeContract);

    /** @brief Tests that a launch over a count writes every element of its buffer. */
    TYPED_TEST_P(RuntimeContract, ALaunchOverACountWritesEveryElement)
    {
        constexpr std::size_t K_COUNT = 9;
        auto buffer = this->runtime().template buffer<int>(K_COUNT).value();
        int* cells = buffer.data();
        auto graph = this->runtime().graph();
        graph.add(K_COUNT, SushiRuntime::Reads{}, SushiRuntime::Writes(buffer),
                  [cells](std::size_t i) { cells[i] = static_cast<int>(i) * 3; });

        ASSERT_TRUE(graph.run().has_value());

        EXPECT_EQ(buffer[8], 24);
    }

    /** @brief Tests that a host node ordered after a launch reads what the launch wrote. */
    TYPED_TEST_P(RuntimeContract, AHostNodeAfterALaunchReadsItsWrites)
    {
        constexpr std::size_t K_COUNT = 4;
        auto source = this->runtime().template buffer<int>(K_COUNT).value();
        auto total = this->runtime().template buffer<int>(1).value();
        int* cells = source.data();
        int* sum = total.data();
        auto graph = this->runtime().graph();
        graph.add(K_COUNT, SushiRuntime::Reads{}, SushiRuntime::Writes(source),
                  [cells](std::size_t i) { cells[i] = static_cast<int>(i) + 1; });
        graph.add_host(SushiRuntime::Reads(source), SushiRuntime::Writes(total),
                       [cells, sum]() { *sum = cells[0] + cells[1] + cells[2] + cells[3]; });

        ASSERT_TRUE(graph.run().has_value());

        EXPECT_EQ(total[0], 10);
    }

    /** @brief Tests that a host node that throws fails the run. */
    TYPED_TEST_P(RuntimeContract, AThrowingHostNodeFailsTheRun)
    {
        auto graph = this->runtime().graph();
        graph.add_host([]() { throw std::runtime_error("boom"); });

        EXPECT_FALSE(graph.run().has_value());
    }

    /** @brief Tests that a run of several steps runs an untracked host node once per step. */
    TYPED_TEST_P(RuntimeContract, ARunOfStepsRunsAHostNodeOncePerStep)
    {
        int ticks = 0;
        auto graph = this->runtime().graph();
        graph.add_host([&ticks]() { ++ticks; });

        ASSERT_TRUE(graph.run(5).has_value());

        EXPECT_EQ(ticks, 5);
    }

    REGISTER_TYPED_TEST_SUITE_P(RuntimeContract, ALaunchOverACountWritesEveryElement,
                                AHostNodeAfterALaunchReadsItsWrites, AThrowingHostNodeFailsTheRun,
                                ARunOfStepsRunsAHostNodeOncePerStep);
} // namespace TestSupport
