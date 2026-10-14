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
 * @file test_runtime_config.cpp
 * @brief Tests the worker, core and device choices a RuntimeConfig makes.
 * @see tests/surface/api/runtime/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

using namespace SushiRuntime;

/** @brief Tests the worker count, core range and pinning policy a host sets to share its machine. */
namespace CoTenancyTests
{
    namespace
    {
        /** @brief Runs a small graph and checks every element of its output. */
        void expect_runs_correctly(Runtime& rt)
        {
            constexpr std::size_t N = 512;

            auto buf = rt.buffer<int>(N).value();
            for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
            int* p = buf.data();

            auto g = rt.graph();
            g.add(N, Reads{}, Writes(buf),
                  [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
            g.run().value();

            for (std::size_t i = 0; i < N; ++i)
                ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "at i=" << i;
        }

        std::size_t machine_cores()
        {
            const std::size_t cores = std::thread::hardware_concurrency();
            return cores == 0 ? 1u : cores;
        }
    }

    /** @brief Tests that the default pool is one worker per logical core, starting at core 0. */
    TEST(Integration_CoTenancy, DefaultsToOneWorkerPerCoreFromCoreZero)
    {
        auto rt = Runtime::create().value();
        const auto pool = rt.advanced().worker_pool();

        EXPECT_EQ(pool.count, machine_cores());
        EXPECT_EQ(pool.first_core, 0u);
        EXPECT_EQ(pool.pinning, PinPolicy::Pin);
    }

    /**
     * @brief Tests that the pool takes exactly the worker count and core placement the host asked for.
     */
    TEST(Integration_CoTenancy, ReservesTheCoresTheHostAskedFor)
    {
        const std::size_t cores = machine_cores();
        if (cores < 4)
            GTEST_SKIP() << "needs at least 4 logical cores to reserve a range";

        const std::size_t reserved = 2;

        RuntimeConfig config;
        config.worker_count = cores - reserved;
        config.first_core   = reserved;

        auto rt = Runtime::create(config).value();
        const auto pool = rt.advanced().worker_pool();

        EXPECT_EQ(pool.count, cores - reserved);
        EXPECT_EQ(pool.first_core, reserved);

        expect_runs_correctly(rt);
    }

    /** @brief Tests that a single worker still executes the whole graph. */
    TEST(Integration_CoTenancy, ASingleWorkerStillExecutesTheWholeGraph)
    {
        RuntimeConfig config;
        config.worker_count = 1;

        auto rt = Runtime::create(config).value();
        EXPECT_EQ(rt.advanced().worker_pool().count, 1u);

        expect_runs_correctly(rt);
    }

    /** @brief Tests that the Float policy keeps the requested pool size and binds no worker. */
    TEST(Integration_CoTenancy, FloatPolicyRunsUnpinned)
    {
        RuntimeConfig config;
        config.worker_count = 2;
        config.pinning      = PinPolicy::Float;

        auto rt = Runtime::create(config).value();
        const auto pool = rt.advanced().worker_pool();

        EXPECT_EQ(pool.count, 2u);
        EXPECT_EQ(pool.pinning, PinPolicy::Float);

        expect_runs_correctly(rt);
    }

    /** @brief Tests that a core range past the end of the machine is clamped. */
    TEST(Integration_CoTenancy, AnOutOfRangeCoreRangeIsClampedNotIgnored)
    {
        const std::size_t cores = machine_cores();

        RuntimeConfig config;
        config.worker_count = 4;
        config.first_core   = cores + 8;   // entirely past the last core

        auto rt = Runtime::create(config).value();
        const auto pool = rt.advanced().worker_pool();

        EXPECT_LE(pool.first_core + pool.count, cores)
            << "the worker core range must fit inside the machine";
        EXPECT_GE(pool.count, 1u) << "clamping must not leave an empty pool";

        expect_runs_correctly(rt);
    }
} // namespace CoTenancyTests

/** @brief Tests what the hardware filter selects and how an empty selection fails. */
namespace DeviceSelectionTests
{
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
} // namespace DeviceSelectionTests

/** @brief Tests the code a caller of the library reads from a failed factory. */
namespace ErrorTypesTests
{
    namespace
    {
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

    /** @brief Tests that a filter excluding every device returns Unsupported and throws nothing. */
    TEST(Integration_ErrorTypes, AFilterThatExcludesEveryDeviceReturnsUnsupported)
    {
        EXPECT_NO_THROW({
            const auto refused = Runtime::create(config_admitting_nothing());
            ASSERT_FALSE(refused.has_value()) << "a runtime was built over no device";
            EXPECT_EQ(refused.error().code, ErrorCode::Unsupported);
            EXPECT_FALSE(refused.error().message.empty());
        });
    }
} // namespace ErrorTypesTests
