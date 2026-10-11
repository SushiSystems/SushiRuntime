/**************************************************************************/
/* test_late_bindings.cpp                                                 */
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
 * @file test_late_bindings.cpp
 * @brief Tests that a graph whose live size, base or participation changes per step compiles once.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

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
    g.add(sized([&] { return live; }),
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
    g.add(when([&] { return on; }),
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
    g.add(when([&] { return on; }),
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
    g.add(sized([&] { return live; }), field,
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
    g.add(when([&] { return on; }).sized([&] { return live; }),
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
    g.add(based_at([&base] { return base; })
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
    g.add(based_at([] { return BASE; }),
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
    g.add(based_at_device(slice, 0).sized_from_device(slice, 1),
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
    g.add(based_at([&base] { return base; }).sized([] { return std::size_t{10}; }),
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
