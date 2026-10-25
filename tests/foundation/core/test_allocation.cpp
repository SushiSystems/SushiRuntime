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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
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

#include <gtest/gtest.h>

#include <SushiRuntime/core/allocation.hpp>

namespace AllocationTests
{
    using SushiRuntime::Detail::Allocation;
    using SushiRuntime::Detail::AllocationOwner;


    namespace
    {
        /** @brief Counts the releases it receives and remembers the last address. */
        class CountingOwner final : public AllocationOwner
        {
            public:
                /** @brief Binds the owner to the counters it reports into. */
                CountingOwner(int& releases, void*& last) : releases_(releases), last_(last) {}

                /** @brief Records one release of @p address. */
                void release(void* address) noexcept override
                {
                    ++releases_;
                    last_ = address;
                }

            private:
                int& releases_;
                void*& last_;
        };

        /** @brief Returns a distinct non-null address that is never dereferenced. */
        void* fake(std::uintptr_t n) { return reinterpret_cast<void*>(n * 4096u); }
    }

    TEST(Small_Allocation, ReleasesOnceOnDestruction)
    {
        int releases = 0;
        void* last = nullptr;
        {
            Allocation allocation(fake(1), 64, std::make_unique<CountingOwner>(releases, last));
            EXPECT_EQ(allocation.address(), fake(1));
            EXPECT_EQ(allocation.bytes(), 64u);
        }
        EXPECT_EQ(releases, 1);
        EXPECT_EQ(last, fake(1));
    }

    TEST(Small_Allocation, MoveTransfersTheReleaseAndEmptiesTheSource)
    {
        int releases = 0;
        void* last = nullptr;
        {
            Allocation source(fake(2), 32, std::make_unique<CountingOwner>(releases, last));
            Allocation target(std::move(source));
            EXPECT_TRUE(source.empty());
            EXPECT_EQ(target.address(), fake(2));
        }
        EXPECT_EQ(releases, 1);
        EXPECT_EQ(last, fake(2));
    }

    TEST(Small_Allocation, MoveAssignmentReleasesTheOverwrittenAddressFirst)
    {
        int releases = 0;
        void* last = nullptr;
        Allocation first(fake(3), 8, std::make_unique<CountingOwner>(releases, last));
        Allocation second(fake(4), 8, std::make_unique<CountingOwner>(releases, last));
        first = std::move(second);
        EXPECT_EQ(releases, 1);
        EXPECT_EQ(last, fake(3));
        EXPECT_EQ(first.address(), fake(4));
    }

    TEST(Small_Allocation, AnEmptyAllocationHasNoAddressOrOwner)
    {
        Allocation allocation;
        EXPECT_TRUE(allocation.empty());
        EXPECT_EQ(allocation.bytes(), 0u);
    }

    TEST(Small_Allocation, ResetOnANonNullAddressWithNoOwnerReleasesNothing)
    {
        Allocation allocation(fake(9), 16, nullptr);
        EXPECT_NO_THROW(allocation.reset());
        EXPECT_TRUE(allocation.empty());
    }

} // namespace AllocationTests
