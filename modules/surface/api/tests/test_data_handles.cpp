/**************************************************************************/
/* test_data_handles.cpp                                                  */
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
 * @file test_data_handles.cpp
 * @brief Tests the range, allocation, residency and element contracts of Buffer and State.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Element count of every handle the range tests allocate. */
    constexpr std::size_t K_SIZE = 37;

    /** @brief Returns the pattern of the line a call writes when it stops on a window. */
    std::string window_pattern(const std::string& call, ElementRange range, std::size_t size)
    {
        return call + ".*offset " + std::to_string(range.offset) + ", count " +
               std::to_string(range.count) + ".*size " + std::to_string(size);
    }

    /**
     * @brief Expects read_range, write_range and region of a handle to stop on one window.
     * @param name "Buffer" or "State", as the messages spell the handle.
     */
    template <class Handle>
    void expect_every_range_call_stops(const Handle& handle, const std::string& name,
                                       ElementRange range)
    {
        const std::vector<int> source(K_SIZE, 1);
        EXPECT_DEATH(static_cast<void>(handle.read_range(range)),
                     window_pattern(name + "::read_range", range, handle.size()));
        EXPECT_DEATH(static_cast<void>(handle.write_range(range, source)),
                     window_pattern(name + "::write_range", range, handle.size()));
        EXPECT_DEATH(static_cast<void>(handle.region(range)),
                     window_pattern(name + "::region", range, handle.size()));
    }

    /** @brief Expects a handle to accept a zero-length window at its end. */
    template <class Handle>
    void expect_empty_window_at_the_end_accepted(const Handle& handle)
    {
        const ElementRange at_end{handle.size(), 0};
        EXPECT_TRUE(handle.read_range(at_end).value().empty());
        EXPECT_NO_THROW(handle.write_range(at_end, span<const int>()).value());
        const BufferRegion region = handle.region(at_end);
        EXPECT_EQ(region.offset, handle.size() * sizeof(int));
        EXPECT_EQ(region.length, 0u);
    }

    /** @brief A trivially copyable element type of two floats. */
    struct Pair
    {
        float x;
        float y;
    };
} // namespace

/**
 * @brief Tests that every range call of a Buffer stops on a window one past its end.
 */
TEST(Integration_DataHandlesDeathTest, BufferStopsOnAWindowPastTheEnd)
{
    auto rt = Runtime::create().value();
    const auto buf = rt.buffer<int>(K_SIZE).value();

    expect_every_range_call_stops(buf, "Buffer", {K_SIZE, 1});
    expect_every_range_call_stops(buf, "Buffer", {K_SIZE - 2, 5});
}

/**
 * @brief Tests that every range call of a State stops on a window one past its end.
 */
TEST(Integration_DataHandlesDeathTest, StateStopsOnAWindowPastTheEnd)
{
    auto rt = Runtime::create().value();
    const auto field = rt.state<int>(K_SIZE).value();

    expect_every_range_call_stops(field, "State", {K_SIZE, 1});
    expect_every_range_call_stops(field, "State", {K_SIZE - 2, 5});
}

/**
 * @brief Tests that a window whose offset plus count wraps around stops the call, not wraps.
 */
TEST(Integration_DataHandlesDeathTest, WindowWhoseEndWrapsStops)
{
    auto rt = Runtime::create().value();
    const auto buf = rt.buffer<int>(K_SIZE).value();

    const std::size_t most = std::numeric_limits<std::size_t>::max();
    expect_every_range_call_stops(buf, "Buffer", {most, 2});
    expect_every_range_call_stops(buf, "Buffer", {2, most});
}

/**
 * @brief Tests that a zero-length window at the end of a Buffer or a State is accepted.
 */
TEST(Integration_DataHandles, EmptyWindowAtTheEndIsAccepted)
{
    auto rt = Runtime::create().value();
    const auto buf = rt.buffer<int>(K_SIZE).value();
    const auto field = rt.state<int>(K_SIZE).value();

    expect_empty_window_at_the_end_accepted(buf);
    expect_empty_window_at_the_end_accepted(field);
}

/**
 * @brief Tests that a window which fits round-trips through write_range and read_range.
 */
TEST(Integration_DataHandles, FittingWindowRoundTrips)
{
    auto rt = Runtime::create().value();
    const auto buf = rt.buffer<int>(K_SIZE).value();
    for (int& element : buf.host())
        element = 0;

    const std::vector<int> source{11, 12, 13, 14, 15};
    buf.write_range({K_SIZE - 5, 5}, source).value();

    EXPECT_EQ(buf.read_range({K_SIZE - 5, 5}).value(), source);
    EXPECT_EQ(buf[K_SIZE - 6], 0);
}

/**
 * @brief Tests that write_range stops on a null source and a short source for a non-empty window.
 */
TEST(Integration_DataHandlesDeathTest, NullOrShortSourceWithACountStops)
{
    auto rt = Runtime::create().value();
    const auto buf = rt.buffer<int>(K_SIZE).value();
    const auto field = rt.state<int>(K_SIZE).value();
    const std::vector<int> two(2, 1);

    EXPECT_DEATH(static_cast<void>(buf.write_range({0, 4}, span<const int>())),
                 "Buffer::write_range: the source is null for a window of count 4");
    EXPECT_DEATH(static_cast<void>(field.write_range({0, 4}, span<const int>())),
                 "State::write_range: the source is null");
    EXPECT_DEATH(static_cast<void>(buf.write_range({0, 4}, two)),
                 "the source holds 2 elements for a window of count 4");
}

/**
 * @brief Tests that an element count whose byte size overflows stops before any allocation.
 */
TEST(Integration_DataHandlesDeathTest, OverflowingElementCountStopsBeforeAllocating)
{
    auto rt = Runtime::create().value();
    // The byte size of this count wraps to 8, which an unchecked allocation would grant.
    const std::size_t count = std::numeric_limits<std::size_t>::max() / sizeof(double) + 2;
    const std::string pattern = "Buffer allocation: " + std::to_string(count) + " elements";

    EXPECT_DEATH(static_cast<void>(rt.buffer<double>(count)), pattern);
    EXPECT_DEATH(static_cast<void>(rt.state<double>(count)), pattern);
}

/**
 * @brief Tests that a State and a Buffer answer the queries they share alike.
 */
TEST(Integration_DataHandles, StateAndBufferAnswerTheSharedQueriesAlike)
{
    auto rt = Runtime::create().value();
    const auto buf = rt.buffer<int>(K_SIZE, DeviceIndex{0}).value();
    const auto field = rt.state<int>(K_SIZE, DeviceIndex{0}).value();

    EXPECT_EQ(field.device_index(), DeviceIndex{0});
    EXPECT_EQ(field.device_index(), buf.device_index());
    EXPECT_EQ(field.size(), buf.size());
    EXPECT_EQ(field.empty(), buf.empty());
    EXPECT_EQ(field.residency(), buf.residency());
    EXPECT_EQ(field.is_host_accessible(), buf.is_host_accessible());

    const BufferRegion window = field.region({4, 8});
    EXPECT_EQ(window.base, static_cast<void*>(field.current()));
    EXPECT_EQ(window.offset, 4 * sizeof(int));
    EXPECT_EQ(window.length, 8 * sizeof(int));
}

/**
 * @brief Tests that a trivially copyable struct instantiates Buffer and State and round-trips.
 */
TEST(Integration_DataHandles, TriviallyCopyableStructRoundTrips)
{
    EXPECT_TRUE(std::is_trivially_copyable<Pair>::value);

    auto rt = Runtime::create().value();
    const auto buf = rt.buffer<Pair>(2).value();
    const auto field = rt.state<Pair>(2).value();
    const std::vector<Pair> source{{1.5f, -2.0f}, {3.25f, 4.0f}};
    buf.write_range({0, 2}, source).value();
    field.write_range({0, 2}, source).value();

    // A round trip copies bits, so the absolute tolerance only bounds the comparison.
    constexpr float K_TOLERANCE = 1e-6f;
    const std::vector<Pair> from_buffer = buf.read_range({0, 2}).value();
    const std::vector<Pair> from_state = field.read_range({0, 2}).value();
    ASSERT_EQ(from_buffer.size(), source.size());
    ASSERT_EQ(from_state.size(), source.size());
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        EXPECT_NEAR(from_buffer[i].x, source[i].x, K_TOLERANCE) << "at i=" << i;
        EXPECT_NEAR(from_buffer[i].y, source[i].y, K_TOLERANCE) << "at i=" << i;
        EXPECT_NEAR(from_state[i].x, source[i].x, K_TOLERANCE) << "at i=" << i;
        EXPECT_NEAR(from_state[i].y, source[i].y, K_TOLERANCE) << "at i=" << i;
    }
}

/**
 * @brief Tests that host() on a device-resident handle stops and names the handle.
 */
TEST(Integration_DataHandlesDeathTest, HostOnADeviceResidentHandleStops)
{
    auto rt = Runtime::create().value();
    const auto buf = rt.buffer<int>(K_SIZE, DeviceIndex{0}, Residency::Device).value();
    const auto field = rt.state<int>(K_SIZE, DeviceIndex{0}, Residency::Device).value();

    EXPECT_DEATH(static_cast<void>(buf.host()), "Buffer::host: a device-resident buffer");
    EXPECT_DEATH(static_cast<void>(field.host()), "State::host: a device-resident buffer");
}

#if SR_ENABLE_ASSERTIONS
/**
 * @brief Tests that operator[] on a device-resident handle stops where SR_ASSERT is checked.
 */
TEST(Integration_DataHandlesDeathTest, IndexingADeviceResidentHandleStops)
{
    auto rt = Runtime::create().value();
    const auto buf = rt.buffer<int>(K_SIZE, DeviceIndex{0}, Residency::Device).value();
    const auto field = rt.state<int>(K_SIZE, DeviceIndex{0}, Residency::Device).value();

    EXPECT_DEATH(static_cast<void>(buf[0]), "Buffer::operator.*a device-resident buffer");
    EXPECT_DEATH(static_cast<void>(field[0]), "State::operator.*a device-resident state");
}
#endif

/**
 * @brief Tests that buffer() and state() stop on a device index that names no device.
 */
TEST(Integration_DataHandlesDeathTest, DeviceIndexThatNamesNoDeviceStops)
{
    auto rt = Runtime::create().value();
    const std::size_t count = rt.advanced().device_count();
    const DeviceIndex past_the_end{count};
    const std::string detail = ": device index " + std::to_string(count) +
                               " names no device; the runtime drives " +
                               std::to_string(count) + " devices, and advanced";

    EXPECT_DEATH(static_cast<void>(rt.buffer<float>(8, past_the_end)),
                 "Runtime::buffer" + detail);
    EXPECT_DEATH(static_cast<void>(rt.state<float>(8, past_the_end)),
                 "Runtime::state" + detail);
}
