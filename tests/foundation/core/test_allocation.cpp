/**************************************************************************/
/* test_allocation.cpp                                                    */
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
 * @file test_allocation.cpp
 * @brief Tests Allocation: one release on destruction, moves, and an empty or ownerless allocation.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/allocation.hpp>
#include <SushiRuntime/core/recording_allocation_owner.hpp>

namespace AllocationTests
{
    using SushiRuntime::Detail::Allocation;
    using SushiRuntime::Detail::RecordingAllocationOwner;

    namespace
    {
        /** @brief Returns a distinct non-null address that is never dereferenced. */
        void* fake(std::uintptr_t n) { return reinterpret_cast<void*>(n * 4096u); }
    }

    /** @brief Tests that an allocation reports its address and size and releases the address once at its end. */
    TEST(Small_Allocation, ReleasesOnceOnDestruction)
    {
        std::vector<void*> releases;
        {
            Allocation allocation(fake(1), 64, std::make_unique<RecordingAllocationOwner>(releases));
            EXPECT_EQ(allocation.address(), fake(1));
            EXPECT_EQ(allocation.bytes(), 64u);
        }
        const std::vector<void*> expected{fake(1)};
        EXPECT_EQ(releases, expected);
    }

    /** @brief Tests that a move hands the release to the target, which releases the address once. */
    TEST(Small_Allocation, MoveTransfersTheRelease)
    {
        std::vector<void*> releases;
        {
            Allocation source(fake(2), 32, std::make_unique<RecordingAllocationOwner>(releases));
            Allocation target(std::move(source));
            EXPECT_EQ(target.address(), fake(2));
        }
        const std::vector<void*> expected{fake(2)};
        EXPECT_EQ(releases, expected);
    }

    /** @brief Tests that a moved-from allocation is empty. */
    TEST(Small_Allocation, MoveEmptiesTheSource)
    {
        std::vector<void*> releases;
        Allocation source(fake(2), 32, std::make_unique<RecordingAllocationOwner>(releases));
        const Allocation target(std::move(source));
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(source.bytes(), 0u);
    }

    /** @brief Tests that move-assignment releases the overwritten address before it takes the new one. */
    TEST(Small_Allocation, MoveAssignmentReleasesTheOverwrittenAddressFirst)
    {
        std::vector<void*> releases;
        Allocation first(fake(3), 8, std::make_unique<RecordingAllocationOwner>(releases));
        Allocation second(fake(4), 8, std::make_unique<RecordingAllocationOwner>(releases));
        first = std::move(second);
        const std::vector<void*> expected{fake(3)};
        EXPECT_EQ(releases, expected);
        EXPECT_EQ(first.address(), fake(4));
    }

    /** @brief Tests that a default allocation is empty and holds no bytes. */
    TEST(Small_Allocation, AnEmptyAllocationHasNoAddressAndNoBytes)
    {
        const Allocation allocation;
        EXPECT_TRUE(allocation.empty());
        EXPECT_EQ(allocation.bytes(), 0u);
    }

    /** @brief Tests that reset empties an allocation that has no owner and releases nothing. */
    TEST(Small_Allocation, ResetOnANonNullAddressWithNoOwnerReleasesNothing)
    {
        Allocation allocation(fake(9), 16, nullptr);
        allocation.reset();
        EXPECT_TRUE(allocation.empty());
    }

} // namespace AllocationTests
