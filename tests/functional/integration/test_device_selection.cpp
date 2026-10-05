/****************************************************************/
/* test_device_selection.cpp                                    */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// Coverage for what the hardware filter selects and how it fails.
//
// Two properties, both about moving a failure earlier. A double-precision
// kernel on a device without sycl::aspect::fp64 fails inside the driver, at
// launch, with a message naming neither the device nor the aspect; require_fp64
// moves that to discovery. And a filter that matches nothing used to surface as
// "Queue index out of bounds" from the first allocator call, which is the first
// thing a player on a machine with no SYCL device would see.

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    // True when at least one SYCL device on this lane reports fp64, which decides
    // whether a require_fp64 runtime is expected to construct or to fail here.
    bool any_device_has_fp64()
    {
        for (const auto& dev : sycl::device::get_devices(sycl::info::device_type::all))
            if (dev.has(sycl::aspect::fp64))
                return true;
        return false;
    }
}

// Every device the filter admits reports fp64. Asserted against the discovery
// layer directly so the property is checked on the list itself, not inferred
// from a count that a deduplication step could also explain.
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

// The filter is a filter, not a rejection of the whole machine: a lane that has
// an fp64 device still builds a runtime with require_fp64 set. The CPU lanes
// this suite runs on (POCL, the Intel OpenCL CPU runtime) all report fp64, so
// this is the branch normally taken.
TEST(Integration_DeviceSelection, RequireFp64StillConstructsOnAConformingLane)
{
    Topology::HardwareFilterConfig config;
    config.require_fp64 = true;

    if (any_device_has_fp64())
    {
        auto rt = Runtime::create(config);
        EXPECT_GE(rt.advanced().device_count(), 1u);
    }
    else
    {
        // No conforming device: the failure must still be the precise one below,
        // not a driver error from inside a kernel launch.
        EXPECT_THROW(Runtime::create(config), SushiException);
    }
}

// A filter that admits nothing fails at construction with a code a caller can
// branch on. This is the same shape as a machine with no SYCL device at all,
// which is why the error carries Errc::unsupported rather than a bare
// out-of-bounds message from the first queue lookup.
TEST(Integration_DeviceSelection, AnEmptyFilterFailsWithUnsupported)
{
    Topology::HardwareFilterConfig config;
    config.include_cpu         = false;
    config.include_gpu         = false;
    config.include_accelerator = false;

    try
    {
        Runtime::create(config);
        FAIL() << "a filter admitting no device class must not produce a runtime";
    }
    catch (const SushiException& e)
    {
        EXPECT_EQ(e.error().code, Core::Errc::unsupported);
        // The message must name the filter, so the reader can tell "this machine
        // has nothing" from "you excluded everything".
        const std::string message = e.what();
        EXPECT_NE(message.find("cpu=no"), std::string::npos) << message;
        EXPECT_NE(message.find("gpu=no"), std::string::npos) << message;
    }
}

// The default filter admits everything, so the common path is unaffected by the
// check above — a plain Runtime::create() still finds this lane's devices.
TEST(Integration_DeviceSelection, DefaultFilterIsUnaffected)
{
    auto rt = Runtime::create();
    EXPECT_GE(rt.advanced().device_count(), 1u);
}
