/**************************************************************************/
/* test_operation_id.cpp                                                  */
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
 * @file test_operation_id.cpp
 * @brief Tests OperationID: the FNV-1a hash, the literal, naming and ordering.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/core/operation_id.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Literals;

/** @brief Tests the hash against the FNV-1a offset basis and a published value. */
TEST(Small_OperationID, HashIsFNV1a)
{
    static_assert(OperationID::hash("", 0) == 2166136261u, "the empty name hashes to the offset basis");
    EXPECT_EQ(OperationID::hash("a", 1), 0xE40C292Cu);
}

/** @brief Tests that the literal hashes at compile time and equals the explicit form. */
TEST(Small_OperationID, LiteralMatchesTheExplicitHash)
{
    constexpr OperationID literal = "vec.double"_op;
    static_assert(literal.is_named(), "a literal names an operation");
    EXPECT_EQ(literal, OperationID(OperationID::hash("vec.double", 10)));
    EXPECT_NE(literal, "vec.triple"_op);
}

/** @brief Tests that the default names nothing and ordering follows the value. */
TEST(Small_OperationID, DefaultIsUnnamedAndOrderFollowsTheValue)
{
    constexpr OperationID none;
    EXPECT_FALSE(none.is_named());
    EXPECT_EQ(none.value, OperationID::K_NONE);
    EXPECT_TRUE(OperationID(1) < OperationID(2));
    EXPECT_FALSE(OperationID(2) < OperationID(2));
}
