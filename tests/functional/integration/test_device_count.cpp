/**************************************************************************/
/* test_device_count.cpp                                                  */
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

// Coverage for API::sized_from_device: a task whose iteration size comes from a
// count a PREDECESSOR KERNEL produced, within the same step.
//
// This is the shape a collision pipeline is: broadphase writes a pair count,
// narrowphase iterates exactly that many pairs and writes a manifold count, the
// solver iterates exactly that many manifolds. None of those numbers exists when
// the step boundary polls the host-side sized() providers, so without this the
// options are a mid-step readback or dispatching at the worst case every tick.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

// A producer kernel writes a count; a consumer sized_from_device that count
// iterates exactly that many elements, in one run(). The consumer must see the
// count this step wrote, not the previous step's and not the capacity.
TEST(Integration_DeviceCount, ConsumerIteratesTheCountTheProducerJustWrote)
{
    constexpr std::size_t CAPACITY = 4096;

    auto rt      = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1);
    auto marks   = rt.buffer<int>(CAPACITY);

    std::uint32_t* c = counter.data();
    int* m = marks.data();

    // The count the producer computes on the device. Read through a pointer so
    // the host cannot be accused of having supplied it.
    auto wanted = rt.buffer<std::uint32_t>(1);
    std::uint32_t* w = wanted.data();

    counter[0] = 0;
    for (std::size_t i = 0; i < CAPACITY; ++i)
        marks[i] = -1;

    auto g = rt.graph();

    // Producer: derive the count on the device from data the kernel reads.
    g.add(Reads(wanted), Writes(counter), std::size_t{1},
          [c, w](std::size_t) { c[0] = w[0] * 3u; });

    // Consumer: iterate exactly that many elements.
    g.add(API::sized_from_device(counter), Reads(), Writes(marks), CAPACITY,
          [m](std::size_t i) { m[i] = static_cast<int>(i); });

    const std::uint32_t multipliers[] = { 5, 200, 1, 0, 1000 };
    for (std::uint32_t multiplier : multipliers)
    {
        wanted[0] = multiplier;
        for (std::size_t i = 0; i < CAPACITY; ++i)
            marks[i] = -1;

        g.run();

        const std::size_t expected = static_cast<std::size_t>(multiplier) * 3u;
        ASSERT_EQ(counter[0], multiplier * 3u);

        for (std::size_t i = 0; i < expected; ++i)
            ASSERT_EQ(marks[i], static_cast<int>(i))
                << "element " << i << " should have run for count " << expected;
        for (std::size_t i = expected; i < CAPACITY; ++i)
            ASSERT_EQ(marks[i], -1)
                << "element " << i << " ran past the live count " << expected;
    }

    EXPECT_EQ(g.compile_count(), 1u)
        << "a device-driven count must not force a recompile";
}

// The producer/consumer edge is not something the caller has to remember: the
// counter is registered as a read of the consuming task, so the consumer is
// ordered after the producer even though the caller named neither in Reads().
// Without that edge the consumer could read the count before it was written and
// would iterate a stale value (0 on the first step).
TEST(Integration_DeviceCount, CounterIsRegisteredAsAReadAutomatically)
{
    constexpr std::size_t CAPACITY = 512;
    constexpr std::uint32_t LIVE = 300;

    auto rt      = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1);
    auto out     = rt.buffer<int>(CAPACITY);

    std::uint32_t* c = counter.data();
    int* o = out.data();

    counter[0] = 0;
    for (std::size_t i = 0; i < CAPACITY; ++i)
        out[i] = 0;

    auto g = rt.graph();
    g.add(Reads(), Writes(counter), std::size_t{1},
          [c](std::size_t) { c[0] = LIVE; });
    g.add(API::sized_from_device(counter), Reads(), Writes(out), CAPACITY,
          [o](std::size_t i) { o[i] = 1; });

    g.run();

    int ran = 0;
    for (std::size_t i = 0; i < CAPACITY; ++i)
        ran += out[i];
    EXPECT_EQ(ran, static_cast<int>(LIVE))
        << "the consumer did not observe the count written this step";
}

// A three-stage chain, which is the real shape: each stage's output count feeds
// the next stage's iteration size, all in one run() with one compilation.
TEST(Integration_DeviceCount, ChainedProducerCountsInOneComposition)
{
    constexpr std::size_t CAPACITY = 1024;

    auto rt     = Runtime::create();
    auto first  = rt.buffer<std::uint32_t>(1);
    auto second = rt.buffer<std::uint32_t>(1);
    auto stage1 = rt.buffer<int>(CAPACITY);
    auto stage2 = rt.buffer<int>(CAPACITY);

    std::uint32_t* c1 = first.data();
    std::uint32_t* c2 = second.data();
    int* s1 = stage1.data();
    int* s2 = stage2.data();

    first[0] = 0;
    second[0] = 0;

    auto g = rt.graph();

    // Stage 0: pick a first-level count on the device.
    g.add(Reads(), Writes(first), std::size_t{1}, [c1](std::size_t) { c1[0] = 700u; });

    // Stage 1: iterate it, and produce the next count from how much it did.
    g.add(API::sized_from_device(first), Reads(), Writes(stage1), CAPACITY,
          [s1](std::size_t i) { s1[i] = 1; });
    g.add(Reads(first), Writes(second), std::size_t{1},
          [c1, c2](std::size_t) { c2[0] = c1[0] / 2u; });

    // Stage 2: iterate the second count.
    g.add(API::sized_from_device(second), Reads(), Writes(stage2), CAPACITY,
          [s2](std::size_t i) { s2[i] = 1; });

    for (std::size_t i = 0; i < CAPACITY; ++i) { stage1[i] = 0; stage2[i] = 0; }
    g.run();

    int ran1 = 0, ran2 = 0;
    for (std::size_t i = 0; i < CAPACITY; ++i) { ran1 += stage1[i]; ran2 += stage2[i]; }

    EXPECT_EQ(ran1, 700);
    EXPECT_EQ(ran2, 350);
    EXPECT_EQ(g.compile_count(), 1u);
}

// when() still composes with a device-driven size: a disabled step runs nothing
// regardless of what the counter says.
TEST(Integration_DeviceCount, WhenComposesWithDeviceSize)
{
    constexpr std::size_t CAPACITY = 256;

    auto rt      = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1);
    auto out     = rt.buffer<int>(CAPACITY);

    std::uint32_t* c = counter.data();
    int* o = out.data();
    counter[0] = 0;

    bool enabled = true;

    auto g = rt.graph();
    g.add(Reads(), Writes(counter), std::size_t{1}, [c](std::size_t) { c[0] = 100u; });
    g.add(API::sized_from_device(counter)
              .and_when([&enabled] { return enabled; }),
          Reads(), Writes(out), CAPACITY,
          [o](std::size_t i) { o[i] += 1; });

    for (std::size_t i = 0; i < CAPACITY; ++i) o[i] = 0;

    g.run();
    EXPECT_EQ(out[0], 1);

    enabled = false;
    g.run();
    EXPECT_EQ(out[0], 1) << "a disabled step must run no elements";

    enabled = true;
    g.run();
    EXPECT_EQ(out[0], 2);
}

// A count larger than the compiled capacity fails the run with a message naming
// both numbers. Truncating instead would silently drop work, which is the exact
// failure mode this seam exists to remove.
TEST(Integration_DeviceCount, CountOverCapacityFailsTheRun)
{
    constexpr std::size_t CAPACITY = 64;

    auto rt      = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1);
    auto out     = rt.buffer<int>(CAPACITY);

    std::uint32_t* c = counter.data();
    int* o = out.data();

    auto g = rt.graph();
    g.add(Reads(), Writes(counter), std::size_t{1},
          [c](std::size_t) { c[0] = static_cast<std::uint32_t>(CAPACITY) + 1u; });
    g.add(API::sized_from_device(counter), Reads(), Writes(out), CAPACITY,
          [o](std::size_t i) { o[i] = 1; });

    EXPECT_THROW(g.run(), SushiException);

    // The runtime survives it: the pool is re-armable and a clean graph still runs.
    auto clean = rt.graph();
    clean.add(Reads(), Writes(out), CAPACITY, [o](std::size_t i) { o[i] = 7; });
    clean.run();
    EXPECT_EQ(out[0], 7);
}

// A device-resident counter is not host-addressable, so the size could not be
// read at dispatch. That is rejected where it is written, not mid-run.
TEST(Integration_DeviceCount, RejectsADeviceResidentCounter)
{
    auto rt = Runtime::create();
    auto resident = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Device);
    auto shared   = rt.buffer<std::uint32_t>(2);

    EXPECT_THROW(API::sized_from_device(resident), SushiException);
    EXPECT_THROW(API::sized_from_device(shared, 5), SushiException);
    EXPECT_NO_THROW(API::sized_from_device(shared, 1));
}
