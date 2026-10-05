/****************************************************************/
/* test_device_resident.cpp                                     */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
    auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Device);

    EXPECT_EQ(buf.residency(), Residency::Device);
    EXPECT_FALSE(buf.host_accessible());

    std::vector<int> seed(N);
    std::iota(seed.begin(), seed.end(), 0);     // 0,1,2,...
    buf.write_range({0, N}, seed.data());

    int* p = buf.data();
    auto g = rt.graph();
    g.add(API::Reads(buf), API::Writes(buf), N, [p](std::size_t i) { p[i] *= 2; });
    g.run();

    const std::vector<int> out = buf.read_range({0, N});
    ASSERT_EQ(out.size(), N);
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(out[i], static_cast<int>(i) * 2) << "at i=" << i;
}

// read_range can pull a window without copying the whole field.
TEST(Integration_DeviceResident, SelectiveReadback)
{
    constexpr std::size_t N = 64;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Device);

    std::vector<int> seed(N);
    std::iota(seed.begin(), seed.end(), 100);   // 100,101,...
    buf.write_range({0, N}, seed.data());

    const std::vector<int> window = buf.read_range({8, 4});
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
    auto field = rt.state<int>(N, DeviceIndex{0}, Residency::Device);

    EXPECT_EQ(field.residency(), Residency::Device);
    EXPECT_FALSE(field.host_accessible());

    std::vector<int> seed(N, 0);
    field.write_range({0, N}, seed.data());

    auto g = rt.graph();
    g.add(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });
    g.run(STEPS);

    const std::vector<int> out = field.read_range({0, N});
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

    const std::vector<int> out = buf.read_range({0, N});
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_EQ(out[i], static_cast<int>(i));
}

// host() is the branch-free bulk seam for shared USM: one residency check, then
// unchecked indexing over the whole buffer.
TEST(Integration_DeviceResident, HostSpanIndexesSharedBuffer)
{
    constexpr std::size_t N = 16;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);

    auto view = buf.host();
    ASSERT_EQ(view.size(), N);
    for (std::size_t i = 0; i < N; ++i)
        view[i] = static_cast<int>(i * 2);

    const std::vector<int> out = buf.read_range({0, N});
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_EQ(out[i], static_cast<int>(i * 2));
}

// A device-resident buffer is not host-addressable: host indexing now throws a
// deterministic error in every build instead of dereferencing into UB.
TEST(Integration_DeviceResident, HostAccessOnDeviceThrows)
{
    constexpr std::size_t N = 16;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Device);
    ASSERT_FALSE(buf.host_accessible());

    EXPECT_THROW((void)buf[0], Core::SushiException);
    EXPECT_THROW((void)buf.host(), Core::SushiException);
}

// A host-resident buffer is host-addressable, a kernel on its device writes it,
// and the host reads the result by direct indexing.
TEST(Integration_DeviceResident, HostBufferIsWrittenByAKernelAndReadOnTheHost)
{
    constexpr std::size_t N = 64;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Host);

    EXPECT_EQ(buf.residency(), Residency::Host);
    EXPECT_TRUE(buf.host_accessible());

    for (std::size_t i = 0; i < N; ++i)
        buf[i] = static_cast<int>(i);

    int* p = buf.data();
    auto g = rt.graph();
    g.add(API::Reads(buf), API::Writes(buf), N, [p](std::size_t i) { p[i] += 1000; });
    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i) + 1000) << "at i=" << i;
}
