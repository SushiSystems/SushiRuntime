/**************************************************************************/
/* test_capability_cache.cpp                                              */
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
 * @file test_capability_cache.cpp
 * @brief Tests CapabilityCache: one probe per device, and nothing kept from a probe that throws.
 * @see tests/surface/memory_exchange/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/interop/external_handle_kind_set.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>
#include <SushiRuntime/memory_exchange/capability_cache.hpp>

using SushiRuntime::ExternalHandleKind;
using SushiRuntime::InteropCapabilities;
using SushiRuntime::Internal::MemoryExchange::CapabilityCache;

/** @brief Tests that the second query of a device returns the kept answer unprobed. */
TEST(Medium_CapabilityCache, TheSecondQueryHitsTheCache)
{
    const sycl::device device;
    std::size_t probes = 0;
    InteropCapabilities answer;
    answer.host_export = true;
    answer.importable_kinds.insert(ExternalHandleKind::OpaqueFd);
    CapabilityCache cache(
        [&probes, answer](const sycl::device&)
        {
            ++probes;
            return answer;
        });

    const InteropCapabilities first = cache.get(device);
    const InteropCapabilities second = cache.get(device);

    EXPECT_EQ(probes, 1u);
    EXPECT_TRUE(second.host_export);
    EXPECT_EQ(second.importable_kinds, first.importable_kinds);
    EXPECT_TRUE(second.importable_kinds.contains(ExternalHandleKind::OpaqueFd));
}

/** @brief Tests that each distinct device is probed once and keeps its own answer. */
TEST(Medium_CapabilityCache, EachDeviceIsProbedOnce)
{
    const std::vector<sycl::device> devices = sycl::device::get_devices();
    if (devices.size() < 2)
        GTEST_SKIP() << "the machine exposes fewer than two SYCL devices";

    std::vector<sycl::device> probed;
    CapabilityCache cache(
        [&probed, &devices](const sycl::device& device)
        {
            probed.push_back(device);
            InteropCapabilities answer;
            answer.host_export = device == devices[0];
            return answer;
        });

    EXPECT_TRUE(cache.get(devices[0]).host_export);
    EXPECT_FALSE(cache.get(devices[1]).host_export);
    EXPECT_TRUE(cache.get(devices[0]).host_export);
    ASSERT_EQ(probed.size(), 2u);
    EXPECT_TRUE(probed[0] == devices[0]);
    EXPECT_TRUE(probed[1] == devices[1]);
}

/** @brief Tests that a probe that throws passes the throw on and keeps no answer. */
TEST(Medium_CapabilityCache, AThrowingProbeKeepsNothing)
{
    const sycl::device device;
    std::size_t probes = 0;
    CapabilityCache cache(
        [&probes](const sycl::device&) -> InteropCapabilities
        {
            ++probes;
            if (probes == 1)
                throw std::runtime_error("probe failed");
            InteropCapabilities answer;
            answer.host_export = true;
            return answer;
        });

    EXPECT_THROW(static_cast<void>(cache.get(device)), std::runtime_error);
    EXPECT_TRUE(cache.get(device).host_export) << "the failed probe left an answer behind";
    EXPECT_EQ(probes, 2u);
}
