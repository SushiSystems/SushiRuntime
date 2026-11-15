/**************************************************************************/
/* test_borrowed_index.cpp                                                */
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
 * @file test_borrowed_index.cpp
 * @brief Tests the Borrowed handles BorrowedIndex gives byte ranges of caller-owned memory.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/memory/borrowed_index.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Memory;

namespace
{
    /** @brief Holds a directory and the index that mints in it. */
    class Small_BorrowedIndex : public ::testing::Test
    {
        protected:
            /// @brief The directory the handles are minted in.
            ResidencyDirectory directory_{0};

            /// @brief The index under test.
            BorrowedIndex index_{directory_};

            /// @brief Memory the test borrows ranges of.
            unsigned char bytes_[64] = {};
    };

    /** @brief Tests that one range borrowed twice gets one handle, minted once, flagged Borrowed. */
    TEST_F(Small_BorrowedIndex, OneRangeGetsOneHandle)
    {
        const Result<BorrowedHandle> first = index_.acquire(bytes_, 32);
        const Result<BorrowedHandle> second = index_.acquire(bytes_, 32);

        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_TRUE(first->minted);
        EXPECT_FALSE(second->minted);
        EXPECT_EQ(first->handle, second->handle);
        EXPECT_TRUE(has_flags(first->handle.flags(), HandleFlags::Borrowed));
    }

    /** @brief Tests that the handle stays live until its last borrow is released. */
    TEST_F(Small_BorrowedIndex, TheLastReleaseRetiresTheHandle)
    {
        const GlobalHandle handle = index_.acquire(bytes_, 32)->handle;
        static_cast<void>(index_.acquire(bytes_, 32));

        EXPECT_EQ(index_.release(handle), HandleStatus::Ok);
        EXPECT_FALSE(directory_.is_stale(handle));
        EXPECT_EQ(index_.release(handle), HandleStatus::Ok);
        EXPECT_TRUE(directory_.is_stale(handle));
        EXPECT_EQ(index_.release(handle), HandleStatus::NullHandle);
    }

    /** @brief Tests that disjoint ranges get distinct handles and find() answers by containment. */
    TEST_F(Small_BorrowedIndex, DisjointRangesGetDistinctHandles)
    {
        const GlobalHandle low = index_.acquire(bytes_, 16)->handle;
        const GlobalHandle high = index_.acquire(bytes_ + 16, 16)->handle;

        EXPECT_NE(low, high);
        EXPECT_EQ(index_.find(bytes_ + 8), low);
        EXPECT_EQ(index_.find(bytes_ + 16), high);
        EXPECT_FALSE(index_.find(bytes_ + 40).has_value());
    }

    /** @brief Tests that a range overlapping a live range it does not equal is refused. */
    TEST_F(Small_BorrowedIndex, AnOverlappingRangeIsRefused)
    {
        ASSERT_TRUE(index_.acquire(bytes_, 32).has_value());

        const Result<BorrowedHandle> inside = index_.acquire(bytes_ + 8, 8);
        const Result<BorrowedHandle> across = index_.acquire(bytes_ + 24, 16);

        ASSERT_FALSE(inside.has_value());
        EXPECT_EQ(inside.error().code, ErrorCode::InvalidArgument);
        ASSERT_FALSE(across.has_value());
        EXPECT_EQ(across.error().code, ErrorCode::InvalidArgument);
    }

    /** @brief Tests that a null address or an empty range is refused. */
    TEST_F(Small_BorrowedIndex, ANullOrEmptyRangeIsRefused)
    {
        EXPECT_FALSE(index_.acquire(nullptr, 8).has_value());
        EXPECT_FALSE(index_.acquire(bytes_, 0).has_value());
    }
} // namespace
