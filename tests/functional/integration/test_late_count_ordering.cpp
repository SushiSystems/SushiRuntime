/****************************************************************/
/* test_late_count_ordering.cpp                                 */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// Coverage for the ordering claim behind LateCount's device slot (fix round 2
// of the 2026-09-16 scan-and-counting-sort wave): a device-sourced count must
// be resolved at the consuming node's own dispatch, after its producer, not
// polled at the step boundary before the producer has run. A graph whose
// resolver dropped the device slot from its Reads would still compile and run,
// but would fold last step's count instead of the one just written; this test
// is built to catch exactly that regression, not merely to exercise the API.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

// A producer node writes a fresh live count into a Residency::Host slot every
// run; a late-bound add_reduce reads that same slot through LateCount::device
// in the same run. The reduce must see the count this run's producer wrote.
TEST(Integration_LateCountOrdering, ReduceSeesTheCountItsPredecessorJustWrote)
{
    constexpr std::size_t CAPACITY = 1000;

    auto rt      = Runtime::create();
    auto values  = rt.buffer<double>(CAPACITY);
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    auto source  = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    auto out     = rt.buffer<double>(1);

    for (std::size_t i = 0; i < CAPACITY; ++i)
        values[i] = 1.0;

    // The producer reads its next count from a buffer rather than from a host
    // stack variable: the kernel may run on the device, which cannot reach this
    // function's frame at all.
    const std::uint32_t* source_ptr = source.data();
    std::uint32_t* counter_ptr = counter.data();

    LateCount lc;
    lc.capacity = CAPACITY;
    lc.device = &counter;

    auto g = rt.graph();
    g.add(Reads(source), Writes(counter), std::size_t{1},
          [counter_ptr, source_ptr](std::size_t) { counter_ptr[0] = source_ptr[0]; });
    g.add_reduce(values, out, lc, Sum<double>{});

    source[0] = 300;
    g.run();
    EXPECT_DOUBLE_EQ(out[0], 300.0)
        << "the reduce must fold the count its own step's producer wrote";

    source[0] = 700;
    g.run();
    EXPECT_DOUBLE_EQ(out[0], 700.0)
        << "a changed count must take effect the same run, not one run late";

    EXPECT_EQ(g.compile_count(), 1u);
}
