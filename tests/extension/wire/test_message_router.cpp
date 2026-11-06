/**************************************************************************/
/* test_message_router.cpp                                                */
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
 * @file test_message_router.cpp
 * @brief Tests MessageRouter: delivery by type and the types it has no handler for.
 * @see tests/extension/wire/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/transport/message_type.hpp>

#include <SushiRuntime/wire/message_router.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using namespace SushiRuntime::Distributed::Wire;

/** @brief Tests that the router calls the handler of the type and reports the rest. */
TEST(Small_WireFormat, RouterDeliversByType)
{
    MessageRouter<int&> router;
    router.set_handler(MessageType::Ping, [](int& calls) { ++calls; });

    int calls = 0;
    EXPECT_TRUE(router.has_handler(MessageType::Ping));
    EXPECT_FALSE(router.has_handler(MessageType::Pong));
    EXPECT_EQ(router.route(MessageType::Ping, calls), RouteStatus::Delivered);
    EXPECT_EQ(router.route(MessageType::Pong, calls), RouteStatus::Unhandled);
    EXPECT_EQ(router.route(static_cast<MessageType>(0), calls), RouteStatus::Unhandled);
    EXPECT_EQ(calls, 1);
}
