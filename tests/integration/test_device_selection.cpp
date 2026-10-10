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

#include <SushiRuntime/SushiRuntime.h>

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
    Topology::HardwareFilterConfig config;
    config.require_fp64 = true;

    Topology::HardwareDiscovery discovery;
    const std::vector<Topology::HardwareNode> nodes = discovery.probe_all_devices(config);

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
        auto rt = Runtime::create(config);
        EXPECT_GE(rt.advanced().device_count(), 1u);
    }
    else
    {
        // With no conforming device the failure is still the SushiException below; see README.md.
        EXPECT_THROW(static_cast<void>(Runtime::create(config)), SushiException);
    }
}

/** @brief Tests that a filter that admits nothing fails at construction with Errc::unsupported. */
TEST(Integration_DeviceSelection, AnEmptyFilterFailsWithUnsupported)
{
    RuntimeConfig config;
    config.devices.include_cpu         = false;
    config.devices.include_gpu         = false;
    config.devices.include_accelerator = false;

    try
    {
        static_cast<void>(Runtime::create(config));
        FAIL() << "a filter admitting no device class must not produce a runtime";
    }
    catch (const SushiException& e)
    {
        EXPECT_EQ(e.error().code, Core::Errc::unsupported);
        // The message names the filter; see README.md.
        const std::string message = e.what();
        EXPECT_NE(message.find("cpu=no"), std::string::npos) << message;
        EXPECT_NE(message.find("gpu=no"), std::string::npos) << message;
    }
}

/** @brief Tests that the default filter still finds this lane's devices. */
TEST(Integration_DeviceSelection, DefaultFilterIsUnaffected)
{
    auto rt = Runtime::create();
    EXPECT_GE(rt.advanced().device_count(), 1u);
}
