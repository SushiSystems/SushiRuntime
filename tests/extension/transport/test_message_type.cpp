/**************************************************************************/
/* test_message_type.cpp                                                  */
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
 * @file test_message_type.cpp
 * @brief Tests MessageType: sixteen types from 1, their names, and the bytes that name none.
 * @see tests/extension/transport/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include <SushiRuntime/transport/message_type.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that the types run from 1 to 16 and only those bytes name a type. */
TEST(Unit_MessageType, SixteenTypesFromOne)
{
    static_assert(static_cast<int>(MessageType::Hello) == K_FIRST_MESSAGE_TYPE, "the first type is 1");
    static_assert(static_cast<int>(MessageType::Heartbeat) == K_LAST_MESSAGE_TYPE, "the last type is 16");
    static_assert(K_MESSAGE_TYPE_COUNT == 16, "sixteen messages");
    EXPECT_FALSE(is_message_type(0));
    EXPECT_TRUE(is_message_type(1));
    EXPECT_TRUE(is_message_type(16));
    EXPECT_FALSE(is_message_type(17));
}

/** @brief Tests the name of a type and the name of a byte that names none. */
TEST(Unit_MessageType, NamesAreTheWireNames)
{
    EXPECT_STREQ(message_type_name(MessageType::Hello), "HELLO");
    EXPECT_STREQ(message_type_name(MessageType::TaskDispatch), "TASK_DISPATCH");
    EXPECT_STREQ(message_type_name(MessageType::Heartbeat), "HEARTBEAT");
    EXPECT_STREQ(message_type_name(static_cast<MessageType>(0)), "UNKNOWN");
    EXPECT_STREQ(message_type_name(static_cast<MessageType>(200)), "UNKNOWN");
}
