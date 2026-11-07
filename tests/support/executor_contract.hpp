/**************************************************************************/
/* executor_contract.hpp                                                  */
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
 * @file executor_contract.hpp
 * @brief Declares ExecutorContract, the typed suite every Executor adapter passes.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <atomic>
#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/core/executor.hpp>

namespace TestSupport
{
    /**
     * @brief Fixture of the contract every Executor adapter passes.
     * @tparam Maker A type with a static make() returning a std::unique_ptr to the adapter and a
     *         static drain(adapter&) that returns once every posted job has run.
     */
    template <class Maker>
    class ExecutorContract : public ::testing::Test
    {
        protected:
            /** @brief Returns the executor under test, through its port. */
            [[nodiscard]] SushiRuntime::Executor& executor() { return *executor_; }

            /** @brief Returns once every job posted so far, and every job they posted, has run. */
            void drain() { Maker::drain(*executor_); }

        private:
            /** @brief The adapter under test, one per case. */
            decltype(Maker::make()) executor_ = Maker::make();
    };

    TYPED_TEST_SUITE_P(ExecutorContract);

    /** @brief Tests that every posted job runs exactly once. */
    TYPED_TEST_P(ExecutorContract, EveryPostedJobRunsOnce)
    {
        std::atomic<int> first{0};
        std::atomic<int> second{0};
        std::atomic<int> third{0};
        this->executor().post([&first]() { ++first; });
        this->executor().post([&second]() { ++second; });
        this->executor().post([&third]() { ++third; });

        this->drain();

        EXPECT_EQ(first.load(), 1);
        EXPECT_EQ(second.load(), 1);
        EXPECT_EQ(third.load(), 1);
    }

    /** @brief Tests that a job a running job posts runs too. */
    TYPED_TEST_P(ExecutorContract, AJobPostedByAJobRuns)
    {
        std::atomic<int> inner{0};
        SushiRuntime::Executor& executor = this->executor();
        executor.post([&executor, &inner]() { executor.post([&inner]() { ++inner; }); });

        this->drain();

        EXPECT_EQ(inner.load(), 1);
    }

    /** @brief Tests that run_one on an executor holding no job runs nothing and says so. */
    TYPED_TEST_P(ExecutorContract, RunOneOnNothingPendingReturnsFalse)
    {
        EXPECT_FALSE(this->executor().run_one());
    }

    REGISTER_TYPED_TEST_SUITE_P(ExecutorContract, EveryPostedJobRunsOnce, AJobPostedByAJobRuns,
                                RunOneOnNothingPendingReturnsFalse);
} // namespace TestSupport
