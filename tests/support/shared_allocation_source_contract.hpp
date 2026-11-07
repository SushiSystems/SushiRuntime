/**************************************************************************/
/* shared_allocation_source_contract.hpp                                  */
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
 * @file shared_allocation_source_contract.hpp
 * @brief Declares SharedAllocationSourceContract, the suite every SharedAllocationSource passes.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/usm_guard/shared_allocation_source.hpp>

namespace TestSupport
{
    /**
     * @brief Fixture of the contract; Maker::make() returns the source under test.
     * @tparam Maker A type with a static make() returning
     *               std::unique_ptr<SushiRuntime::Test::SharedAllocationSource>.
     */
    template <class Maker>
    class SharedAllocationSourceContract : public ::testing::Test
    {
        protected:
            /** @brief Makes the source each case runs on. */
            void SetUp() override { source_ = Maker::make(); }

            /** @brief Returns the source under test. */
            [[nodiscard]] SushiRuntime::Test::SharedAllocationSource& source() { return *source_; }

        private:
            /** @brief The source under test, one per case. */
            std::unique_ptr<SushiRuntime::Test::SharedAllocationSource> source_;
    };

    TYPED_TEST_SUITE_P(SharedAllocationSourceContract);

    /** @brief Tests that an allocation keeps the bytes the host writes until it is freed. */
    TYPED_TEST_P(SharedAllocationSourceContract, AnAllocationKeepsTheHostsBytes)
    {
        constexpr std::size_t K_BYTES = 48;
        void* const address = this->source().allocate(K_BYTES, alignof(int));
        ASSERT_NE(address, nullptr);
        std::memset(address, 0x5A, K_BYTES);

        EXPECT_EQ(static_cast<unsigned char*>(address)[K_BYTES - 1], 0x5A);

        this->source().free(address);
    }

    /** @brief Tests that an allocation starts on the alignment asked for. */
    TYPED_TEST_P(SharedAllocationSourceContract, AnAllocationStartsOnItsAlignment)
    {
        constexpr std::size_t K_ALIGNMENT = 32;
        void* const address = this->source().allocate(24, K_ALIGNMENT);
        ASSERT_NE(address, nullptr);

        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(address) % K_ALIGNMENT, 0u);

        this->source().free(address);
    }

    /** @brief Tests that two live allocations do not share an address. */
    TYPED_TEST_P(SharedAllocationSourceContract, TwoLiveAllocationsDiffer)
    {
        void* const first = this->source().allocate(16, alignof(int));
        void* const second = this->source().allocate(16, alignof(int));

        EXPECT_NE(first, second);

        this->source().free(first);
        this->source().free(second);
    }

    REGISTER_TYPED_TEST_SUITE_P(SharedAllocationSourceContract, AnAllocationKeepsTheHostsBytes,
                                AnAllocationStartsOnItsAlignment, TwoLiveAllocationsDiffer);
} // namespace TestSupport
