/**************************************************************************/
/* test_dynamic.cpp                                                       */
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
 * @file test_dynamic.cpp
 * @brief Tests late-bound sizes, bases, enables and device counts of a dynamic launch.
 * @see tests/surface/api/vocabulary/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

/** @brief Tests that a graph whose live size, base or participation changes per step compiles once. */
namespace LateBindingsTests
{
    /** @brief Tests that sized() shrinks the iteration range to a live prefix each step. */
    TEST(Integration_LateBindings, DynamicSizePerElement)
    {
        constexpr std::size_t CAP = 8;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(CAP).value();
        for (std::size_t i = 0; i < CAP; ++i)
            buf[i] = 0;

        int* p = buf.data();

        std::size_t live = 0;
        auto g = rt.graph();
        g.add_dynamic(sized([&] { return live; }),
                      CAP, Reads{}, Writes(buf),
                      [p](std::size_t i) { p[i] += 1; });

        for (std::size_t k = 1; k <= CAP; ++k)
        {
            live = k;
            g.run().value();
        }

        EXPECT_EQ(g.compile_count(), 1u) << "size changes must not recompile.";
        for (std::size_t i = 0; i < CAP; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(CAP - i)) << "at i=" << i;
    }

    /** @brief Tests that when() runs the node only on the enabled steps. */
    TEST(Integration_LateBindings, EnableTogglePerElement)
    {
        constexpr std::size_t N     = 16;
        constexpr std::size_t STEPS = 10;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            buf[i] = 0;

        int* p = buf.data();

        bool on = true;
        auto g = rt.graph();
        g.add_dynamic(when([&] { return on; }),
                      N, Reads{}, Writes(buf),
                      [p](std::size_t i) { p[i] += 1; });

        std::size_t enabled_steps = 0;
        for (std::size_t k = 0; k < STEPS; ++k)
        {
            on = (k % 2 == 0);          // enabled on even steps only
            if (on) ++enabled_steps;
            g.run().value();
        }

        EXPECT_EQ(g.compile_count(), 1u) << "enable toggles must not recompile.";
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(enabled_steps)) << "at i=" << i;
    }

    /** @brief Tests that a disabled node still releases its successors. */
    TEST(Integration_LateBindings, DisabledNodeStillReleasesSuccessors)
    {
        constexpr std::size_t N     = 8;
        constexpr std::size_t STEPS = 6;

        auto rt   = Runtime::create().value();
        auto gate = rt.buffer<int>(N).value();   // written by the toggled upstream node
        auto sink = rt.buffer<int>(N).value();   // written by the always-on downstream node
        for (std::size_t i = 0; i < N; ++i) { gate[i] = 0; sink[i] = 0; }

        int* gp = gate.data();
        int* sp = sink.data();

        bool on = false;
        auto g = rt.graph();
        // Downstream reads the gate buffer upstream writes, so it is ordered after; see README.md.
        g.add_dynamic(when([&] { return on; }),
                      N, Reads{}, Writes(gate),
                      [gp](std::size_t i) { gp[i] += 1; });
        g.add(N, Reads(gate), Writes(sink),
              [sp](std::size_t i) { sp[i] += 1; });

        for (std::size_t k = 0; k < STEPS; ++k)
            g.run().value();                     // upstream stays disabled the whole run

        EXPECT_EQ(g.compile_count(), 1u);
        for (std::size_t i = 0; i < N; ++i)
        {
            ASSERT_EQ(gate[i], 0) << "disabled upstream must not run, at i=" << i;
            ASSERT_EQ(sink[i], static_cast<int>(STEPS))
                << "downstream must run every step, at i=" << i;
        }
    }

    /** @brief Tests that sized() on a stepped State follows a live prefix that grows each step. */
    TEST(Integration_LateBindings, DynamicSizeState)
    {
        constexpr std::size_t CAP   = 8;
        constexpr std::size_t STEPS = 4;

        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(CAP).value();
        for (std::size_t i = 0; i < CAP; ++i)
            field[i] = 0;

        std::size_t live = 0;
        auto g = rt.graph();
        g.add_dynamic(sized([&] { return live; }), field,
                      [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        // Grow the active prefix 2,4,6,8; the first two cells are active every step.
        for (std::size_t k = 0; k < STEPS; ++k)
        {
            live = 2 * (k + 1);
            g.run().value();
        }

        EXPECT_EQ(g.compile_count(), 1u) << "state size changes must not recompile.";
        EXPECT_EQ(field[0], static_cast<int>(STEPS));
        EXPECT_EQ(field[1], static_cast<int>(STEPS));
    }

    /** @brief Tests that when() and sized() compose on one node. */
    TEST(Integration_LateBindings, ComposedWhenAndSized)
    {
        constexpr std::size_t CAP   = 4;
        constexpr std::size_t STEPS = 8;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(CAP).value();
        for (std::size_t i = 0; i < CAP; ++i)
            buf[i] = 0;

        int* p = buf.data();

        bool on = true;
        std::size_t live = CAP;
        auto g = rt.graph();
        g.add_dynamic(when([&] { return on; }).sized([&] { return live; }),
                      CAP, Reads{}, Writes(buf),
                      [p](std::size_t i) { p[i] += 1; });

        int expected0 = 0;               // increments seen by element 0
        for (std::size_t k = 0; k < STEPS; ++k)
        {
            on   = (k % 3 != 0);         // disabled every third step
            live = (k % 2 == 0) ? CAP : 2;
            if (on && live > 0) ++expected0;   // element 0 is in range whenever live > 0
            g.run().value();
        }

        EXPECT_EQ(g.compile_count(), 1u);
        EXPECT_EQ(buf[0], expected0);
    }

    /** @brief Tests that based_at() moves where a late-bound node starts without recompiling. */
    TEST(Integration_LateBindings, BaseOffsetIteratesASlice)
    {
        constexpr std::size_t CAPACITY = 64;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(CAPACITY).value();
        int* p = buf.data();

        std::size_t base = 0;
        std::size_t count = 0;

        auto g = rt.graph();
        g.add_dynamic(based_at([&base] { return base; })
                  .sized([&count] { return count; }),
                      CAPACITY, Reads{}, Writes(buf),
                      [p](std::size_t i) { p[i] = static_cast<int>(i) + 1000; });

        const std::size_t starts[] = { 0, 8, 40, 63 };
        const std::size_t lengths[] = { 4, 16, 20, 1 };

        for (std::size_t round = 0; round < 4; ++round)
        {
            for (std::size_t i = 0; i < CAPACITY; ++i) p[i] = -1;
            base = starts[round];
            count = lengths[round];

            g.run().value();

            for (std::size_t i = 0; i < CAPACITY; ++i)
            {
                const bool inside = (i >= base && i < base + count);
                if (inside)
                    ASSERT_EQ(buf[i], static_cast<int>(i) + 1000)
                        << "round " << round << ", element " << i
                        << " should have run with its absolute index";
                else
                    ASSERT_EQ(buf[i], -1)
                        << "round " << round << ", element " << i
                        << " is outside the window but ran";
            }
        }

        EXPECT_EQ(g.compile_count(), 1u) << "moving the window must not recompile";
    }

    /** @brief Tests that a base without a size runs from the base to the end of the capacity. */
    TEST(Integration_LateBindings, BaseWithoutASizeRunsToTheEnd)
    {
        constexpr std::size_t CAPACITY = 32;
        constexpr std::size_t BASE = 12;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(CAPACITY).value();
        int* p = buf.data();
        for (std::size_t i = 0; i < CAPACITY; ++i) p[i] = 0;

        auto g = rt.graph();
        g.add_dynamic(based_at([] { return BASE; }),
                      CAPACITY, Reads{}, Writes(buf),
                      [p](std::size_t i) { p[i] = 1; });
        g.run().value();

        for (std::size_t i = 0; i < BASE; ++i)
            ASSERT_EQ(buf[i], 0) << "at i=" << i;
        for (std::size_t i = BASE; i < CAPACITY; ++i)
            ASSERT_EQ(buf[i], 1) << "at i=" << i;
    }

    /**
     * @brief Tests that based_at_device() and sized_from_device() take both values from slots a
     *        predecessor wrote.
     */
    TEST(Integration_LateBindings, BaseAndSizeBothFromTheDevice)
    {
        constexpr std::size_t CAPACITY = 128;

        auto rt     = Runtime::create().value();
        // slice[0] is the base and slice[1] the count.
        auto slice  = rt.buffer<std::uint32_t>(2, DeviceIndex{0}, Residency::Host).value();
        auto buf    = rt.buffer<int>(CAPACITY).value();

        std::uint32_t* s = slice.data();
        int* p = buf.data();
        for (std::size_t i = 0; i < CAPACITY; ++i) p[i] = -1;

        auto g = rt.graph();
        g.add(std::size_t{1}, Reads{}, Writes(slice),
              [s](std::size_t) { s[0] = 30u; s[1] = 25u; });
        g.add_dynamic(based_at_device(slice, 0).sized_from_device(slice, 1),
                      CAPACITY, Reads{}, Writes(buf),
                      [p](std::size_t i) { p[i] = static_cast<int>(i); });
        g.run().value();

        for (std::size_t i = 0; i < CAPACITY; ++i)
        {
            const bool inside = (i >= 30 && i < 55);
            if (inside)
                ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
            else
                ASSERT_EQ(buf[i], -1) << "at i=" << i;
        }
    }

    /** @brief Tests that a window past the capacity fails the run and names what it asked for. */
    TEST(Integration_LateBindings, WindowPastCapacityFailsTheRun)
    {
        constexpr std::size_t CAPACITY = 16;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(CAPACITY).value();
        int* p = buf.data();

        std::size_t base = 10;

        auto g = rt.graph();
        g.add_dynamic(based_at([&base] { return base; }).sized([] { return std::size_t{10}; }),
                      CAPACITY, Reads{}, Writes(buf),
                      [p](std::size_t i) { p[i] = 1; });

        const auto failed = g.run();
        ASSERT_FALSE(failed.has_value()) << "10 elements from index 10 does not fit 16";
        EXPECT_EQ(failed.error().code, ErrorCode::InvalidGraph);

        // A window that does fit runs normally: the rejection wedged nothing.
        base = 6;
        EXPECT_TRUE(g.run().has_value());
    }

    /** @brief Tests that based_at_device stops on an unreadable slot at the call site. */
    TEST(Integration_LateBindingsDeathTest, BasedAtDeviceStopsOnAnUnreadableSlot)
    {
        auto rt = Runtime::create().value();
        auto resident = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Device).value();
        auto shared   = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Shared).value();
        auto host     = rt.buffer<std::uint32_t>(2, DeviceIndex{0}, Residency::Host).value();

        EXPECT_DEATH(static_cast<void>(based_at_device(resident)),
                     "based_at_device: the buffer is Residency::Device");
        EXPECT_DEATH(static_cast<void>(based_at_device(shared)),
                     "based_at_device: the buffer is Residency::Shared");
        EXPECT_DEATH(static_cast<void>(based_at_device(host, 7)),
                     "based_at_device: index 7 is out of range");
        static_cast<void>(based_at_device(host, 1));
    }

    /** @brief Tests that the member sized_from_device stops on a Shared slot where it is called. */
    TEST(Integration_LateBindingsDeathTest, TheMemberSetterStopsOnASharedSlot)
    {
        auto rt     = Runtime::create().value();
        auto shared = rt.buffer<std::uint32_t>(2, DeviceIndex{0}, Residency::Shared).value();

        Dynamic dyn;
        EXPECT_DEATH(static_cast<void>(dyn.sized_from_device(shared, 1)),
                     "sized_from_device: the buffer is Residency::Shared");
    }
} // namespace LateBindingsTests

/** @brief Tests sized_from_device, which sizes a task from a count written earlier in the step. */
namespace DeviceCountTests
{
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
} // namespace DeviceCountTests

/** @brief Tests that a graph watches the buffer a Dynamic reads its count or base from. */
namespace DeviceSlotLivenessTests
{
    namespace
    {
        /** @brief Names the element type of every handle here. */
        using Word = std::uint32_t;

        constexpr std::size_t K_WIDTH = 64;
        constexpr Word K_LIVE = 24;

        /** @brief Allocates a one-word Residency::Host buffer that holds value. */
        Buffer<Word> host_slot(Runtime& rt, Word value)
        {
            auto slot = rt.buffer<Word>(1, DeviceIndex{0}, Residency::Host).value();
            slot[0] = value;
            return slot;
        }
    } // namespace

    /** @brief Tests that a run is refused once the buffer of sized_from_device() is destroyed. */
    TEST(Integration_DeviceSlotLiveness, ADestroyedCountBufferRefusesTheRun)
    {
        auto rt = Runtime::create().value();
        auto target = rt.buffer<Word>(K_WIDTH).value();
        std::optional<Buffer<Word>> counter(host_slot(rt, K_LIVE));
        Word* words = target.data();

        auto graph = rt.graph();
        graph.add_dynamic(sized_from_device(*counter, 0), K_WIDTH, Reads(), Writes(target),
                          [words](std::size_t i) { words[i] = 1u; });
        EXPECT_EQ(graph.recorder().watched_allocation_count(), 2u)
            << "the target and the counter are both watched";
        ASSERT_TRUE(graph.run());

        counter.reset();

        EXPECT_TRUE(graph.recorder().has_dangling_resource());
        auto refused = graph.run();
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, ErrorCode::DanglingResource);
    }

    /** @brief Tests that a run is refused once the buffer of based_at_device() is destroyed. */
    TEST(Integration_DeviceSlotLiveness, ADestroyedBaseBufferRefusesTheRun)
    {
        auto rt = Runtime::create().value();
        auto target = rt.buffer<Word>(K_WIDTH).value();
        std::optional<Buffer<Word>> base(host_slot(rt, 0u));
        Word* words = target.data();

        auto graph = rt.graph();
        graph.add_dynamic(based_at_device(*base), K_WIDTH, Reads(), Writes(target),
                          [words](std::size_t i) { words[i] = 1u; });
        ASSERT_TRUE(graph.run());

        base.reset();

        auto refused = graph.run();
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, ErrorCode::DanglingResource);
    }

    /** @brief Tests that a late-bound State rule watches its count buffer as well as its State. */
    TEST(Integration_DeviceSlotLiveness, ADestroyedCountBufferRefusesAStateRule)
    {
        auto rt = Runtime::create().value();
        auto field = rt.state<Word>(K_WIDTH).value();
        std::optional<Buffer<Word>> counter(host_slot(rt, K_LIVE));

        auto graph = rt.graph();
        graph.add_dynamic(sized_from_device(*counter), field,
                          [](std::size_t i, const Word* current, Word* next) { next[i] = current[i]; });
        ASSERT_TRUE(graph.run());

        counter.reset();

        auto refused = graph.run();
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, ErrorCode::DanglingResource);
    }
} // namespace DeviceSlotLivenessTests
