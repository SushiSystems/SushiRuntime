/**************************************************************************/
/* test_device_resident.cpp                                               */
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
 * @file test_device_resident.cpp
 * @brief Tests buffers and States allocated with Residency::Device, Shared and Host.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <numeric>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

/**
 * @brief Tests that a device-resident buffer round-trips a host seed, a kernel and a host readback.
 */
TEST(Integration_DeviceResident, BufferRoundTrip)
{
    constexpr std::size_t N = 128;

    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Device).value();

    EXPECT_EQ(buf.residency(), Residency::Device);
    EXPECT_FALSE(buf.is_host_accessible());

    std::vector<int> seed(N);
    std::iota(seed.begin(), seed.end(), 0);     // 0,1,2,...
    buf.write_range({0, N}, seed).value();

    int* p = buf.data();
    auto g = rt.graph();
    g.add(N, Reads(buf), Writes(buf), [p](std::size_t i) { p[i] *= 2; });
    g.run().value();

    const std::vector<int> out = buf.read_range({0, N}).value();
    ASSERT_EQ(out.size(), N);
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(out[i], static_cast<int>(i) * 2) << "at i=" << i;
}

// read_range can pull a window without copying the whole field.
TEST(Integration_DeviceResident, SelectiveReadback)
{
    constexpr std::size_t N = 64;

    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Device).value();

    std::vector<int> seed(N);
    std::iota(seed.begin(), seed.end(), 100);   // 100,101,...
    buf.write_range({0, N}, seed).value();

    const std::vector<int> window = buf.read_range({8, 4}).value();
    ASSERT_EQ(window.size(), 4u);
    for (std::size_t k = 0; k < 4; ++k)
        EXPECT_EQ(window[k], static_cast<int>(108 + k));
}

/**
 * @brief Tests that a device-resident State steps and is seeded and read back through its range
 *        API.
 */
TEST(Integration_DeviceResident, StateStepsResident)
{
    constexpr std::size_t N     = 96;
    constexpr std::size_t STEPS = 5;

    auto rt    = Runtime::create().value();
    auto field = rt.state<int>(N, DeviceIndex{0}, Residency::Device).value();

    EXPECT_EQ(field.residency(), Residency::Device);
    EXPECT_FALSE(field.is_host_accessible());

    std::vector<int> seed(N, 0);
    field.write_range({0, N}, seed).value();

    auto g = rt.graph();
    g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });
    g.run(STEPS).value();

    const std::vector<int> out = field.read_range({0, N}).value();
    ASSERT_EQ(out.size(), N);
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(out[i], static_cast<int>(STEPS)) << "at i=" << i;
}

/** @brief Tests that Shared residency is the default and stays host-addressable. */
TEST(Integration_DeviceResident, SharedDefaultUnchanged)
{
    constexpr std::size_t N = 32;

    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();

    EXPECT_EQ(buf.residency(), Residency::Shared);
    EXPECT_TRUE(buf.is_host_accessible());

    for (std::size_t i = 0; i < N; ++i)
        buf[i] = static_cast<int>(i);           // direct host indexing still valid

    const std::vector<int> out = buf.read_range({0, N}).value();
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_EQ(out[i], static_cast<int>(i));
}

/** @brief Tests that host() returns a span that indexes the whole shared buffer. */
TEST(Integration_DeviceResident, HostSpanIndexesSharedBuffer)
{
    constexpr std::size_t N = 16;

    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();

    auto view = buf.host();
    ASSERT_EQ(view.size(), N);
    for (std::size_t i = 0; i < N; ++i)
        view[i] = static_cast<int>(i * 2);

    const std::vector<int> out = buf.read_range({0, N}).value();
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_EQ(out[i], static_cast<int>(i * 2));
}

/** @brief Tests that host() on a device-resident buffer stops the process. */
TEST(Integration_DeviceResidentDeathTest, HostOnADeviceBufferStops)
{
    constexpr std::size_t N = 16;

    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Device).value();
    ASSERT_FALSE(buf.is_host_accessible());

    EXPECT_DEATH(static_cast<void>(buf.host()), "Buffer::host: a device-resident buffer");
}

/**
 * @brief Tests that a kernel writes a host-resident buffer and the host reads it by direct
 *        indexing.
 */
TEST(Integration_DeviceResident, HostBufferIsWrittenByAKernelAndReadOnTheHost)
{
    constexpr std::size_t N = 64;

    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Host).value();

    EXPECT_EQ(buf.residency(), Residency::Host);
    EXPECT_TRUE(buf.is_host_accessible());

    for (std::size_t i = 0; i < N; ++i)
        buf[i] = static_cast<int>(i);

    int* p = buf.data();
    auto g = rt.graph();
    g.add(N, Reads(buf), Writes(buf), [p](std::size_t i) { p[i] += 1000; });
    g.run().value();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i) + 1000) << "at i=" << i;
}
