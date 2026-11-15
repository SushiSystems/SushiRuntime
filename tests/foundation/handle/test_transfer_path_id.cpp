/**************************************************************************/
/* test_transfer_path_id.cpp                                              */
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

/**
 * @file test_transfer_path_id.cpp
 * @brief Tests TransferPathID's identity, ordering and hash.
 * @see tests/foundation/handle/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <functional>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/transfer_path_id.hpp>

using SushiRuntime::TransferPathID;

static_assert(TransferPathID::named("peer") == TransferPathID::named("peer"),
              "an id is a constant of its name");

TEST(Small_TransferPathID, DefaultNamesNoPath)
{
    EXPECT_FALSE(TransferPathID().is_set());
    EXPECT_EQ(TransferPathID().value(), 0U);
    EXPECT_NE(TransferPathID(), TransferPathID::named(""));
}

TEST(Small_TransferPathID, SameNameGivesSameIDAndDistinctNamesApart)
{
    EXPECT_EQ(TransferPathID::named("same_context"), TransferPathID::named("same_context"));
    EXPECT_NE(TransferPathID::named("same_context"), TransferPathID::named("peer"));
    EXPECT_NE(TransferPathID::named("host_staged"), TransferPathID::named("file"));
    EXPECT_TRUE(TransferPathID::named("peer").is_set());
}

TEST(Small_TransferPathID, KeepsAFixedValueAcrossRuns)
{
    EXPECT_EQ(TransferPathID::named("a").value(), 12638187200555641996ULL);
}

TEST(Small_TransferPathID, OrdersByValue)
{
    const TransferPathID first = TransferPathID::named("peer");
    const TransferPathID second = TransferPathID::named("file");
    EXPECT_NE(first < second, second < first);
    EXPECT_FALSE(first < first);
}

TEST(Small_TransferPathID, EqualIDsHashEqual)
{
    const std::hash<TransferPathID> hash;
    EXPECT_EQ(hash(TransferPathID::named("peer")), hash(TransferPathID::named("peer")));
    EXPECT_NE(hash(TransferPathID::named("peer")), hash(TransferPathID::named("file")));
}
