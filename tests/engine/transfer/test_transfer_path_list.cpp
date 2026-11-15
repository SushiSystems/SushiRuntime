/**************************************************************************/
/* test_transfer_path_list.cpp                                            */
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
 * @file test_transfer_path_list.cpp
 * @brief Tests that admit_path() refuses a path whose TransferPathID a path of the list already has.
 * @see tests/engine/transfer/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/transfer/transfer_path_list.hpp>

#include "support/fake_transfer_path.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

/** @brief Tests that paths with different ids are all admitted, in order. */
TEST(Small_TransferPathList, PathsWithDistinctIDsAreAdmittedInOrder)
{
    FakeTransferPath fake;
    ClosedTransferPath closed;
    std::vector<TransferPath*> list;

    EXPECT_EQ(admit_path(list, fake), PathListStatus::Admitted);
    EXPECT_EQ(admit_path(list, closed), PathListStatus::Admitted);

    ASSERT_EQ(list.size(), 2U);
    EXPECT_EQ(list[0], &fake);
    EXPECT_EQ(list[1], &closed);
}

/** @brief Tests that a path whose id a listed path has is refused and not listed. */
TEST(Small_TransferPathList, ASecondPathWithTheSameIDIsRefused)
{
    FakeTransferPath first;
    FakeTransferPath second;
    std::vector<TransferPath*> list;
    ASSERT_EQ(admit_path(list, first), PathListStatus::Admitted);

    EXPECT_EQ(admit_path(list, second), PathListStatus::DuplicateID);

    ASSERT_EQ(list.size(), 1U);
    EXPECT_EQ(list[0], &first);
}

/** @brief Tests that building a list from candidates with one id twice fails with InvalidState. */
TEST(Small_TransferPathList, BuildingAListWithADuplicateIDFailsWithInvalidState)
{
    FakeTransferPath first;
    FakeTransferPath second;
    ClosedTransferPath closed;
    std::vector<TransferPath*> list;

    const Result<void> built = admit_paths({&first, &second, &closed}, list);

    ASSERT_FALSE(built);
    EXPECT_EQ(built.error().code, ErrorCode::InvalidState);
    EXPECT_EQ(list, (std::vector<TransferPath*>{&first, &closed}));
}

/** @brief Tests that building a list from distinct candidates succeeds. */
TEST(Small_TransferPathList, BuildingAListOfDistinctIDsSucceeds)
{
    FakeTransferPath fake;
    ClosedTransferPath closed;
    std::vector<TransferPath*> list;

    EXPECT_TRUE(admit_paths({&fake, &closed}, list));
    EXPECT_EQ(list.size(), 2U);
}
