/**************************************************************************/
/* test_device_resident.cpp                                               */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// WP-6 coverage: device-resident state. A buffer/state allocated with
// Residency::Device stays on its device and is not host-addressable; the host
// seeds and reads it back through write_range()/read_range() while kernels run
// against it exactly as for shared USM.

#include <cstddef>
#include <numeric>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

// A device-resident buffer reports its residency and is not host-accessible; a
// host seed, a kernel, and a host readback round-trip correctly without ever
// host-indexing the allocation.
TEST(Integration_DeviceResident, BufferRoundTrip)
{
    constexpr std::size_t N = 128;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N, 0, Residency::Device);

    EXPECT_EQ(buf.residency(), Residency::Device);
    EXPECT_FALSE(buf.host_accessible());

    std::vector<int> seed(N);
    std::iota(seed.begin(), seed.end(), 0);     // 0,1,2,...
    buf.write_range(0, seed.data(), N);

    int* p = buf.data();
    auto g = rt.graph();
    g.add(API::Reads(buf), API::Writes(buf), N, [p](std::size_t i) { p[i] *= 2; });
    g.run();

    const std::vector<int> out = buf.read_range(0, N);
    ASSERT_EQ(out.size(), N);
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(out[i], static_cast<int>(i) * 2) << "at i=" << i;
}

// read_range can pull a window without copying the whole field.
TEST(Integration_DeviceResident, SelectiveReadback)
{
    constexpr std::size_t N = 64;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N, 0, Residency::Device);

    std::vector<int> seed(N);
    std::iota(seed.begin(), seed.end(), 100);   // 100,101,...
    buf.write_range(0, seed.data(), N);

    const std::vector<int> window = buf.read_range(8, 4);
    ASSERT_EQ(window.size(), 4u);
    for (std::size_t k = 0; k < 4; ++k)
        EXPECT_EQ(window[k], static_cast<int>(108 + k));
}

// A device-resident State steps with the fluent add(state, fn) rule and is
// seeded / read back through its range API. The field stays resident across all
// steps; only the windowed readback touches the host.
TEST(Integration_DeviceResident, StateStepsResident)
{
    constexpr std::size_t N     = 96;
    constexpr std::size_t STEPS = 5;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(N, 0, Residency::Device);

    EXPECT_EQ(field.residency(), Residency::Device);
    EXPECT_FALSE(field.host_accessible());

    std::vector<int> seed(N, 0);
    field.write_range(0, seed.data(), N);

    auto g = rt.graph();
    g.add(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });
    g.run(STEPS);

    const std::vector<int> out = field.read_range(0, N);
    ASSERT_EQ(out.size(), N);
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(out[i], static_cast<int>(STEPS)) << "at i=" << i;
}

// Shared residency stays the default and is host-addressable; its range API
// works alongside direct indexing, so existing code is unaffected.
TEST(Integration_DeviceResident, SharedDefaultUnchanged)
{
    constexpr std::size_t N = 32;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);

    EXPECT_EQ(buf.residency(), Residency::Shared);
    EXPECT_TRUE(buf.host_accessible());

    for (std::size_t i = 0; i < N; ++i)
        buf[i] = static_cast<int>(i);           // direct host indexing still valid

    const std::vector<int> out = buf.read_range(0, N);
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_EQ(out[i], static_cast<int>(i));
}
