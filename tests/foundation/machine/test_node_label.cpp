/**************************************************************************/
/* test_node_label.cpp                                                    */
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
 * @file test_node_label.cpp
 * @brief Tests NodeLabel: a key and a value that copy as plain strings.
 * @author Mustafa Garip
 * @see tests/foundation/machine/README.md
 */

#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/machine/node_label.hpp>

using namespace SushiRuntime;

/** @brief Tests that a default label holds two empty strings. */
TEST(Small_NodeLabel, DefaultsHoldAnEmptyKeyAndValue)
{
    const NodeLabel label;
    EXPECT_TRUE(label.key.empty());
    EXPECT_TRUE(label.value.empty());
}

/** @brief Tests that a copy keeps the key and the value. */
TEST(Small_NodeLabel, ACopyKeepsTheKeyAndTheValue)
{
    const NodeLabel label{"zone", "north"};
    const NodeLabel copy = label;
    EXPECT_EQ(copy.key, "zone");
    EXPECT_EQ(copy.value, "north");
}
