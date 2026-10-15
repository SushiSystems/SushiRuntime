/**************************************************************************/
/* test_event_timestamps.cpp                                              */
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
 * @file test_event_timestamps.cpp
 * @brief Tests read_event_timestamps() on an event of a profiling queue and on a default event.
 * @see tests/platform/observability/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/observability/event_timestamps.hpp>

using namespace SushiRuntime::Internal::Observability;

/** @brief Tests that a profiled event reads start before end, and submit no later than start. */
TEST(Medium_EventTimestamps, AProfiledEventReadsOrderedTimes)
{
    if (sycl::device::get_devices().empty())
        GTEST_SKIP() << "no SYCL device";
    sycl::queue queue(sycl::device::get_devices().front(),
                      sycl::property_list{sycl::property::queue::enable_profiling()});
    int* const value = sycl::malloc_shared<int>(1, queue);
    if (value == nullptr)
        GTEST_SKIP() << "the device gives no shared memory";
    sycl::event event = queue.single_task([value] { *value = 1; });
    event.wait();

    EventTimestamps times;
    ASSERT_TRUE(read_event_timestamps(event, times));
    EXPECT_LE(times.start_ns, times.end_ns);
    if (times.has_submit)
        EXPECT_LE(times.submit_ns, times.start_ns);
    else
        EXPECT_EQ(times.submit_ns, times.start_ns);
    sycl::free(value, queue);
}

/** @brief Tests that an event with no profiling reads nothing and leaves the output alone. */
TEST(Medium_EventTimestamps, AnUnprofiledEventReadsNothing)
{
    EventTimestamps times;
    times.start_ns = 7;
    EXPECT_FALSE(read_event_timestamps(sycl::event(), times));
    EXPECT_EQ(times.start_ns, 7u);
}
