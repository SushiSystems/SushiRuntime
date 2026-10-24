/**************************************************************************/
/* test_node_facts.cpp                                                    */
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
 * @file test_node_facts.cpp
 * @brief Tests NodeFacts: the values a record holds before a runtime fills it.
 * @author Mustafa Garip
 * @see tests/foundation/machine/README.md
 */

#include <gtest/gtest.h>

#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/machine/operating_system.hpp>

using namespace SushiRuntime;

/** @brief Tests the defaults of a record. */
TEST(Small_NodeFacts, DefaultsDescribeNoMachine)
{
    const NodeFacts facts;
    EXPECT_TRUE(facts.name.empty());
    EXPECT_EQ(facts.operating_system, OperatingSystem::Unknown);
    EXPECT_TRUE(facts.labels.empty());
    EXPECT_TRUE(facts.devices.empty());
}

/** @brief Tests that a copy keeps the labels in order and every device. */
TEST(Small_NodeFacts, ACopyKeepsTheLabelsInOrderAndTheDevices)
{
    NodeFacts facts;
    facts.labels = {NodeLabel{"zone", "a"}, NodeLabel{"rack", "b"}};
    facts.devices.push_back(DeviceFacts{DeviceKind::GPU, "card", 1024u});

    const NodeFacts copy = facts;
    ASSERT_EQ(copy.labels.size(), 2u);
    EXPECT_EQ(copy.labels[0].key, "zone");
    EXPECT_EQ(copy.labels[1].key, "rack");
    ASSERT_EQ(copy.devices.size(), 1u);
    EXPECT_EQ(copy.devices[0].kind, DeviceKind::GPU);
    EXPECT_EQ(copy.devices[0].name, "card");
    EXPECT_EQ(copy.devices[0].memory_bytes, 1024u);
}
