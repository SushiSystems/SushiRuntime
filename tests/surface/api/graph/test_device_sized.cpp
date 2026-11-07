/**************************************************************************/
/* test_device_sized.cpp                                                  */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_device_sized.cpp
 * @brief Tests launches whose width a device count sets.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/host_runtime.hpp"
#include "support/runtime_environment.hpp"

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
            recorder.scratch<std::uint32_t>(2, marks.device_index(), marks.residency()).value();
        const BufferRegion live_region{live, 0, 2 * sizeof(std::uint32_t)};
        std::uint32_t* words = marks.data();

        auto rule = recorder.rule();
        recorder.resolve(std::array<LateCount, 2>{width, salt}, marks.device_index(),
                         Access(Reads(), Writes(live_region)),
                         [live](const std::array<std::size_t, 2>& values)
                         {
                             live[0] = static_cast<std::uint32_t>(values[0]);
                             live[1] = static_cast<std::uint32_t>(values[1]);
                         },
                         NodeOptions{std::size_t{1}});
        recorder.kernel(DeviceSized{width.capacity(), live, live},
                        Access(Reads(marks, live_region), Writes(marks)),
                        [words](std::size_t t, std::size_t live_width, std::size_t live_salt)
                        {
                            words[t] = (words[t] ^ static_cast<std::uint32_t>(t + live_width)) *
                                       K_MIX + static_cast<std::uint32_t>(live_salt);
                        },
                        NodeOptions{width.capacity()});
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
        Harness(const Runtime& rt, std::size_t capacity)
            : counter(rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value()),
              step(rt.buffer<std::uint32_t>(1).value()),
              schedule(rt.buffer<std::uint32_t>(schedule_for(capacity).size()).value()),
              history(rt.buffer<std::uint32_t>(K_STEPS).value())
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
        std::vector<std::uint32_t> record(Graph& g)
        {
            const auto report = g.run(K_STEPS).value();
            EXPECT_TRUE(report.is_successful);
            return std::vector<std::uint32_t>(history.data(), history.data() + K_STEPS);
        }
    };

    /** @brief Folds the first words words of source into a digest, as the digest kernel does. */
    std::uint32_t digest_of(const std::vector<std::uint32_t>& source, std::size_t words)
    {
        std::uint32_t acc = 2166136261u;
        for (std::size_t i = 0; i < words; ++i)
            acc = (acc ^ source[i]) * K_MIX;
        return acc;
    }

    /** @brief Checks that the live count changes the digests step to step, so a match shows the count at work. */
    void expect_digests_change(const std::vector<std::uint32_t>& digests)
    {
        std::size_t changes = 0;
        for (std::size_t i = 1; i < digests.size(); ++i)
            changes += (digests[i] != digests[i - 1]) ? 1u : 0u;
        EXPECT_GT(changes, K_STEPS / 4) << "the live count must change the results step to step";
    }
} // namespace


/** @brief Fixture of the device-sized launch tests, which record graphs on a host runtime. */
class Small_DeviceSizedLaunch : public TestSupport::HostRuntimeTest
{
};

/** @brief Fixture of the sized_from_device test, which runs on a host runtime. */
class Small_SizedFromDevice : public TestSupport::HostRuntimeTest
{
};

/**
 * @brief Fixture of the rows that run a device-sized workload on the in-order chain (false) and on the worker pool (true).
 */
class Medium_DeviceSizedLayers : public TestSupport::LayerRuntimeTest
{
};

/** @brief Tests that exactly the work-items below the live width run, at every width. */
TEST_F(Small_DeviceSizedLaunch, OnlyItemsBelowTheLiveWidthRun)
{
    constexpr std::size_t CAPACITY = 600;
    constexpr std::size_t SALT = 7;

    auto marks = runtime().buffer<std::uint32_t>(CAPACITY + 1).value();
    std::vector<std::uint32_t> expected(CAPACITY + 1, K_CANARY);
    for (std::size_t i = 0; i <= CAPACITY; ++i)
        marks[i] = K_CANARY;

    std::size_t live = 0;
    const LateCount width = LateCount::from_host(CAPACITY, [&live]() { return live; });
    const LateCount salt = LateCount::fixed(SALT);

    auto g = runtime().graph();
    record_marking_rule(g.recorder(), marks, width, salt);

    for (const std::size_t n : {std::size_t{10}, std::size_t{0}, CAPACITY, std::size_t{257}})
    {
        live = n;
        g.run().value();
        for (std::size_t t = 0; t < n; ++t)
            expected[t] = marked(expected[t], t, n, SALT);
        for (std::size_t t = 0; t <= CAPACITY; ++t)
            ASSERT_EQ(marks[t], expected[t]) << "live width " << n << ", item " << t;
    }
}

/** @brief Tests that a change of the live width does not recompile the graph. */
TEST_F(Small_DeviceSizedLaunch, ALiveWidthChangeDoesNotRecompile)
{
    constexpr std::size_t CAPACITY = 600;

    auto marks = runtime().buffer<std::uint32_t>(CAPACITY).value();
    for (std::size_t i = 0; i < CAPACITY; ++i)
        marks[i] = K_CANARY;

    std::size_t live = 0;
    const LateCount width = LateCount::from_host(CAPACITY, [&live]() { return live; });

    auto g = runtime().graph();
    record_marking_rule(g.recorder(), marks, width, LateCount::fixed(7));

    for (const std::size_t n : {std::size_t{10}, std::size_t{0}, CAPACITY, std::size_t{257}})
    {
        live = n;
        g.run().value();
    }

    EXPECT_EQ(g.compile_count(), 1u);
}

/** @brief Tests that a launch with a null live width runs every item up to its capacity. */
TEST_F(Small_DeviceSizedLaunch, ANullLiveWidthRunsEveryItem)
{
    constexpr std::size_t CAPACITY = 300;

    auto plain = runtime().buffer<std::uint32_t>(CAPACITY).value();
    for (std::size_t i = 0; i < CAPACITY; ++i)
        plain[i] = K_CANARY;
    std::uint32_t* p = plain.data();

    auto g = runtime().graph();
    g.recorder().kernel(DeviceSized{CAPACITY, nullptr, nullptr},
                        Access(Reads(), Writes(plain)),
                        [p](std::size_t t) { p[t] = static_cast<std::uint32_t>(t); });
    g.run().value();

    for (std::size_t t = 0; t < CAPACITY; ++t)
        ASSERT_EQ(plain[t], t) << "item " << t;
}

/** @brief Tests that a kernel taking a live word is passed the live words, while one that does not take it is not. */
TEST_F(Small_DeviceSizedLaunch, TheKernelArityPicksTheLiveWords)
{
    constexpr std::size_t CAPACITY = 300;
    constexpr std::size_t LIVE = 123;

    auto worded = runtime().buffer<std::uint32_t>(CAPACITY).value();
    for (std::size_t i = 0; i < CAPACITY; ++i)
        worded[i] = K_CANARY;

    const LateCount count = LateCount::from_host(CAPACITY, []() { return LIVE; });

    auto g = runtime().graph();
    Recorder& recorder = g.recorder();
    std::uint32_t* word =
        recorder.scratch<std::uint32_t>(1, worded.device_index(), worded.residency()).value();
    const BufferRegion word_region{word, 0, sizeof(std::uint32_t)};
    std::uint32_t* w = worded.data();

    recorder.resolve(std::array<LateCount, 1>{count}, worded.device_index(),
                     Access(Reads(), Writes(word_region)),
                     [word](const std::array<std::size_t, 1>& live)
                     {
                         word[0] = static_cast<std::uint32_t>(live[0]);
                     });
    recorder.kernel(DeviceSized{CAPACITY, nullptr, word},
                    Access(Reads(word_region), Writes(worded)),
                    [w](std::size_t t, std::size_t live)
                    {
                        w[t] = static_cast<std::uint32_t>(t + live);
                    });
    g.run().value();

    for (std::size_t t = 0; t < CAPACITY; ++t)
        ASSERT_EQ(worded[t], t + LIVE) << "item " << t;
}

/** @brief Tests that a launch of capacity zero runs no item. */
TEST_F(Small_DeviceSizedLaunch, ZeroCapacityLaunchRunsNothing)
{
    auto target = runtime().buffer<std::uint32_t>(1).value();
    target[0] = K_CANARY;
    std::uint32_t* t = target.data();

    auto g = runtime().graph();
    g.recorder().kernel(DeviceSized{0, nullptr, nullptr},
                        Access(Reads(), Writes(target)),
                        [t](std::size_t item) { t[item] = 0u; });

    const auto report = g.run().value();

    EXPECT_TRUE(report.is_successful);
    EXPECT_EQ(target[0], K_CANARY);
}

/** @brief Tests that the successor of a launch of capacity zero still runs. */
TEST_F(Small_DeviceSizedLaunch, ZeroCapacityLaunchReleasesItsSuccessor)
{
    auto target = runtime().buffer<std::uint32_t>(1).value();
    auto done   = runtime().buffer<std::uint32_t>(1).value();
    target[0] = K_CANARY;
    done[0] = 0;
    std::uint32_t* t = target.data();
    std::uint32_t* d = done.data();

    auto g = runtime().graph();
    g.recorder().kernel(DeviceSized{0, nullptr, nullptr},
                        Access(Reads(), Writes(target)),
                        [t](std::size_t item) { t[item] = 0u; });
    g.add(std::size_t{1}, Reads(target), Writes(done), [t, d](std::size_t) { d[0] = t[0]; });

    g.run().value();

    EXPECT_EQ(done[0], K_CANARY) << "the successor must run after the empty launch";
}

/** @brief Tests that a device-sized rule leaves the bytes the host model of the marking rule gives, as its counts change. */
TEST_P(Medium_DeviceSizedLayers, DeviceSizedRuleMatchesTheHostModelAsTheCountsChange)
{
    constexpr std::size_t CAPACITY = 4096;
    constexpr std::size_t SALT = 7;

    Harness hx(runtime(), CAPACITY);
    auto marks = runtime().buffer<std::uint32_t>(CAPACITY).value();
    for (std::size_t i = 0; i < CAPACITY; ++i)
        marks[i] = static_cast<std::uint32_t>(i);

    auto g = runtime().graph();
    hx.add_producer(g);
    record_marking_rule(g.recorder(), marks, device_count(hx.counter, CAPACITY),
                        LateCount::fixed(SALT));
    hx.add_digest(g, marks, CAPACITY);
    const std::vector<std::uint32_t> measured = hx.record(g);

    const std::vector<std::uint32_t> schedule = schedule_for(CAPACITY);
    std::vector<std::uint32_t> model(CAPACITY);
    for (std::size_t i = 0; i < CAPACITY; ++i)
        model[i] = static_cast<std::uint32_t>(i);
    std::vector<std::uint32_t> expected(K_STEPS);
    for (std::size_t step = 0; step < K_STEPS; ++step)
    {
        const std::size_t width = schedule[step % schedule.size()];
        for (std::size_t t = 0; t < width; ++t)
            model[t] = marked(model[t], t, width, SALT);
        expected[step] = digest_of(model, CAPACITY);
    }

    expect_digests_change(expected);
    EXPECT_EQ(measured, expected);
}

/** @brief Tests that a sized_from_device task leaves the bytes the host model gives as its count changes. */
TEST_F(Small_SizedFromDevice, SizedFromDeviceMatchesTheHostModelAsTheCountChanges)
{
    constexpr std::size_t CAPACITY = 4096;

    Harness hx(runtime(), CAPACITY);
    auto acc = runtime().buffer<std::uint32_t>(CAPACITY).value();
    for (std::size_t i = 0; i < CAPACITY; ++i)
        acc[i] = static_cast<std::uint32_t>(i);
    std::uint32_t* a = acc.data();

    auto g = runtime().graph();
    hx.add_producer(g);
    g.add_dynamic(sized_from_device(hx.counter), CAPACITY, Reads{}, Writes(acc),
                  [a](std::size_t i) { a[i] = a[i] * 2654435761u + 12345u; });
    hx.add_digest(g, acc, CAPACITY);
    const std::vector<std::uint32_t> measured = hx.record(g);

    const std::vector<std::uint32_t> schedule = schedule_for(CAPACITY);
    std::vector<std::uint32_t> model(CAPACITY);
    for (std::size_t i = 0; i < CAPACITY; ++i)
        model[i] = static_cast<std::uint32_t>(i);
    std::vector<std::uint32_t> expected(K_STEPS);
    for (std::size_t step = 0; step < K_STEPS; ++step)
    {
        const std::size_t count = schedule[step % schedule.size()];
        for (std::size_t i = 0; i < count; ++i)
            model[i] = model[i] * 2654435761u + 12345u;
        expected[step] = digest_of(model, CAPACITY);
    }

    expect_digests_change(expected);
    EXPECT_EQ(measured, expected);
}

INSTANTIATE_TEST_SUITE_P(Rows, Medium_DeviceSizedLayers, ::testing::Values(false, true));

/** @brief Tests that a device count over its capacity fails the run. */
TEST_F(Small_DeviceSizedLaunch, DeviceCountOverCapacityFailsTheRun)
{
    constexpr std::size_t CAPACITY = 100;

    auto counter = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto marks = runtime().buffer<std::uint32_t>(CAPACITY).value();
    for (std::size_t i = 0; i < CAPACITY; ++i)
        marks[i] = 0;
    std::uint32_t* c = counter.data();

    auto g = runtime().graph();
    g.add(std::size_t{1}, Reads{}, Writes(counter),
          [c](std::size_t) { c[0] = static_cast<std::uint32_t>(CAPACITY) + 1u; });
    record_marking_rule(g.recorder(), marks, device_count(counter, CAPACITY), fixed_count(1));
    EXPECT_FALSE(g.run(4).has_value());
}

/** @brief Tests that a graph with a count inside the capacity runs clean after a run failed for a count over it. */
TEST_F(Small_DeviceSizedLaunch, AGraphRunsCleanAfterARunFailedForACountOverTheCapacity)
{
    constexpr std::size_t CAPACITY = 100;

    auto counter = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto marks = runtime().buffer<std::uint32_t>(CAPACITY).value();
    for (std::size_t i = 0; i < CAPACITY; ++i)
        marks[i] = 0;
    std::uint32_t* c = counter.data();

    auto g = runtime().graph();
    g.add(std::size_t{1}, Reads{}, Writes(counter),
          [c](std::size_t) { c[0] = static_cast<std::uint32_t>(CAPACITY) + 1u; });
    record_marking_rule(g.recorder(), marks, device_count(counter, CAPACITY), fixed_count(1));
    ASSERT_FALSE(g.run(4).has_value());

    auto valid = runtime().graph();
    valid.add(std::size_t{1}, Reads{}, Writes(counter), [c](std::size_t) { c[0] = 3u; });
    record_marking_rule(valid.recorder(), marks, device_count(counter, CAPACITY), fixed_count(1));
    const auto report = valid.run(2).value();
    EXPECT_TRUE(report.is_successful);
}
