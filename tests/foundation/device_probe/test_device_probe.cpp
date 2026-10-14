/**************************************************************************/
/* test_device_probe.cpp                                                  */
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
 * @file test_device_probe.cpp
 * @brief Tests has_any_sycl_device() against the device list SYCL reports.
 * @see tests/foundation/device_probe/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/device_probe/device_probe.hpp>

using namespace SushiRuntime::Test;

/** @brief Tests that the probe agrees with the device list. */
TEST(Unit_DeviceProbe, AgreesWithTheDeviceList)
{
    EXPECT_EQ(has_any_sycl_device(), !sycl::device::get_devices().empty());
}
