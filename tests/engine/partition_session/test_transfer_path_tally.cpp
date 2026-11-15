/**************************************************************************/
/* test_transfer_path_tally.cpp                                           */
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
 * @file test_transfer_path_tally.cpp
 * @brief Tests that TransferPathTally counts copies by destination and path and forwards each one.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include <SushiRuntime/partition_session/transfer_path_tally.hpp>

#include "support/transfer_path_doubles.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_TransferPathTally, TwoPathsOfOneNameButAnotherIdentityAreCountedApart)
    {
        TransferPathTally tally;
        tally.copied(TransferRecord{"pcie", MemorySpaceID(0), MemorySpaceID(1), 100, TransferPathID::named("pcie")});
        tally.copied(TransferRecord{"pcie", MemorySpaceID(0), MemorySpaceID(1), 50, TransferPathID::named("peer")});

        const PathTallies tallies = tally.tallies();

        ASSERT_EQ(tallies.size(), 2u);
        EXPECT_EQ(tallies.at({MemorySpaceID(1), TransferPathID::named("peer")}).path, "pcie");
    }

    TEST(Small_TransferPathTally, CopiesAreCountedByDestinationAndPath)
    {
        TransferPathTally tally;
        tally.copied(TransferRecord{"pcie", MemorySpaceID(0), MemorySpaceID(1), 100, TransferPathID::named("pcie")});
        tally.copied(TransferRecord{"pcie", MemorySpaceID(0), MemorySpaceID(1), 50, TransferPathID::named("pcie")});
        tally.copied(TransferRecord{"host_staged", MemorySpaceID(0), MemorySpaceID(2), 10, TransferPathID::named("host_staged")});

        const PathTallies tallies = tally.tallies();

        ASSERT_EQ(tallies.size(), 2u);
        EXPECT_EQ(tallies.at({MemorySpaceID(1), TransferPathID::named("pcie")}).copies, 2u);
        EXPECT_EQ(tallies.at({MemorySpaceID(1), TransferPathID::named("pcie")}).bytes, 150u);
        EXPECT_EQ(tallies.at({MemorySpaceID(2), TransferPathID::named("host_staged")}).copies, 1u);
    }

    TEST(Small_TransferPathTally, EachCopyReachesTheNextObserver)
    {
        TransferPathTally tally;
        const auto next = std::make_shared<RecordingTransferObserver>();
        tally.set_next(next);

        tally.copied(TransferRecord{"pcie", MemorySpaceID(0), MemorySpaceID(1), 100, TransferPathID::named("pcie")});

        ASSERT_EQ(next->heard().size(), 1u);
        EXPECT_EQ(next->heard()[0].path, "pcie");
    }

    TEST(Small_TransferPathTally, ClearForgetsEveryTally)
    {
        TransferPathTally tally;
        tally.copied(TransferRecord{"pcie", MemorySpaceID(0), MemorySpaceID(1), 100, TransferPathID::named("pcie")});

        tally.clear();

        EXPECT_TRUE(tally.tallies().empty());
    }
} // namespace
