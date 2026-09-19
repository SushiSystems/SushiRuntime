/**************************************************************************/
/* test_device_sized_primitives.cpp                                       */
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

/**
 * @brief Covers P3 of docs/superpowers/specs/2026-09-19-graph-replay-design.md (§D.2, §G).
 *
 * Every step a producer kernel writes a new live count, 0 and the capacity
 * included; the scan, counting sort, reductions and a sized_from_device task
 * size themselves from it on the device. The chain and the forced scheduler
 * must record the same per-step digests bit for bit.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t STEPS = 48;

    /** @brief Holds one run's per-step digests and the layer it took. */
    struct Recording
    {
        std::vector<std::uint32_t> digests;                           /**< One word per step. */
        Core::ExecutionLayer layer = Core::ExecutionLayer::Scheduler; /**< Layer the run took. */
    };

    /**
     * @brief Builds a live-count schedule over @p capacity that hits 0 and @p capacity.
     * @param capacity The capacity the counts must not exceed.
     * @return Eleven counts, cycled by the producer.
     */
    std::vector<std::uint32_t> schedule_for(std::size_t capacity)
    {
        const auto cap = static_cast<std::uint32_t>(capacity);
        const auto clamp = [cap](std::uint32_t v) { return v > cap ? cap : v; };
        return {cap, 0u, clamp(1u), cap / 2u + (cap > 1u ? 1u : 0u), cap, 0u, cap / 3u,
                cap > 0u ? cap - 1u : 0u, clamp(cap / 7u + 3u), cap, clamp(2u)};
    }

    /**
     * @brief Hosts the step counter, the live count and the digest history of one graph.
     */
    struct Harness
    {
        API::Buffer<std::uint32_t> counter;  /**< Residency::Host live count the producer writes. */
        API::Buffer<std::uint32_t> step;     /**< Steps the producer has run. */
        API::Buffer<std::uint32_t> schedule; /**< Live count per step, cycled. */
        API::Buffer<std::uint32_t> history;  /**< One digest per step. */

        /**
         * @brief Allocates the shared buffers on @p rt for a schedule over @p capacity.
         */
        Harness(Runtime& rt, std::size_t capacity)
            : counter(rt.buffer<std::uint32_t>(1, API::DeviceIndex{0}, API::Residency::Host)),
              step(rt.buffer<std::uint32_t>(1)),
              schedule(rt.buffer<std::uint32_t>(schedule_for(capacity).size())),
              history(rt.buffer<std::uint32_t>(STEPS))
        {
            const std::vector<std::uint32_t> s = schedule_for(capacity);
            for (std::size_t i = 0; i < s.size(); ++i)
                schedule[i] = s[i];
            counter[0] = 0;
            step[0] = 0;
            for (std::size_t i = 0; i < STEPS; ++i)
                history[i] = 0;
        }

        /**
         * @brief Adds the kernel that advances the step and writes this step's live count.
         */
        void add_producer(API::Graph& g)
        {
            std::uint32_t* c = counter.data();
            std::uint32_t* st = step.data();
            const std::uint32_t* sc = schedule.data();
            const auto period = static_cast<std::uint32_t>(schedule.size());
            g.add(Reads(schedule), Writes(counter, step), std::size_t{1},
                  [c, st, sc, period](std::size_t)
                  {
                      const std::uint32_t s = st[0];
                      c[0] = sc[s % period];
                      st[0] = s + 1u;
                  });
        }

        /**
         * @brief Adds the kernel that folds @p words words of @p source into this step's digest.
         */
        void add_digest(API::Graph& g, const API::Buffer<std::uint32_t>& source, std::size_t words)
        {
            const std::uint32_t* src = source.data();
            const std::uint32_t* st = step.data();
            std::uint32_t* h = history.data();
            g.add(Reads(source, step), Writes(history), std::size_t{1},
                  [src, st, h, words](std::size_t)
                  {
                      std::uint32_t acc = 2166136261u;
                      for (std::size_t i = 0; i < words; ++i)
                          acc = (acc ^ src[i]) * 16777619u;
                      h[st[0] - 1u] = acc;
                  });
        }

        /**
         * @brief Runs @p g for STEPS steps and copies the digests out.
         */
        Recording record(API::Graph& g)
        {
            Recording r;
            const auto report = g.run(STEPS);
            EXPECT_TRUE(report.is_successful);
            r.layer = report.layer;
            r.digests.assign(history.data(), history.data() + STEPS);
            return r;
        }
    };

    /**
     * @brief Returns a LateCount the kernels read from @p counter on the device.
     */
    LateCount device_count(const API::Buffer<std::uint32_t>& counter, std::size_t capacity)
    {
        LateCount lc;
        lc.capacity = capacity;
        lc.device = &counter;
        return lc;
    }

    /**
     * @brief Runs @p workload on a default and a forced-scheduler runtime and compares them.
     *
     * The default run must take the chain, the forced one the scheduler, and
     * every step's digest must match bit for bit.
     */
    void expect_chain_matches_scheduler(const std::function<Recording(Runtime&)>& workload)
    {
        auto chained_rt = Runtime::create();
        RuntimeConfig forced;
        forced.force_scheduler = true;
        auto scheduled_rt = Runtime::create({}, forced);

        const Recording chained = workload(chained_rt);
        const Recording scheduled = workload(scheduled_rt);

        EXPECT_EQ(chained.layer, Core::ExecutionLayer::Chain)
            << "a plan of device-sized primitives and kernels must take the chain";
        EXPECT_EQ(scheduled.layer, Core::ExecutionLayer::Scheduler);
        ASSERT_EQ(chained.digests.size(), scheduled.digests.size());
        EXPECT_EQ(std::memcmp(chained.digests.data(), scheduled.digests.data(),
                              chained.digests.size() * sizeof(std::uint32_t)), 0);

        std::size_t changes = 0;
        for (std::size_t i = 1; i < chained.digests.size(); ++i)
            changes += (chained.digests[i] != chained.digests[i - 1]) ? 1u : 0u;
        EXPECT_GT(changes, STEPS / 4) << "the live count must change the results step to step";
    }
} // namespace

TEST(Integration_DeviceSizedPrimitives, ReduceChainMatchesSchedulerAsTheCountChanges)
{
    constexpr std::size_t CAPACITY = 70000;
    expect_chain_matches_scheduler([](Runtime& rt)
    {
        Harness hx(rt, CAPACITY);
        auto values = rt.buffer<float>(CAPACITY);
        auto result = rt.buffer<float>(1);
        auto bits = rt.buffer<std::uint32_t>(1);
        for (std::size_t i = 0; i < CAPACITY; ++i)
            values[i] = static_cast<float>((i * 7919u) % 1013u) * 0.001f + 0.1f;

        auto g = rt.graph();
        hx.add_producer(g);
        g.add_reduce(values, result, device_count(hx.counter, CAPACITY), API::Sum<float>{});
        const float* r = result.data();
        std::uint32_t* b = bits.data();
        // The float result reaches the digest as its bit pattern.
        g.add(Reads(result), Writes(bits), std::size_t{1},
              [r, b](std::size_t)
              {
                  std::uint32_t word = 0;
                  std::memcpy(&word, r, sizeof(word));
                  b[0] = word;
              });
        hx.add_digest(g, bits, 1);
        return hx.record(g);
    });
}

TEST(Integration_DeviceSizedPrimitives, SegmentedReduceChainMatchesSchedulerAsTheCountChanges)
{
    constexpr std::size_t SEGMENTS = 500;
    expect_chain_matches_scheduler([](Runtime& rt)
    {
        Harness hx(rt, SEGMENTS);
        auto offsets = rt.buffer<std::uint32_t>(SEGMENTS + 1);
        std::uint32_t at = 0;
        for (std::size_t s = 0; s <= SEGMENTS; ++s)
        {
            offsets[s] = at;
            at += static_cast<std::uint32_t>(s % 5);
        }
        auto values = rt.buffer<std::uint32_t>(at);
        for (std::size_t i = 0; i < at; ++i)
            values[i] = static_cast<std::uint32_t>(i) * 2654435761u;
        auto out = rt.buffer<std::uint32_t>(SEGMENTS);
        for (std::size_t s = 0; s < SEGMENTS; ++s)
            out[s] = 0;

        auto g = rt.graph();
        hx.add_producer(g);
        g.add_segmented_reduce(values, offsets, out, device_count(hx.counter, SEGMENTS),
                               API::Sum<std::uint32_t>{});
        // A segment past the live count keeps a previous step's fold, so later steps differ.
        std::uint32_t* o = out.data();
        g.add(Reads(out), Writes(out), SEGMENTS,
              [o](std::size_t s) { o[s] = o[s] * 3u + static_cast<std::uint32_t>(s); });
        hx.add_digest(g, out, SEGMENTS);
        return hx.record(g);
    });
}

TEST(Integration_DeviceSizedPrimitives, PrefixScanChainMatchesSchedulerAsTheCountChanges)
{
    constexpr std::size_t CAPACITY = 70000;
    expect_chain_matches_scheduler([](Runtime& rt)
    {
        Harness hx(rt, CAPACITY);
        auto counts = rt.buffer<std::uint32_t>(CAPACITY);
        auto offsets = rt.buffer<std::uint32_t>(CAPACITY + 1);
        auto total = rt.buffer<std::uint32_t>(1);
        for (std::size_t i = 0; i < CAPACITY; ++i)
            counts[i] = static_cast<std::uint32_t>((i * 31u) % 9u);
        for (std::size_t i = 0; i <= CAPACITY; ++i)
            offsets[i] = 0;

        auto g = rt.graph();
        hx.add_producer(g);
        g.add_prefix_scan(counts, offsets, total, device_count(hx.counter, CAPACITY));
        hx.add_digest(g, offsets, CAPACITY + 1);
        return hx.record(g);
    });
}

TEST(Integration_DeviceSizedPrimitives, CountingSortChainMatchesSchedulerAsTheCountsChange)
{
    constexpr std::size_t N_CAPACITY = 1000;
    constexpr std::size_t KEY_CAPACITY = std::size_t{1} << 17;
    constexpr std::size_t BUCKETS = 32;
    constexpr std::size_t PACKED = N_CAPACITY + BUCKETS + 2;
    expect_chain_matches_scheduler([](Runtime& rt)
    {
        Harness hx(rt, N_CAPACITY);
        auto keys = rt.buffer<std::uint32_t>(N_CAPACITY);
        auto indices = rt.buffer<std::uint32_t>(N_CAPACITY);
        auto offsets = rt.buffer<std::uint32_t>(KEY_CAPACITY + 1);
        auto total = rt.buffer<std::uint32_t>(1);
        auto packed = rt.buffer<std::uint32_t>(PACKED);
        for (std::size_t i = 0; i < N_CAPACITY; ++i)
        {
            keys[i] = static_cast<std::uint32_t>((i * 7u + i / 13u) % BUCKETS);
            indices[i] = 0;
        }

        // The live key range changes every step on the host, so one, two and three digits all run.
        const std::array<std::size_t, 4> ranges{BUCKETS, 300, 70000, KEY_CAPACITY};
        auto tick = std::make_shared<std::size_t>(0);
        LateCount key_range;
        key_range.capacity = KEY_CAPACITY;
        key_range.host = [ranges, tick] { return ranges[(*tick)++ % ranges.size()]; };

        auto g = rt.graph();
        hx.add_producer(g);
        g.add_counting_sort(keys, indices, offsets, total,
                            device_count(hx.counter, N_CAPACITY), key_range);

        const std::uint32_t* ip = indices.data();
        const std::uint32_t* op = offsets.data();
        const std::uint32_t* tp = total.data();
        std::uint32_t* pp = packed.data();
        g.add(Reads(indices, offsets, total), Writes(packed), PACKED,
              [ip, op, tp, pp](std::size_t i)
              {
                  if (i < N_CAPACITY)
                      pp[i] = ip[i];
                  else if (i < N_CAPACITY + BUCKETS + 1)
                      pp[i] = op[i - N_CAPACITY];
                  else
                      pp[i] = tp[0];
              });
        hx.add_digest(g, packed, PACKED);
        return hx.record(g);
    });
}

TEST(Integration_DeviceSizedPrimitives, SizedFromDeviceChainMatchesSchedulerAsTheCountChanges)
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
        g.add(API::sized_from_device(hx.counter), Reads{}, Writes(acc), CAPACITY,
              [a](std::size_t i) { a[i] = a[i] * 2654435761u + 12345u; });
        hx.add_digest(g, acc, CAPACITY);
        return hx.record(g);
    });
}

TEST(Integration_DeviceSizedPrimitives, DeviceCountOverCapacityFailsAChainedRun)
{
    constexpr std::size_t CAPACITY = 100;

    auto rt = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1, API::DeviceIndex{0}, API::Residency::Host);
    auto counts = rt.buffer<std::uint32_t>(CAPACITY);
    auto offsets = rt.buffer<std::uint32_t>(CAPACITY + 1);
    auto total = rt.buffer<std::uint32_t>(1);
    std::uint32_t* c = counter.data();

    auto g = rt.graph();
    g.add(Reads{}, Writes(counter), std::size_t{1},
          [c](std::size_t) { c[0] = static_cast<std::uint32_t>(CAPACITY) + 1u; });
    g.add_prefix_scan(counts, offsets, total, device_count(counter, CAPACITY));
    EXPECT_THROW(g.run(4), SushiException);

    // A later graph with a count inside the capacity runs clean on the same runtime.
    auto valid = rt.graph();
    valid.add(Reads{}, Writes(counter), std::size_t{1}, [c](std::size_t) { c[0] = 3u; });
    valid.add_prefix_scan(counts, offsets, total, device_count(counter, CAPACITY));
    const auto report = valid.run(2);
    EXPECT_TRUE(report.is_successful);
    EXPECT_EQ(report.layer, Core::ExecutionLayer::Chain);
}
