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
#include <vector>

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

using namespace SushiRuntime::Internal::Observability;

namespace
{
    /** @brief Builds timestamps with a backend submit reading. */
    EventTimestamps stamps(std::uint64_t submit, std::uint64_t start, std::uint64_t end, bool has_submit = true)
    {
        EventTimestamps value;
        value.submit_ns = submit;
        value.start_ns = start;
        value.end_ns = end;
        value.has_submit = has_submit;
        return value;
    }
} // namespace

/** @brief Tests that two events fold into the earliest submit and start and the latest end. */
TEST(Small_EventTimestamps, MergesEventsIntoOneSpan)
{
    EventTimestamps total = stamps(100, 110, 150);
    merge_timestamps(total, stamps(90, 130, 200));

    EXPECT_EQ(total.submit_ns, 90u);
    EXPECT_EQ(total.start_ns, 110u);
    EXPECT_EQ(total.end_ns, 200u);
    EXPECT_TRUE(total.has_submit);
}

/** @brief Tests that the merge covers both events whichever one is merged into the other. */
TEST(Small_EventTimestamps, TheMergeDoesNotDependOnTheOrder)
{
    EventTimestamps forward = stamps(100, 110, 150);
    merge_timestamps(forward, stamps(90, 130, 200));
    EventTimestamps backward = stamps(90, 130, 200);
    merge_timestamps(backward, stamps(100, 110, 150));

    EXPECT_EQ(forward.submit_ns, backward.submit_ns);
    EXPECT_EQ(forward.start_ns, backward.start_ns);
    EXPECT_EQ(forward.end_ns, backward.end_ns);
}

/** @brief Tests that the submit reading counts as the backend's own only when every event had one. */
TEST(Small_EventTimestamps, TheSubmitIsTheBackendsOnlyWhenEveryEventHadOne)
{
    EventTimestamps total = stamps(100, 110, 150, true);
    merge_timestamps(total, stamps(120, 120, 160, false));

    EXPECT_FALSE(total.has_submit);
}

/** @brief Tests that merging one event into itself changes nothing. */
TEST(Small_EventTimestamps, MergingAnEventIntoItselfChangesNothing)
{
    EventTimestamps total = stamps(10, 20, 30);
    merge_timestamps(total, stamps(10, 20, 30));

    EXPECT_EQ(total.submit_ns, 10u);
    EXPECT_EQ(total.start_ns, 20u);
    EXPECT_EQ(total.end_ns, 30u);
}

/** @brief Tests that an event that cannot be read fails the dispatch whether it is returned or handed over. */
TEST(Small_EventTimestamps, AnUnreadableEventFailsTheDispatch)
{
    EventTimestamps out = stamps(1, 2, 3);
    const std::vector<sycl::event> none;
    const std::vector<sycl::event> handed{sycl::event{}, sycl::event{}};

    // A default event comes from no queue built with profiling, so reading its times throws and is reported.
    EXPECT_FALSE(read_dispatch_timestamps(sycl::event{}, none, out));
    EXPECT_FALSE(read_dispatch_timestamps(sycl::event{}, handed, out));
    EXPECT_EQ(out.submit_ns, 1u) << "a failed read leaves the output as it was";
}
