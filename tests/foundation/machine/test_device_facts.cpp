/**************************************************************************/
/* test_device_facts.cpp                                                  */
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
 * @file test_device_facts.cpp
 * @brief Tests DeviceFacts: the values a record holds before a device fills it.
 * @author Mustafa Garip
 * @see tests/foundation/machine/README.md
 */

#include <gtest/gtest.h>

#include <SushiRuntime/machine/device_facts.hpp>
#include <SushiRuntime/machine/device_kind.hpp>

using namespace SushiRuntime;

/** @brief Tests the defaults of a record. */
TEST(Small_DeviceFacts, DefaultsDescribeNoDevice)
{
    const DeviceFacts facts;
    EXPECT_EQ(facts.kind, DeviceKind::Other);
    EXPECT_TRUE(facts.name.empty());
    EXPECT_EQ(facts.memory_bytes, 0u);
}
