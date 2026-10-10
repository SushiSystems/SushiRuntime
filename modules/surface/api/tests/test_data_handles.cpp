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
 * @brief Tests the range, allocation and query contracts Buffer and State share.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <limits>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    /** @brief Element count of every handle the range tests allocate. */
    constexpr std::size_t K_SIZE = 37;

    /**
     * @brief Returns the error a call throws as a SushiException.
     * @return An error of code ok when the call throws nothing.
     */
    template <class Call>
    Core::Error error_of(Call&& call)
    {
        try
        {
            call();
        }
        catch (const Core::SushiException& exception)
        {
            return exception.error();
        }
        return Core::Error{};
    }

    /** @brief Expects an invalid_access error whose message holds the window and the size. */
    void expect_window_rejected(const Core::Error& error, const char* call,
                                API::ElementRange range, std::size_t size)
    {
        EXPECT_EQ(error.code, Core::Errc::invalid_access) << error.message;
        EXPECT_NE(error.message.find(call), std::string::npos) << error.message;
        EXPECT_NE(error.message.find("offset " + std::to_string(range.offset)), std::string::npos)
            << error.message;
        EXPECT_NE(error.message.find("count " + std::to_string(range.count)), std::string::npos)
            << error.message;
        EXPECT_NE(error.message.find("size " + std::to_string(size)), std::string::npos)
            << error.message;
    }

    /**
     * @brief Expects read_range, write_range and region of a handle to reject one window.
     * @param name "Buffer" or "State", as the messages spell the handle.
     */
    template <class Handle>
    void expect_every_range_call_rejects(const Handle& handle, const std::string& name,
                                         API::ElementRange range)
    {
        const std::vector<int> source(K_SIZE, 1);
        expect_window_rejected(
            error_of([&]() { const std::vector<int> out = handle.read_range(range); }),
            (name + "::read_range").c_str(), range, handle.size());
        expect_window_rejected(
            error_of([&]() { handle.write_range(range, source); }),
            (name + "::write_range").c_str(), range, handle.size());
        expect_window_rejected(
            error_of([&]() { const API::BufferRegion region = handle.region(range); }),
            (name + "::region").c_str(), range, handle.size());
    }

    /** @brief Expects a handle to accept a zero-length window at its end. */
    template <class Handle>
    void expect_empty_window_at_the_end_accepted(const Handle& handle)
    {
        const API::ElementRange at_end{handle.size(), 0};
        EXPECT_TRUE(handle.read_range(at_end).empty());
        EXPECT_NO_THROW(handle.write_range(at_end, span<const int>()));
        const API::BufferRegion region = handle.region(at_end);
        EXPECT_EQ(region.offset, handle.size() * sizeof(int));
        EXPECT_EQ(region.length, 0u);
    }
} // namespace

/**
 * @brief Tests that a Buffer rejects a window one past its end from every range call.
 */
TEST(Integration_DataHandles, BufferRejectsAWindowPastTheEnd)
{
    auto rt = Runtime::create();
    const auto buf = rt.buffer<int>(K_SIZE);

    expect_every_range_call_rejects(buf, "Buffer", {K_SIZE, 1});
    expect_every_range_call_rejects(buf, "Buffer", {K_SIZE - 2, 5});
}

/**
 * @brief Tests that a State rejects a window one past its end from every range call.
 */
TEST(Integration_DataHandles, StateRejectsAWindowPastTheEnd)
{
    auto rt = Runtime::create();
    const auto field = rt.state<int>(K_SIZE);

    expect_every_range_call_rejects(field, "State", {K_SIZE, 1});
    expect_every_range_call_rejects(field, "State", {K_SIZE - 2, 5});
}

/**
 * @brief Tests that a window whose offset plus count wraps around is rejected, not wrapped.
 */
TEST(Integration_DataHandles, WindowWhoseEndWrapsIsRejected)
{
    auto rt = Runtime::create();
    const auto buf = rt.buffer<int>(K_SIZE);

    const std::size_t most = std::numeric_limits<std::size_t>::max();
    expect_every_range_call_rejects(buf, "Buffer", {most, 2});
    expect_every_range_call_rejects(buf, "Buffer", {2, most});
}

/**
 * @brief Tests that a rejected write leaves the handle's elements as they were.
 */
TEST(Integration_DataHandles, RejectedWriteCopiesNothing)
{
    auto rt = Runtime::create();
    const auto buf = rt.buffer<int>(K_SIZE);
    for (int& element : buf.host())
        element = 7;

    const std::vector<int> source(K_SIZE, 9);
    const Core::Error error = error_of([&]() { buf.write_range({K_SIZE - 2, 5}, source); });

    EXPECT_EQ(error.code, Core::Errc::invalid_access);
    for (std::size_t i = 0; i < K_SIZE; ++i)
        ASSERT_EQ(buf[i], 7) << "at i=" << i;
}

/**
 * @brief Tests that a zero-length window at the end of a Buffer or a State is accepted.
 */
TEST(Integration_DataHandles, EmptyWindowAtTheEndIsAccepted)
{
    auto rt = Runtime::create();
    const auto buf = rt.buffer<int>(K_SIZE);
    const auto field = rt.state<int>(K_SIZE);

    expect_empty_window_at_the_end_accepted(buf);
    expect_empty_window_at_the_end_accepted(field);
}

/**
 * @brief Tests that a window which fits round-trips through write_range and read_range.
 */
TEST(Integration_DataHandles, FittingWindowRoundTrips)
{
    auto rt = Runtime::create();
    const auto buf = rt.buffer<int>(K_SIZE);
    for (int& element : buf.host())
        element = 0;

    const std::vector<int> source{11, 12, 13, 14, 15};
    buf.write_range({K_SIZE - 5, 5}, source);

    EXPECT_EQ(buf.read_range({K_SIZE - 5, 5}), source);
    EXPECT_EQ(buf[K_SIZE - 6], 0);
}

/**
 * @brief Tests that write_range rejects a null source and a short source for a non-empty window.
 */
TEST(Integration_DataHandles, NullOrShortSourceWithACountThrows)
{
    auto rt = Runtime::create();
    const auto buf = rt.buffer<int>(K_SIZE);
    const auto field = rt.state<int>(K_SIZE);
    const std::vector<int> two(2, 1);

    const Core::Error null_buffer = error_of([&]() { buf.write_range({0, 4}, span<const int>()); });
    EXPECT_EQ(null_buffer.code, Core::Errc::invalid_access);
    EXPECT_NE(null_buffer.message.find("count 4"), std::string::npos) << null_buffer.message;

    const Core::Error null_state =
        error_of([&]() { field.write_range({0, 4}, span<const int>()); });
    EXPECT_EQ(null_state.code, Core::Errc::invalid_access);
    EXPECT_NE(null_state.message.find("State::write_range"), std::string::npos)
        << null_state.message;

    const Core::Error short_source = error_of([&]() { buf.write_range({0, 4}, two); });
    EXPECT_EQ(short_source.code, Core::Errc::invalid_access);
    EXPECT_NE(short_source.message.find("holds 2"), std::string::npos) << short_source.message;
    EXPECT_NE(short_source.message.find("count 4"), std::string::npos) << short_source.message;
}

/**
 * @brief Tests that an element count whose byte size overflows is refused before any allocation.
 */
TEST(Integration_DataHandles, OverflowingElementCountThrowsBeforeAllocating)
{
    auto rt = Runtime::create();
    // The byte size of this count wraps to 8, which an unchecked allocation would grant.
    const std::size_t count = std::numeric_limits<std::size_t>::max() / sizeof(double) + 2;

    const Core::Error buffer_error = error_of([&]() { auto buf = rt.buffer<double>(count); });
    EXPECT_EQ(buffer_error.code, Core::Errc::allocation_failed);
    EXPECT_NE(buffer_error.message.find(std::to_string(count)), std::string::npos)
        << buffer_error.message;

    const Core::Error state_error = error_of([&]() { auto field = rt.state<double>(count); });
    EXPECT_EQ(state_error.code, Core::Errc::allocation_failed);
    EXPECT_NE(state_error.message.find(std::to_string(count)), std::string::npos)
        << state_error.message;
}

/**
 * @brief Tests that a State and a Buffer answer the queries they share alike.
 */
TEST(Integration_DataHandles, StateAndBufferAnswerTheSharedQueriesAlike)
{
    auto rt = Runtime::create();
    const auto buf = rt.buffer<int>(K_SIZE, DeviceIndex{0});
    const auto field = rt.state<int>(K_SIZE, DeviceIndex{0});

    EXPECT_EQ(field.device_index(), DeviceIndex{0});
    EXPECT_EQ(field.device_index(), buf.device_index());
    EXPECT_EQ(field.size(), buf.size());
    EXPECT_EQ(field.empty(), buf.empty());
    EXPECT_EQ(field.residency(), buf.residency());
    EXPECT_EQ(field.is_host_accessible(), buf.is_host_accessible());

    const API::BufferRegion window = field.region({4, 8});
    EXPECT_EQ(window.base, static_cast<void*>(field.current()));
    EXPECT_EQ(window.offset, 4 * sizeof(int));
    EXPECT_EQ(window.length, 8 * sizeof(int));
}
