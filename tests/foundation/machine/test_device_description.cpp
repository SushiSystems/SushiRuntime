/**************************************************************************/
/* test_device_description.cpp                                            */
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
 * @file test_device_description.cpp
 * @brief Tests DeviceDescription: the values a description holds before discovery fills it.
 * @see tests/foundation/machine/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/machine/device_description.hpp>
#include <SushiRuntime/machine/device_kind.hpp>

using namespace SushiRuntime;

/** @brief Tests the defaults of a description. */
TEST(Small_DeviceDescription, DefaultsDescribeNoDevice)
{
    const DeviceDescription description;
    EXPECT_EQ(description.index, 0u);
    EXPECT_TRUE(description.name.empty());
    EXPECT_EQ(description.kind, DeviceKind::Other);
    EXPECT_EQ(description.numa_node, 0);
    EXPECT_EQ(description.memory_bytes, 0u);
    EXPECT_EQ(description.compute_units, 0u);
}
