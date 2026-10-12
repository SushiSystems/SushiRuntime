/**************************************************************************/
/* test_device_count.cpp                                                  */
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
 * @file test_device_count.cpp
 * @brief Tests sized_from_device, which sizes a task from a count written earlier in the step.
 * @see modules/surface/api/tests/vocabulary/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

/** @brief Tests that a consumer iterates exactly the count the producer wrote in the same run(). */
TEST(Integration_DeviceCount, ConsumerIteratesTheCountTheProducerJustWrote)
{
    constexpr std::size_t CAPACITY = 4096;

    auto rt      = Runtime::create().value();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto marks   = rt.buffer<int>(CAPACITY).value();

    std::uint32_t* c = counter.data();
    int* m = marks.data();

    // The producer reads the count through a pointer, so the device computes it; see README.md.
    auto wanted = rt.buffer<std::uint32_t>(1).value();
    std::uint32_t* w = wanted.data();

    counter[0] = 0;
    for (std::size_t i = 0; i < CAPACITY; ++i)
        marks[i] = -1;

    auto g = rt.graph();

    // Producer: derive the count on the device from data the kernel reads.
    g.add(std::size_t{1}, Reads(wanted), Writes(counter),
          [c, w](std::size_t) { c[0] = w[0] * 3u; });

    // Consumer: iterate exactly that many elements.
    g.add_dynamic(sized_from_device(counter), CAPACITY, Reads(), Writes(marks),
                  [m](std::size_t i) { m[i] = static_cast<int>(i); });

    const std::uint32_t multipliers[] = { 5, 200, 1, 0, 1000 };
    for (std::uint32_t multiplier : multipliers)
    {
        wanted[0] = multiplier;
        for (std::size_t i = 0; i < CAPACITY; ++i)
            marks[i] = -1;

        g.run().value();

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

/** @brief Tests that the counter is registered as a read of the consuming task. */
TEST(Integration_DeviceCount, CounterIsRegisteredAsAReadAutomatically)
{
    constexpr std::size_t CAPACITY = 512;
    constexpr std::uint32_t LIVE = 300;

    auto rt      = Runtime::create().value();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto out     = rt.buffer<int>(CAPACITY).value();

    std::uint32_t* c = counter.data();
    int* o = out.data();

    counter[0] = 0;
    for (std::size_t i = 0; i < CAPACITY; ++i)
        out[i] = 0;

    auto g = rt.graph();
    g.add(std::size_t{1}, Reads(), Writes(counter),
          [c](std::size_t) { c[0] = LIVE; });
    g.add_dynamic(sized_from_device(counter), CAPACITY, Reads(), Writes(out),
                  [o](std::size_t i) { o[i] = 1; });

    g.run().value();

    int ran = 0;
    for (std::size_t i = 0; i < CAPACITY; ++i)
        ran += out[i];
    EXPECT_EQ(ran, static_cast<int>(LIVE))
        << "the consumer did not observe the count written this step";
}

/** @brief Tests a three-stage chain where each stage's output count sizes the next stage. */
TEST(Integration_DeviceCount, ChainedProducerCountsInOneComposition)
{
    constexpr std::size_t CAPACITY = 1024;

    auto rt     = Runtime::create().value();
    auto first  = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto second = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto stage1 = rt.buffer<int>(CAPACITY).value();
    auto stage2 = rt.buffer<int>(CAPACITY).value();

    std::uint32_t* c1 = first.data();
    std::uint32_t* c2 = second.data();
    int* s1 = stage1.data();
    int* s2 = stage2.data();

    first[0] = 0;
    second[0] = 0;

    auto g = rt.graph();

    // Stage 0: pick a first-level count on the device.
    g.add(std::size_t{1}, Reads(), Writes(first), [c1](std::size_t) { c1[0] = 700u; });

    // Stage 1: iterate it, and produce the next count from how much it did.
    g.add_dynamic(sized_from_device(first), CAPACITY, Reads(), Writes(stage1),
                  [s1](std::size_t i) { s1[i] = 1; });
    g.add(std::size_t{1}, Reads(first), Writes(second),
          [c1, c2](std::size_t) { c2[0] = c1[0] / 2u; });

    // Stage 2: iterate the second count.
    g.add_dynamic(sized_from_device(second), CAPACITY, Reads(), Writes(stage2),
                  [s2](std::size_t i) { s2[i] = 1; });

    for (std::size_t i = 0; i < CAPACITY; ++i) { stage1[i] = 0; stage2[i] = 0; }
    g.run().value();

    int ran1 = 0, ran2 = 0;
    for (std::size_t i = 0; i < CAPACITY; ++i) { ran1 += stage1[i]; ran2 += stage2[i]; }

    EXPECT_EQ(ran1, 700);
    EXPECT_EQ(ran2, 350);
    EXPECT_EQ(g.compile_count(), 1u);
}

/** @brief Tests that a disabled when() step runs nothing whatever the counter says. */
TEST(Integration_DeviceCount, WhenComposesWithDeviceSize)
{
    constexpr std::size_t CAPACITY = 256;

    auto rt      = Runtime::create().value();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto out     = rt.buffer<int>(CAPACITY).value();

    std::uint32_t* c = counter.data();
    int* o = out.data();
    counter[0] = 0;

    bool enabled = true;

    auto g = rt.graph();
    g.add(std::size_t{1}, Reads(), Writes(counter), [c](std::size_t) { c[0] = 100u; });
    g.add_dynamic(sized_from_device(counter)
              .when([&enabled] { return enabled; }),
                  CAPACITY, Reads(), Writes(out),
                  [o](std::size_t i) { o[i] += 1; });

    for (std::size_t i = 0; i < CAPACITY; ++i) o[i] = 0;

    g.run().value();
    EXPECT_EQ(out[0], 1);

    enabled = false;
    g.run().value();
    EXPECT_EQ(out[0], 1) << "a disabled step must run no elements";

    enabled = true;
    g.run().value();
    EXPECT_EQ(out[0], 2);
}

/**
 * @brief Tests that a count above the compiled capacity fails the run with a message naming both
 *        numbers.
 */
TEST(Integration_DeviceCount, CountOverCapacityFailsTheRun)
{
    constexpr std::size_t CAPACITY = 64;

    auto rt      = Runtime::create().value();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto out     = rt.buffer<int>(CAPACITY).value();

    std::uint32_t* c = counter.data();
    int* o = out.data();

    auto g = rt.graph();
    g.add(std::size_t{1}, Reads(), Writes(counter),
          [c](std::size_t) { c[0] = static_cast<std::uint32_t>(CAPACITY) + 1u; });
    g.add_dynamic(sized_from_device(counter), CAPACITY, Reads(), Writes(out),
                  [o](std::size_t i) { o[i] = 1; });

    EXPECT_FALSE(g.run().has_value());

    // The runtime survives it: the pool is re-armable and a clean graph still runs.
    auto clean = rt.graph();
    clean.add(CAPACITY, Reads(), Writes(out), [o](std::size_t i) { o[i] = 7; });
    clean.run().value();
    EXPECT_EQ(out[0], 7);
}

/** @brief Tests that a counter the host cannot read stops the call that names it. */
TEST(Integration_DeviceCountDeathTest, ACounterThatIsNotHostResidentStops)
{
    auto rt = Runtime::create().value();
    auto resident = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Device).value();
    auto shared   = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Shared).value();
    auto host     = rt.buffer<std::uint32_t>(2, DeviceIndex{0}, Residency::Host).value();

    EXPECT_DEATH(static_cast<void>(sized_from_device(resident)),
                 "sized_from_device: the buffer is Residency::Device.*Residency::Host");
    EXPECT_DEATH(static_cast<void>(sized_from_device(shared)),
                 "sized_from_device: the buffer is Residency::Shared.*Residency::Host");
    EXPECT_DEATH(static_cast<void>(sized_from_device(host, 5)),
                 "sized_from_device: index 5 is out of range");
    static_cast<void>(sized_from_device(host, 1));
}

/** @brief Tests that a count is read from the element the index names, not from element zero. */
TEST(Integration_DeviceCount, ReadsTheCountAtTheGivenIndex)
{
    constexpr std::size_t CAPACITY = 16;
    constexpr std::size_t SLOT     = 5;

    auto rt   = Runtime::create().value();
    auto plan = rt.buffer<std::uint32_t>(8, DeviceIndex{0}, Residency::Host).value();
    auto out  = rt.buffer<int>(CAPACITY).value();

    for (std::size_t i = 0; i < CAPACITY; ++i)
        out[i] = -1;
    plan[SLOT] = 3;

    int* o = out.data();

    auto g = rt.graph();
    ASSERT_NO_THROW(g.add_dynamic(sized_from_device(plan, SLOT),
                                  CAPACITY, Reads(), Writes(out),
                                  [o](std::size_t i) { o[i] = static_cast<int>(i); }));
    g.run().value();

    for (std::size_t i = 0; i < 3; ++i)
        ASSERT_EQ(out[i], static_cast<int>(i)) << "element " << i;
    for (std::size_t i = 3; i < CAPACITY; ++i)
        ASSERT_EQ(out[i], -1) << "element " << i << " ran past the live count";
}

/** @brief Tests that add() accepts a based_at_device slot at a non-zero index. */
TEST(Integration_DeviceCount, AcceptsABaseSlotAtTheGivenIndex)
{
    constexpr std::size_t CAPACITY = 16;
    constexpr std::size_t SLOT     = 3;

    auto rt   = Runtime::create().value();
    auto plan = rt.buffer<std::uint32_t>(8, DeviceIndex{0}, Residency::Host).value();
    auto out  = rt.buffer<int>(CAPACITY).value();

    plan[SLOT] = 4;

    auto g = rt.graph();
    EXPECT_NO_THROW(g.add_dynamic(based_at_device(plan, SLOT),
                                  CAPACITY, Reads(), Writes(out),
                                  [](std::size_t) {}));
}

/** @brief Tests that the member setters check their slot as the free builders do. */
TEST(Integration_DeviceCountDeathTest, MemberSettersStopOnASlotThatIsNotHostResident)
{
    auto rt       = Runtime::create().value();
    auto resident = rt.buffer<std::uint32_t>(4, DeviceIndex{0}, Residency::Device).value();
    auto host     = rt.buffer<std::uint32_t>(4, DeviceIndex{0}, Residency::Host).value();

    Dynamic dyn;
    EXPECT_DEATH(static_cast<void>(dyn.sized_from_device(resident, 1)),
                 "sized_from_device: the buffer is Residency::Device");
    EXPECT_DEATH(static_cast<void>(dyn.based_at_device(host, 4)),
                 "based_at_device: index 4 is out of range");
}
