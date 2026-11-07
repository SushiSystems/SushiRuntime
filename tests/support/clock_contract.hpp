/**************************************************************************/
/* clock_contract.hpp                                                     */
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
 * @file clock_contract.hpp
 * @brief Declares ClockContract, the typed suite every Clock adapter passes.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/core/clock.hpp>

namespace TestSupport
{
    /** @brief Fixture of the contract every Clock adapter passes. */
    template <class Adapter>
    class ClockContract : public ::testing::Test
    {
        protected:
            /** @brief The clock under test, one per case. */
            Adapter clock_;
    };

    TYPED_TEST_SUITE_P(ClockContract);

    /** @brief Tests that a later reading is never smaller than an earlier one. */
    TYPED_TEST_P(ClockContract, ALaterReadingIsNeverSmaller)
    {
        const SushiRuntime::Clock& clock = this->clock_;
        const std::uint64_t first = clock.now_ns();

        EXPECT_GE(clock.now_ns(), first);
    }

    REGISTER_TYPED_TEST_SUITE_P(ClockContract, ALaterReadingIsNeverSmaller);

} // namespace TestSupport
