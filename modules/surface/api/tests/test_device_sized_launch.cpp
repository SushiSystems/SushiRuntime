/**************************************************************************/
/* test_device_sized_launch.cpp                                           */
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
 * @file test_device_sized_launch.cpp
 * @brief Tests launches that read their live width on the device, on the chain and the scheduler.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t K_STEPS = 48;
    constexpr std::uint32_t K_CANARY = 0xDEADBEEFu;
    constexpr std::uint32_t K_MIX = 16777619u;

    /** @brief Returns what the marking kernel leaves in a word that held previous. */
    std::uint32_t marked(std::uint32_t previous, std::size_t item, std::size_t width,
                         std::size_t salt)
    {
        return (previous ^ static_cast<std::uint32_t>(item + width)) * K_MIX +
               static_cast<std::uint32_t>(salt);
    }

    /**
     * @brief Records one rule: a resolver of two live words and a kernel sized by the first.
     *
     * The kernel runs the work-items below the live width and folds its index, the width and
     * the salt into marks[t]; a word at or past the width keeps what it held.
     * @param marks Holds at least width.capacity() words; must outlive the graph.
     */
    void record_marking_rule(Recorder& recorder, const Buffer<std::uint32_t>& marks,
                             const LateCount& width, const LateCount& salt)
    {
        std::uint32_t* live =
            recorder.scratch<std::uint32_t>(2, marks.device_index(), marks.residency());
        const Internal::Memory::ResourceRegion live_region{live, 0, 2 * sizeof(std::uint32_t)};
        std::uint32_t* words = marks.data();

        auto rule = recorder.rule();
        recorder.resolve(std::array<LateCount, 2>{width, salt}, marks.device_index(),
                         Access(Reads(), Writes(live_region)),
                         [live](const std::array<std::size_t, 2>& values)
                         {
                             live[0] = static_cast<std::uint32_t>(values[0]);
                             live[1] = static_cast<std::uint32_t>(values[1]);
                         },
                         NodeOptions{std::size_t{1}, {}, {}, {}});
        recorder.kernel(DeviceSized{width.capacity(), live, live},
                        Access(Reads(marks, live_region), Writes(marks)),
                        [words](std::size_t t, std::size_t live_width, std::size_t live_salt)
                        {
                            words[t] = (words[t] ^ static_cast<std::uint32_t>(t + live_width)) *
                                       K_MIX + static_cast<std::uint32_t>(live_salt);
                        },
                        NodeOptions{width.capacity(), {}, {}, {}});
    }

    /** @brief Returns a LateCount whose live value is its capacity. */
    LateCount fixed_count(std::size_t capacity)
    {
        return LateCount::fixed(capacity);
    }

    /** @brief Returns a LateCount the kernels read from counter[0] on the device. */
    LateCount device_count(const Buffer<std::uint32_t>& counter, std::size_t capacity)
    {
        return LateCount::from_device(capacity, counter);
    }

    /** @brief Holds one run's per-step digests and the layer it took. */
    struct Recording
    {
        std::vector<std::uint32_t> digests;                           /**< One word per step. */
        /** @brief Layer the run took. */
        Internal::Observability::ExecutionLayer layer = Internal::Observability::ExecutionLayer::Scheduler;
    };

    /**
     * @brief Builds a live-count schedule over a capacity that hits 0 and the capacity.
     * @return Eleven counts, cycled by the producer.
     */
    std::vector<std::uint32_t> schedule_for(std::size_t capacity)
    {
        const auto cap = static_cast<std::uint32_t>(capacity);
        const auto clamp = [cap](std::uint32_t v) { return v > cap ? cap : v; };
        return {cap, 0u, clamp(1u), cap / 2u + (cap > 1u ? 1u : 0u), cap, 0u, cap / 3u,
                cap > 0u ? cap - 1u : 0u, clamp(cap / 7u + 3u), cap, clamp(2u)};
    }

    /** @brief Hosts the step counter, the live count and the digest history of one graph. */
    struct Harness
    {
        Buffer<std::uint32_t> counter;  /**< Residency::Host live count the producer writes. */
        Buffer<std::uint32_t> step;     /**< Steps the producer has run. */
        Buffer<std::uint32_t> schedule; /**< Live count per step, cycled. */
        Buffer<std::uint32_t> history;  /**< One digest per step. */

        /** @brief Allocates the shared buffers for a schedule over capacity. */
        Harness(Runtime& rt, std::size_t capacity)
            : counter(rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host)),
              step(rt.buffer<std::uint32_t>(1)),
              schedule(rt.buffer<std::uint32_t>(schedule_for(capacity).size())),
              history(rt.buffer<std::uint32_t>(K_STEPS))
        {
            const std::vector<std::uint32_t> s = schedule_for(capacity);
            for (std::size_t i = 0; i < s.size(); ++i)
                schedule[i] = s[i];
            counter[0] = 0;
            step[0] = 0;
            for (std::size_t i = 0; i < K_STEPS; ++i)
                history[i] = 0;
        }

        /** @brief Adds the kernel that advances the step and writes this step's live count. */
        void add_producer(Graph& g)
        {
            std::uint32_t* c = counter.data();
            std::uint32_t* st = step.data();
            const std::uint32_t* sc = schedule.data();
            const auto period = static_cast<std::uint32_t>(schedule.size());
            g.add(std::size_t{1}, Reads(schedule), Writes(counter, step),
                  [c, st, sc, period](std::size_t)
                  {
                      const std::uint32_t s = st[0];
                      c[0] = sc[s % period];
                      st[0] = s + 1u;
                  });
        }

        /** @brief Adds the kernel that folds the given words of source into this step's digest. */
        void add_digest(Graph& g, const Buffer<std::uint32_t>& source, std::size_t words)
        {
            const std::uint32_t* src = source.data();
            const std::uint32_t* st = step.data();
            std::uint32_t* h = history.data();
            g.add(std::size_t{1}, Reads(source, step), Writes(history),
                  [src, st, h, words](std::size_t)
                  {
                      std::uint32_t acc = 2166136261u;
                      for (std::size_t i = 0; i < words; ++i)
                          acc = (acc ^ src[i]) * K_MIX;
                      h[st[0] - 1u] = acc;
                  });
        }

        /** @brief Runs the graph for K_STEPS steps and copies the digests out. */
        Recording record(Graph& g)
        {
            Recording r;
            const auto report = g.run(K_STEPS);
            EXPECT_TRUE(report.is_successful);
            r.layer = report.layer;
            r.digests.assign(history.data(), history.data() + K_STEPS);
            return r;
        }
    };

    /**
     * @brief Runs a workload on a default and a forced-scheduler runtime and compares them.
     *
     * The default run must take the chain, the forced one the scheduler, and every step's
     * digest must match bit for bit.
     */
    void expect_chain_matches_scheduler(const std::function<Recording(Runtime&)>& workload)
    {
        auto chained_rt = Runtime::create();
        RuntimeConfig forced;
        forced.force_scheduler = true;
        auto scheduled_rt = Runtime::create(forced);

        const Recording chained = workload(chained_rt);
        const Recording scheduled = workload(scheduled_rt);

        EXPECT_EQ(chained.layer, Internal::Observability::ExecutionLayer::Chain)
            << "a plan of device-sized launches and kernels must take the chain";
        EXPECT_EQ(scheduled.layer, Internal::Observability::ExecutionLayer::Scheduler);
        ASSERT_EQ(chained.digests.size(), scheduled.digests.size());
        EXPECT_EQ(std::memcmp(chained.digests.data(), scheduled.digests.data(),
                              chained.digests.size() * sizeof(std::uint32_t)), 0);

        std::size_t changes = 0;
        for (std::size_t i = 1; i < chained.digests.size(); ++i)
            changes += (chained.digests[i] != chained.digests[i - 1]) ? 1u : 0u;
        EXPECT_GT(changes, K_STEPS / 4) << "the live count must change the results step to step";
    }
} // namespace

/** @brief Tests that exactly the work-items below the live width run, at every width. */
TEST(Integration_DeviceSizedLaunch, OnlyItemsBelowTheLiveWidthRun)
{
    constexpr std::size_t CAPACITY = 600;
    constexpr std::size_t SALT = 7;

    auto rt    = Runtime::create();
    auto marks = rt.buffer<std::uint32_t>(CAPACITY + 1);
    std::vector<std::uint32_t> expected(CAPACITY + 1, K_CANARY);
    for (std::size_t i = 0; i <= CAPACITY; ++i)
        marks[i] = K_CANARY;

    std::size_t live = 0;
    const LateCount width = LateCount::from_host(CAPACITY, [&live]() { return live; });
    const LateCount salt = LateCount::fixed(SALT);

    auto g = rt.graph();
    record_marking_rule(g.recorder(), marks, width, salt);

    for (const std::size_t n : {std::size_t{10}, std::size_t{0}, CAPACITY, std::size_t{257}})
    {
        live = n;
        g.run();
        for (std::size_t t = 0; t < n; ++t)
            expected[t] = marked(expected[t], t, n, SALT);
        for (std::size_t t = 0; t <= CAPACITY; ++t)
            ASSERT_EQ(marks[t], expected[t]) << "live width " << n << ", item " << t;
    }

    EXPECT_EQ(g.compile_count(), 1u) << "a live width change must not trigger a recompile";
}

/** @brief Tests that a null live width runs every item and the arity picks the live words. */
TEST(Integration_DeviceSizedLaunch, ANullLiveWidthRunsEveryItemAndTheArityPicksTheWords)
{
    constexpr std::size_t CAPACITY = 300;
    constexpr std::size_t LIVE = 123;

    auto rt     = Runtime::create();
    auto plain  = rt.buffer<std::uint32_t>(CAPACITY);
    auto worded = rt.buffer<std::uint32_t>(CAPACITY);
    for (std::size_t i = 0; i < CAPACITY; ++i)
    {
        plain[i] = K_CANARY;
        worded[i] = K_CANARY;
    }

    const LateCount count = LateCount::from_host(CAPACITY, []() { return LIVE; });

    auto g = rt.graph();
    Recorder& recorder = g.recorder();
    std::uint32_t* word =
        recorder.scratch<std::uint32_t>(1, plain.device_index(), plain.residency());
    const Internal::Memory::ResourceRegion word_region{word, 0, sizeof(std::uint32_t)};
    std::uint32_t* p = plain.data();
    std::uint32_t* w = worded.data();

    recorder.resolve(std::array<LateCount, 1>{count}, plain.device_index(),
                     Access(Reads(), Writes(word_region)),
                     [word](const std::array<std::size_t, 1>& live)
                     {
                         word[0] = static_cast<std::uint32_t>(live[0]);
                     });
    recorder.kernel(DeviceSized{CAPACITY, nullptr, nullptr},
                    Access(Reads(), Writes(plain)),
                    [p](std::size_t t) { p[t] = static_cast<std::uint32_t>(t); });
    recorder.kernel(DeviceSized{CAPACITY, nullptr, word},
                    Access(Reads(word_region), Writes(worded)),
                    [w](std::size_t t, std::size_t live)
                    {
                        w[t] = static_cast<std::uint32_t>(t + live);
                    });
    g.run();

    for (std::size_t t = 0; t < CAPACITY; ++t)
    {
        ASSERT_EQ(plain[t], t) << "item " << t;
        ASSERT_EQ(worded[t], t + LIVE) << "item " << t;
    }
}

/** @brief Tests that a launch of capacity zero runs no item and still releases its successor. */
TEST(Integration_DeviceSizedLaunch, ZeroCapacityLaunchRunsNothingAndReleasesItsSuccessor)
{
    auto rt     = Runtime::create();
    auto target = rt.buffer<std::uint32_t>(1);
    auto done   = rt.buffer<std::uint32_t>(1);
    target[0] = K_CANARY;
    done[0] = 0;
    std::uint32_t* t = target.data();
    std::uint32_t* d = done.data();

    auto g = rt.graph();
    g.recorder().kernel(DeviceSized{0, nullptr, nullptr},
                        Access(Reads(), Writes(target)),
                        [t](std::size_t item) { t[item] = 0u; });
    g.add(std::size_t{1}, Reads(target), Writes(done), [t, d](std::size_t) { d[0] = t[0]; });

    const auto report = g.run();

    EXPECT_TRUE(report.is_successful);
    EXPECT_EQ(target[0], K_CANARY);
    EXPECT_EQ(done[0], K_CANARY) << "the successor must run after the empty launch";
}

/**
 * @brief Tests that a device-sized rule gives the same bytes on both layers as its counts change.
 */
TEST(Integration_DeviceSizedLaunch, DeviceSizedRuleChainMatchesSchedulerAsTheCountsChange)
{
    constexpr std::size_t CAPACITY = 4096;
    constexpr std::size_t SALT_CAPACITY = std::size_t{1} << 20;
    expect_chain_matches_scheduler([](Runtime& rt)
    {
        Harness hx(rt, CAPACITY);
        auto marks = rt.buffer<std::uint32_t>(CAPACITY);
        for (std::size_t i = 0; i < CAPACITY; ++i)
            marks[i] = static_cast<std::uint32_t>(i);

        // The salt changes every step on the host while the width changes on the device.
        const std::array<std::size_t, 4> salts{5, 300, 70000, SALT_CAPACITY};
        auto tick = std::make_shared<std::size_t>(0);
        const LateCount salt = LateCount::from_host(
            SALT_CAPACITY, [salts, tick] { return salts[(*tick)++ % salts.size()]; });

        auto g = rt.graph();
        hx.add_producer(g);
        record_marking_rule(g.recorder(), marks, device_count(hx.counter, CAPACITY), salt);
        hx.add_digest(g, marks, CAPACITY);
        return hx.record(g);
    });
}

/** @brief Tests that a sized_from_device task gives the same bytes on both layers. */
TEST(Integration_DeviceSizedLaunch, SizedFromDeviceChainMatchesSchedulerAsTheCountChanges)
{
    constexpr std::size_t CAPACITY = 4096;
    expect_chain_matches_scheduler([](Runtime& rt)
    {
        Harness hx(rt, CAPACITY);
        auto acc = rt.buffer<std::uint32_t>(CAPACITY);
        for (std::size_t i = 0; i < CAPACITY; ++i)
            acc[i] = static_cast<std::uint32_t>(i);
        std::uint32_t* a = acc.data();

        auto g = rt.graph();
        hx.add_producer(g);
        g.add(sized_from_device(hx.counter), CAPACITY, Reads{}, Writes(acc),
              [a](std::size_t i) { a[i] = a[i] * 2654435761u + 12345u; });
        hx.add_digest(g, acc, CAPACITY);
        return hx.record(g);
    });
}

/** @brief Tests that a device count over its capacity fails a chained run. */
TEST(Integration_DeviceSizedLaunch, DeviceCountOverCapacityFailsAChainedRun)
{
    constexpr std::size_t CAPACITY = 100;

    auto rt = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    auto marks = rt.buffer<std::uint32_t>(CAPACITY);
    for (std::size_t i = 0; i < CAPACITY; ++i)
        marks[i] = 0;
    std::uint32_t* c = counter.data();

    auto g = rt.graph();
    g.add(std::size_t{1}, Reads{}, Writes(counter),
          [c](std::size_t) { c[0] = static_cast<std::uint32_t>(CAPACITY) + 1u; });
    record_marking_rule(g.recorder(), marks, device_count(counter, CAPACITY), fixed_count(1));
    EXPECT_THROW(g.run(4), SushiException);

    // A later graph with a count inside the capacity runs clean on the same runtime.
    auto valid = rt.graph();
    valid.add(std::size_t{1}, Reads{}, Writes(counter), [c](std::size_t) { c[0] = 3u; });
    record_marking_rule(valid.recorder(), marks, device_count(counter, CAPACITY), fixed_count(1));
    const auto report = valid.run(2);
    EXPECT_TRUE(report.is_successful);
    EXPECT_EQ(report.layer, Internal::Observability::ExecutionLayer::Chain);
}
