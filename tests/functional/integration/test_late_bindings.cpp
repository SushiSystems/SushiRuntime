/****************************************************************/
/* test_late_bindings.cpp                                       */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// WP-1 coverage: the persistent-graph / late-binding path. A graph whose live
// size or whose per-step participation changes between steps must be compiled
// exactly once and replayed, never recompiled. Each test asserts both the
// numerical result and Graph::compile_count() == 1.

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

// sized() shrinks the iteration range to a live prefix each step. Driving the
// live count from 1..CAP over CAP successive run(1) calls increments buf[i]
// exactly on the steps where i is inside the live range, so buf[i] == CAP - i.
// The plan must compile once and follow the changing size on replay.
TEST(Integration_LateBindings, DynamicSizePerElement)
{
    constexpr std::size_t CAP = 8;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(CAP);
    for (std::size_t i = 0; i < CAP; ++i)
        buf[i] = 0;

    int* p = buf.data();

    std::size_t live = 0;
    auto g = rt.graph();
    g.add(API::sized([&] { return live; }),
          API::Reads{}, API::Writes(buf), CAP,
          [p](std::size_t i) { p[i] += 1; });

    for (std::size_t k = 1; k <= CAP; ++k)
    {
        live = k;
        g.run();
    }

    EXPECT_EQ(g.compile_count(), 1u) << "size changes must not recompile.";
    for (std::size_t i = 0; i < CAP; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(CAP - i)) << "at i=" << i;
}

// when() gates whether the node runs at all. Toggling the predicate on/off over
// ten steps runs the increment only on the enabled steps, so each element ends
// at the count of enabled steps — with a single compile.
TEST(Integration_LateBindings, EnableTogglePerElement)
{
    constexpr std::size_t N     = 16;
    constexpr std::size_t STEPS = 10;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p = buf.data();

    bool on = true;
    auto g = rt.graph();
    g.add(API::when([&] { return on; }),
          API::Reads{}, API::Writes(buf), N,
          [p](std::size_t i) { p[i] += 1; });

    std::size_t enabled_steps = 0;
    for (std::size_t k = 0; k < STEPS; ++k)
    {
        on = (k % 2 == 0);          // enabled on even steps only
        if (on) ++enabled_steps;
        g.run();
    }

    EXPECT_EQ(g.compile_count(), 1u) << "enable toggles must not recompile.";
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(enabled_steps)) << "at i=" << i;
}

// A disabled step must release its successors so the rest of the graph still
// runs. A downstream task that always runs should advance every step regardless
// of the upstream node's enable state, proving instant-complete drains the DAG.
TEST(Integration_LateBindings, DisabledNodeStillReleasesSuccessors)
{
    constexpr std::size_t N     = 8;
    constexpr std::size_t STEPS = 6;

    auto rt   = Runtime::create();
    auto gate = rt.buffer<int>(N);   // written by the toggled upstream node
    auto sink = rt.buffer<int>(N);   // written by the always-on downstream node
    for (std::size_t i = 0; i < N; ++i) { gate[i] = 0; sink[i] = 0; }

    int* gp = gate.data();
    int* sp = sink.data();

    bool on = false;
    auto g = rt.graph();
    // Upstream writes gate; downstream reads gate and writes sink, so the
    // tracker orders downstream after upstream on the gate buffer.
    g.add(API::when([&] { return on; }),
          API::Reads{}, API::Writes(gate), N,
          [gp](std::size_t i) { gp[i] += 1; });
    g.add(API::Reads(gate), API::Writes(sink), N,
          [sp](std::size_t i) { sp[i] += 1; });

    for (std::size_t k = 0; k < STEPS; ++k)
        g.run();                     // upstream stays disabled the whole run

    EXPECT_EQ(g.compile_count(), 1u);
    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(gate[i], 0) << "disabled upstream must not run, at i=" << i;
        ASSERT_EQ(sink[i], static_cast<int>(STEPS))
            << "downstream must run every step, at i=" << i;
    }
}

// sized() on a stepped State: the live prefix grows each step while the graph is
// compiled once. Cells active on every step accumulate one increment per step,
// which is what a reference single-threaded loop would produce for that prefix.
TEST(Integration_LateBindings, DynamicSizeState)
{
    constexpr std::size_t CAP   = 8;
    constexpr std::size_t STEPS = 4;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(CAP);
    for (std::size_t i = 0; i < CAP; ++i)
        field[i] = 0;

    std::size_t live = 0;
    auto g = rt.graph();
    g.add(API::sized([&] { return live; }), field,
          [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    // Grow the active prefix 2,4,6,8; the first two cells are active every step.
    for (std::size_t k = 0; k < STEPS; ++k)
    {
        live = 2 * (k + 1);
        g.run();
    }

    EXPECT_EQ(g.compile_count(), 1u) << "state size changes must not recompile.";
    EXPECT_EQ(field[0], static_cast<int>(STEPS));
    EXPECT_EQ(field[1], static_cast<int>(STEPS));
}

// when() and sized() compose: the node runs only while enabled, and when it
// runs it iterates the live count. Over the run, an element is incremented once
// per step that is both enabled and within range — still a single compile.
TEST(Integration_LateBindings, ComposedWhenAndSized)
{
    constexpr std::size_t CAP   = 4;
    constexpr std::size_t STEPS = 8;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(CAP);
    for (std::size_t i = 0; i < CAP; ++i)
        buf[i] = 0;

    int* p = buf.data();

    bool on = true;
    std::size_t live = CAP;
    auto g = rt.graph();
    g.add(API::when([&] { return on; }).and_sized([&] { return live; }),
          API::Reads{}, API::Writes(buf), CAP,
          [p](std::size_t i) { p[i] += 1; });

    int expected0 = 0;               // increments seen by element 0
    for (std::size_t k = 0; k < STEPS; ++k)
    {
        on   = (k % 3 != 0);         // disabled every third step
        live = (k % 2 == 0) ? CAP : 2;
        if (on && live > 0) ++expected0;   // element 0 is in range whenever live > 0
        g.run();
    }

    EXPECT_EQ(g.compile_count(), 1u);
    EXPECT_EQ(buf[0], expected0);
}


// based_at() moves where a late-bound node starts, without recompiling. This is
// the graph-colouring shape: each colour is a contiguous slice of one constraint
// buffer and every slice shifts as constraints are added or removed. The callable
// still receives ABSOLUTE indices, so a kernel written without knowing about
// bases stays correct.
TEST(Integration_LateBindings, BaseOffsetIteratesASlice)
{
    constexpr std::size_t CAPACITY = 64;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(CAPACITY);
    int* p = buf.data();

    std::size_t base = 0;
    std::size_t count = 0;

    auto g = rt.graph();
    g.add(API::based_at([&base] { return base; })
              .and_sized([&count] { return count; }),
          API::Reads{}, API::Writes(buf), CAPACITY,
          [p](std::size_t i) { p[i] = static_cast<int>(i) + 1000; });

    const std::size_t starts[] = { 0, 8, 40, 63 };
    const std::size_t lengths[] = { 4, 16, 20, 1 };

    for (std::size_t round = 0; round < 4; ++round)
    {
        for (std::size_t i = 0; i < CAPACITY; ++i) p[i] = -1;
        base = starts[round];
        count = lengths[round];

        g.run();

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

// A base with no size runs from there to the end of the capacity, not over the
// whole buffer — the sentinel means "the rest", and with an offset the rest
// starts at the offset.
TEST(Integration_LateBindings, BaseWithoutASizeRunsToTheEnd)
{
    constexpr std::size_t CAPACITY = 32;
    constexpr std::size_t BASE = 12;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(CAPACITY);
    int* p = buf.data();
    for (std::size_t i = 0; i < CAPACITY; ++i) p[i] = 0;

    auto g = rt.graph();
    g.add(API::based_at([] { return BASE; }),
          API::Reads{}, API::Writes(buf), CAPACITY,
          [p](std::size_t i) { p[i] = 1; });
    g.run();

    for (std::size_t i = 0; i < BASE; ++i)
        ASSERT_EQ(buf[i], 0) << "at i=" << i;
    for (std::size_t i = BASE; i < CAPACITY; ++i)
        ASSERT_EQ(buf[i], 1) << "at i=" << i;
}

// based_at_device() takes the offset from a slot a predecessor wrote, so a
// colouring pass that computes both a slice's start and its length on the device
// can hand over both the same way. The slot is registered as a read, so the
// producer is ordered first without the caller naming it.
TEST(Integration_LateBindings, BaseAndSizeBothFromTheDevice)
{
    constexpr std::size_t CAPACITY = 128;

    auto rt     = Runtime::create();
    auto slice  = rt.buffer<std::uint32_t>(2, DeviceIndex{0}, Residency::Host);   // [0] = base, [1] = count
    auto buf    = rt.buffer<int>(CAPACITY);

    std::uint32_t* s = slice.data();
    int* p = buf.data();
    for (std::size_t i = 0; i < CAPACITY; ++i) p[i] = -1;

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(slice), std::size_t{1},
          [s](std::size_t) { s[0] = 30u; s[1] = 25u; });
    g.add(API::based_at_device(slice, 0).and_sized_from_device(slice.data(), 1),
          API::Reads{}, API::Writes(buf), CAPACITY,
          [p](std::size_t i) { p[i] = static_cast<int>(i); });
    g.run();

    for (std::size_t i = 0; i < CAPACITY; ++i)
    {
        const bool inside = (i >= 30 && i < 55);
        if (inside)
            ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
        else
            ASSERT_EQ(buf[i], -1) << "at i=" << i;
    }
}

// A window that runs off the end of the capacity fails the run, naming what it
// asked for. Clamping would silently drop the tail of a colour, which is the
// class of failure these controls exist to make impossible.
TEST(Integration_LateBindings, WindowPastCapacityFailsTheRun)
{
    constexpr std::size_t CAPACITY = 16;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(CAPACITY);
    int* p = buf.data();

    std::size_t base = 10;

    auto g = rt.graph();
    g.add(API::based_at([&base] { return base; }).and_sized([] { return std::size_t{10}; }),
          API::Reads{}, API::Writes(buf), CAPACITY,
          [p](std::size_t i) { p[i] = 1; });

    EXPECT_THROW(g.run(), SushiException) << "10 elements from index 10 does not fit 16";

    // A window that does fit runs normally: the rejection wedged nothing.
    base = 6;
    EXPECT_NO_THROW(g.run());
}

// based_at_device rejects a slot it could not read at dispatch, at the call site
// rather than mid-run.
TEST(Integration_LateBindings, BasedAtDeviceRejectsAnUnreadableSlot)
{
    auto rt = Runtime::create();
    auto resident = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Device);
    auto shared   = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Shared);
    auto host     = rt.buffer<std::uint32_t>(2, DeviceIndex{0}, Residency::Host);

    EXPECT_THROW(API::based_at_device(resident), SushiException);
    EXPECT_THROW(API::based_at_device(shared), SushiException);
    EXPECT_THROW(API::based_at_device(host, 7), SushiException);
    EXPECT_NO_THROW(API::based_at_device(host, 1));
}

// The raw-pointer setter takes no buffer, so add() checks the pointer itself: a
// slot inside Shared memory is rejected before the graph records the task.
TEST(Integration_LateBindings, RawSharedSlotIsRejectedAtAdd)
{
    constexpr std::size_t CAPACITY = 8;

    auto rt     = Runtime::create();
    auto shared = rt.buffer<std::uint32_t>(2, DeviceIndex{0}, Residency::Shared);
    auto buf    = rt.buffer<int>(CAPACITY);
    int* p = buf.data();

    auto g = rt.graph();
    API::Dynamic dyn;
    dyn.and_sized_from_device(shared.data(), 1);
    try
    {
        g.add(std::move(dyn), API::Reads{}, API::Writes(buf), CAPACITY,
              [p](std::size_t i) { p[i] = 1; });
        ADD_FAILURE() << "a raw Shared slot was accepted";
    }
    catch (const SushiException& e)
    {
        EXPECT_EQ(e.error().code, Core::Errc::invalid_access);
    }
    EXPECT_EQ(g.size(), 0u) << "the rejected add() must not record a task";
}
