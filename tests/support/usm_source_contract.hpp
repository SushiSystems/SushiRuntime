/**************************************************************************/
/* usm_source_contract.hpp                                                */
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
 * @file usm_source_contract.hpp
 * @brief Declares USMSourceContract, the typed suite every USMSource adapter passes.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/memory/usm_source.hpp>

namespace TestSupport
{
    /**
     * @brief Fixture of the contract; Maker::make() returns the source under test.
     * @tparam Maker A type with a static make() returning std::unique_ptr<USMSource> of one device.
     */
    template <class Maker>
    class USMSourceContract : public ::testing::Test
    {
        protected:
            /** @brief Makes the source each case runs on. */
            void SetUp() override { source_ = Maker::make(); }

            /** @brief Returns the source under test. */
            [[nodiscard]] SushiRuntime::Internal::Memory::USMSource& source() { return *source_; }

        private:
            /** @brief The source under test, one per case. */
            std::unique_ptr<SushiRuntime::Internal::Memory::USMSource> source_;
    };

    TYPED_TEST_SUITE_P(USMSourceContract);

    /** @brief Tests that a shared allocation holds the bytes written through it until it is freed. */
    TYPED_TEST_P(USMSourceContract, ASharedAllocationKeepsItsBytes)
    {
        constexpr std::size_t K_BYTES = 64;
        SushiRuntime::Internal::Memory::USMRequest request;
        request.kind = SushiRuntime::Internal::Memory::USMKind::Shared;
        request.bytes = K_BYTES;
        void* const address = this->source().allocate(request);
        ASSERT_NE(address, nullptr);
        std::memset(address, 0x5A, K_BYTES);

        EXPECT_EQ(static_cast<unsigned char*>(address)[K_BYTES - 1], 0x5A);

        this->source().free(address, 0);
    }

    /** @brief Tests that an allocation with an alignment starts on that alignment. */
    TYPED_TEST_P(USMSourceContract, AnAlignedAllocationStartsOnItsAlignment)
    {
        constexpr std::size_t K_ALIGNMENT = 256;
        SushiRuntime::Internal::Memory::USMRequest request;
        request.kind = SushiRuntime::Internal::Memory::USMKind::Host;
        request.bytes = 24;
        request.alignment = K_ALIGNMENT;
        void* const address = this->source().allocate(request);
        ASSERT_NE(address, nullptr);

        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(address) % K_ALIGNMENT, 0u);

        this->source().free(address, 0);
    }

    /** @brief Tests that a device shares its memory context with itself. */
    TYPED_TEST_P(USMSourceContract, ADeviceSharesItsContextWithItself)
    {
        EXPECT_TRUE(this->source().shares_context(0, 0));
    }

    /** @brief Tests that a host stack address no device of the source allocated has no owner. */
    TYPED_TEST_P(USMSourceContract, AStackAddressHasNoOwner)
    {
        int local = 0;

        EXPECT_FALSE(this->source().resolve_owner(&local).has_value());
    }

    /** @brief Tests that a source of one device reports one device. */
    TYPED_TEST_P(USMSourceContract, ASourceOfOneDeviceCountsOne)
    {
        EXPECT_EQ(this->source().device_count(), 1u);
    }

    REGISTER_TYPED_TEST_SUITE_P(USMSourceContract, ASharedAllocationKeepsItsBytes,
                                AnAlignedAllocationStartsOnItsAlignment,
                                ADeviceSharesItsContextWithItself, AStackAddressHasNoOwner,
                                ASourceOfOneDeviceCountsOne);
} // namespace TestSupport
