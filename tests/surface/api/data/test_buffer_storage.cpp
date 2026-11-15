/**************************************************************************/
/* test_buffer_storage.cpp                                                */
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
 * @file test_buffer_storage.cpp
 * @brief Tests the queries every BufferStorage answers, once for each strategy.
 * @see tests/surface/api/data/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <SushiRuntime/api/data/buffer_storage.hpp>
#include <SushiRuntime/api/data/planned_storage.hpp>
#include <SushiRuntime/api/data/stated_storage.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Makes the stated strategy over a block that no test dereferences. */
    struct StatedCase
    {
        static std::unique_ptr<Detail::BufferStorage> make()
        {
            Detail::MemoryBlock block{Detail::Allocation(reinterpret_cast<void*>(4096), 64, nullptr)};
            return std::make_unique<Detail::StatedStorage>(std::move(block), DeviceIndex{2},
                                                           Residency::Device);
        }
    };

    /** @brief Makes the planned strategy. */
    struct PlannedCase
    {
        static std::unique_ptr<Detail::BufferStorage> make()
        {
            return std::make_unique<Detail::PlannedStorage>();
        }
    };

    /** @brief Holds the storage of one strategy for each contract test. */
    template <class Case>
    class Small_BufferStorageContract : public ::testing::Test
    {
        protected:
            std::unique_ptr<Detail::BufferStorage> storage = Case::make();
    };

    using Strategies = ::testing::Types<StatedCase, PlannedCase>;
    TYPED_TEST_SUITE(Small_BufferStorageContract, Strategies, );

    TYPED_TEST(Small_BufferStorageContract, AStorageStatesADeviceExactlyWhenItIsNotPlanned)
    {
        EXPECT_EQ(this->storage->device_index().has_value(),
                  this->storage->residency() != Residency::Planned);
    }

    TYPED_TEST(Small_BufferStorageContract, HostAddressabilityFollowsTheResidencyRule)
    {
        EXPECT_EQ(this->storage->host_addressable(),
                  Detail::is_host_addressable(this->storage->residency()));
    }

    TYPED_TEST(Small_BufferStorageContract, TheHostRefusalReadsAsTheOpeningOfAMessage)
    {
        const std::string refusal = this->storage->host_refusal();

        ASSERT_GE(refusal.size(), 2u);
        EXPECT_EQ(refusal.substr(refusal.size() - 2), "; ");
    }

    TYPED_TEST(Small_BufferStorageContract, AStorageThatBorrowsNothingHasNoOwnerToObserve)
    {
        EXPECT_TRUE(this->storage->owner_life().expired());
    }
} // namespace
