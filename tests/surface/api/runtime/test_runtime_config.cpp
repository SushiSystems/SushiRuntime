/**************************************************************************/
/* test_runtime_config.cpp                                                */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_runtime_config.cpp
 * @brief Tests the worker, core and device choices a RuntimeConfig makes.
 * @see tests/surface/api/runtime/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Runs a small graph on @p rt and checks every element of its output. */
    void expect_runs_correctly(Runtime& rt)
    {
        constexpr std::size_t N = 512;

        auto buf = rt.buffer<int>(N).value();
        TestSupport::fill_with(buf, 0);
        int* p = buf.data();

        auto g = rt.graph();
        g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
        g.run().value();

        EXPECT_EQ(TestSupport::contents(buf), TestSupport::ramp<int>(N, 1));
    }

    /** @brief Returns the logical cores of the machine, at least one. */
    std::size_t machine_cores()
    {
        const std::size_t cores = std::thread::hardware_concurrency();
        return cores == 0 ? 1u : cores;
    }

    /** @brief Returns whether at least one SYCL device on this lane reports fp64. */
    bool any_device_has_fp64()
    {
        const auto devices = sycl::device::get_devices(sycl::info::device_type::all);
        return std::any_of(devices.begin(), devices.end(),
                           [](const sycl::device& dev) { return dev.has(sycl::aspect::fp64); });
    }

    /** @brief Returns whether the machine has a SYCL device of class @p kind. */
    bool machine_has_device_of(DeviceKind kind)
    {
        const auto devices = sycl::device::get_devices(sycl::info::device_type::all);
        return std::any_of(devices.begin(), devices.end(), [kind](const sycl::device& dev)
        {
            switch (kind)
            {
                case DeviceKind::CPU:         return dev.is_cpu();
                case DeviceKind::GPU:         return dev.is_gpu();
                case DeviceKind::Accelerator: return dev.is_accelerator();
                case DeviceKind::Other:       return false;
            }
            return false;
        });
    }

    /** @brief Returns whether @p devices lists a device of class @p kind. */
    bool lists_device_of(const std::vector<DeviceDescription>& devices, DeviceKind kind)
    {
        return std::any_of(devices.begin(), devices.end(),
                           [kind](const DeviceDescription& device) { return device.kind == kind; });
    }

    /** @brief Returns the config whose device filter admits no device class. */
    RuntimeConfig config_admitting_nothing()
    {
        RuntimeConfig config;
        config.devices.include_cpu = false;
        config.devices.include_gpu = false;
        config.devices.include_accelerator = false;
        return config;
    }
} // namespace

/** @brief Fixture of the pool tests that read the program's default runtime. */
class Medium_CoTenancy : public TestSupport::RuntimeTest
{
};

/** @brief Tests that the default pool is one worker per logical core, starting at core 0. */
TEST_F(Medium_CoTenancy, DefaultsToOneWorkerPerCoreFromCoreZero)
{
    const auto pool = runtime().advanced().worker_pool();

    EXPECT_EQ(pool.count, machine_cores());
    EXPECT_EQ(pool.first_core, 0u);
    EXPECT_EQ(pool.pinning, PinPolicy::Pin);
}

/** @brief Fixture of the test that reserves cores, which needs at least four of them. */
class Medium_CoTenancyReservation : public ::testing::Test
{
    protected:
        /** @brief Skips the test on a machine with fewer than four logical cores. */
        void SetUp() override
        {
            if (machine_cores() < 4)
                GTEST_SKIP() << "needs at least 4 logical cores to reserve a range";
        }
};

/** @brief Tests that the pool takes exactly the worker count and core placement the host asked for. */
TEST_F(Medium_CoTenancyReservation, ReservesTheCoresTheHostAskedFor)
{
    const std::size_t cores = machine_cores();
    const std::size_t reserved = 2;
    RuntimeConfig config;
    config.worker_count = cores - reserved;
    config.first_core   = reserved;

    auto rt = Runtime::create(config).value();
    const auto pool = rt.advanced().worker_pool();

    EXPECT_EQ(pool.count, cores - reserved);
    EXPECT_EQ(pool.first_core, reserved);
}

/** @brief Tests that a single worker still executes the whole graph. */
TEST_F(Medium_CoTenancy, ASingleWorkerStillExecutesTheWholeGraph)
{
    RuntimeConfig config;
    config.worker_count = 1;
    auto rt = Runtime::create(config).value();
    EXPECT_EQ(rt.advanced().worker_pool().count, 1u);

    expect_runs_correctly(rt);
}

/** @brief Fixture of the default filter test; the parameter is the device class, and a row the machine has no device of skips. */
class Medium_DefaultDeviceFilter : public TestSupport::RuntimeTest,
                                   public ::testing::WithParamInterface<DeviceKind>
{
    protected:
        /** @brief Skips the row when the machine has no device of the parameter's class. */
        void SetUp() override
        {
            if (!machine_has_device_of(GetParam()))
                GTEST_SKIP() << "the machine has no device of this class";
        }
};

/** @brief Tests that a default runtime drives a device of every class the machine has. */
TEST_P(Medium_DefaultDeviceFilter, ARuntimeWithNoConfigDrivesADeviceOfEveryClassTheMachineHas)
{
    EXPECT_TRUE(lists_device_of(runtime().advanced().devices(), GetParam()));
}

INSTANTIATE_TEST_SUITE_P(Classes, Medium_DefaultDeviceFilter,
                         ::testing::Values(DeviceKind::CPU, DeviceKind::GPU, DeviceKind::Accelerator),
                         [](const ::testing::TestParamInfo<DeviceKind>& info)
                         {
                             switch (info.param)
                             {
                                 case DeviceKind::CPU:         return std::string("CPU");
                                 case DeviceKind::GPU:         return std::string("GPU");
                                 case DeviceKind::Accelerator: return std::string("Accelerator");
                                 case DeviceKind::Other:       return std::string("Other");
                             }
                             return std::string("Unknown");
                         });

/** @brief Fixture of the fp64 filter tests that need a runtime built with require_fp64 over a lane that has an fp64 device. */
class Medium_RequireFp64 : public ::testing::Test
{
    protected:
        /** @brief Skips on a lane with no fp64 device and builds the filtered runtime otherwise. */
        void SetUp() override
        {
            if (!any_device_has_fp64())
                GTEST_SKIP() << "no device of this lane reports fp64";
            RuntimeConfig config;
            config.devices.require_fp64 = true;
            filtered_.emplace(Runtime::create(config).value());
        }

        /** @brief Drops the runtime. */
        void TearDown() override { filtered_.reset(); }

        /** @brief Returns the runtime built with require_fp64. */
        Runtime& filtered() { return *filtered_; }

    private:
        /** @brief The runtime built with require_fp64. */
        std::optional<Runtime> filtered_;
};

/** @brief Fixture of the fp64 filter tests; the parameter is a device index, and a row past the machine's last device skips. */
class Medium_RequireFp64Device : public Medium_RequireFp64,
                                 public ::testing::WithParamInterface<std::size_t>
{
    protected:
        /** @brief Skips the row when the filtered runtime has no device at the parameter's index. */
        void SetUp() override
        {
            Medium_RequireFp64::SetUp();
            if (IsSkipped())
                return;
            if (GetParam() >= filtered().advanced().device_count())
                GTEST_SKIP() << "the runtime drives no device " << GetParam();
        }
};

/** @brief Tests that the device the require_fp64 filter admits at an index reports fp64. */
TEST_P(Medium_RequireFp64Device, RequireFp64AdmitsOnlyFp64Devices)
{
    const sycl::device device = filtered().advanced().queue(DeviceIndex{GetParam()}).get_device();

    EXPECT_TRUE(device.has(sycl::aspect::fp64));
}

INSTANTIATE_TEST_SUITE_P(Devices, Medium_RequireFp64Device, ::testing::Range<std::size_t>(0, 4));

/** @brief Tests that a runtime with require_fp64 constructs on a lane that has an fp64 device. */
TEST_F(Medium_RequireFp64, StillConstructsOnAConformingLane)
{
    EXPECT_GE(filtered().advanced().device_count(), 1u);
}

/** @brief Fixture of the test of require_fp64 on a lane that has no fp64 device. */
class Medium_RequireFp64Unmet : public ::testing::Test
{
    protected:
        /** @brief Skips on a lane that has an fp64 device, since nothing is refused there. */
        void SetUp() override
        {
            if (any_device_has_fp64())
                GTEST_SKIP() << "a device of this lane reports fp64";
        }
};

/** @brief Tests that require_fp64 on a lane with no fp64 device fails with Unsupported. */
TEST_F(Medium_RequireFp64Unmet, IsRefusedWithUnsupported)
{
    RuntimeConfig config;
    config.devices.require_fp64 = true;

    const auto refused = Runtime::create(config);

    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::Unsupported);
}

/** @brief Tests that a filter that admits nothing fails at construction with Unsupported and names the filter. */
TEST(Medium_DeviceFilterEmpty, AnEmptyFilterFailsWithUnsupported)
{
    const auto refused = Runtime::create(config_admitting_nothing());

    ASSERT_FALSE(refused.has_value()) << "a filter admitting no device class must not produce a runtime";
    EXPECT_EQ(refused.error().code, ErrorCode::Unsupported);
    const std::string& message = refused.error().message;
    EXPECT_NE(message.find("cpu=no"), std::string::npos) << message;
    EXPECT_NE(message.find("gpu=no"), std::string::npos) << message;
}
