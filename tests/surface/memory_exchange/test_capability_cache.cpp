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
 * @file test_capability_cache.cpp
 * @brief Tests CapabilityCache: one probe per device, and nothing kept from a probe that throws.
 * @see tests/surface/memory_exchange/README.md
 * @author Mustafa Garip
 */

#include <functional>
#include <stdexcept>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/interop/interop_capabilities.hpp>
#include <SushiRuntime/memory_exchange/capability_cache.hpp>
#include <SushiRuntime/memory_exchange/scripted_capability_probe.hpp>

using SushiRuntime::InteropCapabilities;
using SushiRuntime::Internal::MemoryExchange::CapabilityCache;
using SushiRuntime::Internal::MemoryExchange::ScriptedCapabilityProbe;

/** @brief Tests that a device is probed once over two queries, and both queries return its answer. */
TEST(Medium_CapabilityCache, EachDeviceIsProbedOnce)
{
    const sycl::device device;
    ScriptedCapabilityProbe probe;
    InteropCapabilities answer;
    answer.host_export = true;
    probe.answer_next(answer);
    CapabilityCache cache(std::ref(probe));

    const InteropCapabilities first = cache.get(device);
    const InteropCapabilities second = cache.get(device);

    EXPECT_EQ(probe.calls(), 1u);
    EXPECT_TRUE(first.host_export);
    EXPECT_TRUE(second.host_export);
}

/** @brief Tests that a probe that throws passes the throw on and keeps no answer. */
TEST(Medium_CapabilityCache, AThrowingProbeKeepsNothing)
{
    const sycl::device device;
    ScriptedCapabilityProbe probe;
    InteropCapabilities answer;
    answer.host_export = true;
    probe.throw_next("probe failed");
    probe.answer_next(answer);
    CapabilityCache cache(std::ref(probe));

    EXPECT_THROW(static_cast<void>(cache.get(device)), std::runtime_error);
    const InteropCapabilities retried = cache.get(device);

    EXPECT_TRUE(retried.host_export);
    EXPECT_EQ(probe.calls(), 2u);
}
