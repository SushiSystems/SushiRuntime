/**************************************************************************/
/* test_device_selection.cpp                                              */
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
 * @file test_device_selection.cpp
 * @brief Tests what the hardware filter selects and how an empty selection fails.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Returns whether at least one SYCL device on this lane reports fp64. */
    bool any_device_has_fp64()
    {
        for (const auto& dev : sycl::device::get_devices(sycl::info::device_type::all))
            if (dev.has(sycl::aspect::fp64))
                return true;
        return false;
    }
}

/** @brief Tests that every device the require_fp64 filter admits reports fp64. */
TEST(Integration_DeviceSelection, RequireFp64AdmitsOnlyFp64Devices)
{
    DeviceFilter config;
    config.require_fp64 = true;

    Internal::Topology::HardwareDiscovery discovery;
    const std::vector<Internal::Topology::HardwareNode> nodes = discovery.probe_all_devices(config).value();

    for (const auto& node : nodes)
        EXPECT_TRUE(node.sycl_dev.has(sycl::aspect::fp64))
            << "device [" << node.name << "] was admitted without fp64 support";
}

/**
 * @brief Tests that a runtime with require_fp64 still constructs on a lane that has an fp64 device.
 */
TEST(Integration_DeviceSelection, RequireFp64StillConstructsOnAConformingLane)
{
    RuntimeConfig config;
    config.devices.require_fp64 = true;

    if (any_device_has_fp64())
    {
        auto rt = Runtime::create(config).value();
        EXPECT_GE(rt.advanced().device_count(), 1u);
    }
    else
    {
        // With no conforming device the failure is still the Error below; see README.md.
        const auto refused = Runtime::create(config);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, ErrorCode::Unsupported);
    }
}

/** @brief Tests that a filter that admits nothing fails at construction with Unsupported. */
TEST(Integration_DeviceSelection, AnEmptyFilterFailsWithUnsupported)
{
    RuntimeConfig config;
    config.devices.include_cpu         = false;
    config.devices.include_gpu         = false;
    config.devices.include_accelerator = false;

    const auto refused = Runtime::create(config);
    ASSERT_FALSE(refused.has_value())
        << "a filter admitting no device class must not produce a runtime";
    EXPECT_EQ(refused.error().code, ErrorCode::Unsupported);
    // The message names the filter; see README.md.
    const std::string& message = refused.error().message;
    EXPECT_NE(message.find("cpu=no"), std::string::npos) << message;
    EXPECT_NE(message.find("gpu=no"), std::string::npos) << message;
}

/** @brief Tests that the default filter still finds this lane's devices. */
TEST(Integration_DeviceSelection, DefaultFilterIsUnaffected)
{
    auto rt = Runtime::create().value();
    EXPECT_GE(rt.advanced().device_count(), 1u);
}
