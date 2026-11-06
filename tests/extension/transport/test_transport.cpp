/**************************************************************************/
/* test_transport.cpp                                                     */
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
 * @file test_transport.cpp
 * @brief Tests the vocabulary of transport.hpp: a result that holds no failure, and peer ids.
 * @see tests/extension/transport/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/transport/transport.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that a default result is Ok with no message. */
TEST(Medium_Transport, ADefaultResultIsOk)
{
    const TransportResult result;
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(result.status, TransportStatus::Ok);
    EXPECT_TRUE(result.message.empty());
}

/** @brief Tests that peer ids compare by value and the send statuses differ. */
TEST(Medium_Transport, PeersCompareByValue)
{
    EXPECT_EQ(PeerID{1}, PeerID{1});
    EXPECT_NE(PeerID{1}, PeerID{2});
    EXPECT_NE(SendStatus::Queued, SendStatus::Stopped);
    EXPECT_NE(SendStatus::QueueFull, SendStatus::UnknownPeer);
}
