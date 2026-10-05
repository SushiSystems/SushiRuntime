/****************************************************************/
/* test_fixed_order_reduce.cpp                                  */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// Coverage for Graph::add_reduce / add_segmented_reduce and the fixed-tile plan
// underneath them. The claim under test is not "the sum is right" — a parallel
// reduce gets that too — but that the COMBINATION ORDER is a function of the
// element layout alone, so the bit pattern is reproducible.
//
// The strongest available check is a host reference that performs the identical
// fixed-tile fold and an exact bit comparison against it: if the device result
// matched some other order, the two would differ in the low bits of a float sum.
// The inputs below are built so the naive left-to-right sum differs from the
// tiled one, which is what gives that comparison teeth.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    // The host mirror of detail::fold_tile over a whole level: fold each tile of
    // at most TILE values left to right, seeded with the tile's first element.
    template <typename T>
    std::vector<T> fold_level(const std::vector<T>& in, std::size_t tile)
    {
        std::vector<T> out;
        for (std::size_t begin = 0; begin < in.size(); begin += tile)
        {
            const std::size_t end = (begin + tile < in.size()) ? begin + tile : in.size();
            T accumulator = in[begin];
            for (std::size_t j = begin + 1; j < end; ++j)
                accumulator = accumulator + in[j];
            out.push_back(accumulator);
        }
        return out;
    }

    // The value the runtime is required to produce for a sum: repeated levels of
    // fold_level until one value remains. Mirrors detail::plan_reduction.
    template <typename T>
    T fixed_order_sum(std::vector<T> values, std::size_t tile)
    {
        while (values.size() > 1)
            values = fold_level(values, tile);
        return values.front();
    }

    // A sum whose result depends on the order the values are combined in: one
    // large magnitude followed by many values small enough to vanish when added
    // to the running total but not when added to each other first.
    std::vector<float> order_sensitive_input(std::size_t n)
    {
        std::vector<float> v(n);
        v[0] = 1.0e8f;
        for (std::size_t i = 1; i < n; ++i)
            v[i] = 1.0f;
        return v;
    }

    // Exact equality on the bit pattern, so a 1-ulp difference fails.
    bool bit_equal(float a, float b)
    {
        std::uint32_t pa = 0, pb = 0;
        std::memcpy(&pa, &a, sizeof(pa));
        std::memcpy(&pb, &b, sizeof(pb));
        return pa == pb;
    }

    // As bit_equal(float, float): the double overload the byte-identity checks
    // below need, since EXPECT_DOUBLE_EQ tolerates a 4-ULP difference and the
    // late-bound contract is byte-identical, not merely close.
    bool bit_equal(double a, double b)
    {
        std::uint64_t pa = 0, pb = 0;
        std::memcpy(&pa, &a, sizeof(pa));
        std::memcpy(&pb, &b, sizeof(pb));
        return pa == pb;
    }

    constexpr std::size_t TILE = 256;
}

// The plan is a pure function of the element count: level counts divide by the
// tile size until one value remains, and consecutive levels never share scratch.
TEST(Unit_FixedOrderReduce, PlanLayoutIsPureAndDisjoint)
{
    using SushiRuntime::API::detail::plan_reduction;
    using SushiRuntime::API::detail::reduce_scratch_count;

    EXPECT_TRUE(plan_reduction(0).empty());

    const auto one = plan_reduction(1);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0].in_count, 1u);
    EXPECT_EQ(one[0].out_count, 1u);
    EXPECT_EQ(reduce_scratch_count(one), 0u) << "a single level writes the output directly";

    const auto exact = plan_reduction(TILE);
    ASSERT_EQ(exact.size(), 1u) << "exactly one tile still needs only one level";

    const auto three = plan_reduction(1000000);
    ASSERT_EQ(three.size(), 3u);
    EXPECT_EQ(three[0].in_count, 1000000u);
    EXPECT_EQ(three[0].out_count, (1000000u + TILE - 1) / TILE);
    EXPECT_EQ(three[1].in_count, three[0].out_count);
    EXPECT_EQ(three[2].out_count, 1u);

    // Level i reads exactly what level i-1 wrote, and the windows do not alias.
    for (std::size_t i = 1; i < three.size(); ++i)
        EXPECT_EQ(three[i].in_offset, three[i - 1].out_offset) << "at level " << i;
    EXPECT_NE(three[0].out_offset, three[1].out_offset);

    EXPECT_EQ(reduce_scratch_count(three), three[0].out_count + three[1].out_count);
}

// The sum matches a host fold that performs the identical tile structure, bit
// for bit — and differs from the naive left-to-right sum, which is what proves
// the comparison is actually discriminating between orders.
TEST(Unit_FixedOrderReduce, SumMatchesTheFixedOrderReferenceExactly)
{
    constexpr std::size_t N = 100000;

    auto rt  = Runtime::create();
    auto in  = rt.buffer<float>(N);
    auto out = rt.buffer<float>(1);

    const std::vector<float> host = order_sensitive_input(N);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = host[i];
    out[0] = -1.0f;

    auto g = rt.graph();
    g.add_reduce(in, out, N, Sum<float>{});
    g.run();

    const float expected = fixed_order_sum(host, TILE);
    EXPECT_TRUE(bit_equal(out[0], expected))
        << "got " << out[0] << ", fixed-order reference " << expected;

    float naive = host[0];
    for (std::size_t i = 1; i < N; ++i)
        naive = naive + host[i];
    EXPECT_FALSE(bit_equal(naive, expected))
        << "the input is not order-sensitive, so this test cannot detect a "
           "schedule-dependent combination order";
}

// Repeated runs of the same graph, and a fresh graph on a runtime running the
// background rebalancer, must all agree bit for bit. Work stealing and the
// rebalancer change which worker folds which tile; neither may change the
// result. This varies the schedule at a fixed pool size; varying the pool size
// itself is the graph-level conformance suite's job (Integration_Determinism).
TEST(Unit_FixedOrderReduce, ResultIsStableAcrossSchedules)
{
    constexpr std::size_t N = 70000;
    constexpr int ROUNDS = 32;

    const std::vector<float> host = order_sensitive_input(N);

    auto rt  = Runtime::create();
    auto in  = rt.buffer<float>(N);
    auto out = rt.buffer<float>(1);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = host[i];

    auto g = rt.graph();
    g.add_reduce(in, out, N, Sum<float>{});

    g.run();
    const float first = out[0];

    for (int round = 1; round < ROUNDS; ++round)
    {
        out[0] = 0.0f;
        g.run();
        ASSERT_TRUE(bit_equal(out[0], first)) << "round " << round;
    }
    EXPECT_EQ(g.compile_count(), 1u) << "a replayed reduction must not recompile";

    // Same input, rebalancer on: a different scheduling regime entirely, since
    // task migration reassigns tiles to workers mid-run.
    auto quiet = Runtime::create();
    quiet.rebalancer(true);
    auto qin  = quiet.buffer<float>(N);
    auto qout = quiet.buffer<float>(1);
    for (std::size_t i = 0; i < N; ++i)
        qin[i] = host[i];

    auto qg = quiet.graph();
    qg.add_reduce(qin, qout, N, Sum<float>{});
    qg.run();

    EXPECT_TRUE(bit_equal(qout[0], first))
        << "the rebalancer changed the result: the combination order is not fixed";
}

// Element counts around the tile boundaries, and the degenerate ones: a single
// value, an empty reduction (which yields the identity), and counts that land
// exactly on a level boundary.
TEST(Unit_FixedOrderReduce, EdgeCounts)
{
    const std::size_t counts[] = { 1, 2, TILE - 1, TILE, TILE + 1,
                                   TILE * TILE, TILE * TILE + 1 };

    auto rt = Runtime::create();

    for (std::size_t n : counts)
    {
        auto in  = rt.buffer<double>(n);
        auto out = rt.buffer<double>(1);
        std::vector<double> host(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            host[i] = static_cast<double>(i % 7) + 0.5;
            in[i] = host[i];
        }
        out[0] = -1.0;

        auto g = rt.graph();
        g.add_reduce(in, out, n, Sum<double>{});
        g.run();

        EXPECT_DOUBLE_EQ(out[0], fixed_order_sum(host, TILE)) << "n = " << n;
    }

    // n == 0 produces the identity without reading the input at all.
    auto in  = rt.buffer<double>(4);
    auto out = rt.buffer<double>(1);
    out[0] = 123.0;
    auto g = rt.graph();
    g.add_reduce(in, out, std::size_t{0}, Sum<double>{});
    g.run();
    EXPECT_DOUBLE_EQ(out[0], 0.0);
}

// Minimum and Maximum reduce with the same machinery; their identities must let
// any element win.
TEST(Unit_FixedOrderReduce, MinimumAndMaximum)
{
    constexpr std::size_t N = 5000;

    auto rt   = Runtime::create();
    auto in   = rt.buffer<int>(N);
    auto lo   = rt.buffer<int>(1);
    auto hi   = rt.buffer<int>(1);

    for (std::size_t i = 0; i < N; ++i)
        in[i] = static_cast<int>((i * 7919) % 10007) - 5000;

    int expected_lo = in[0];
    int expected_hi = in[0];
    for (std::size_t i = 1; i < N; ++i)
    {
        if (in[i] < expected_lo) expected_lo = in[i];
        if (in[i] > expected_hi) expected_hi = in[i];
    }

    auto g = rt.graph();
    g.add_reduce(in, lo, N, Minimum<int>{});
    g.add_reduce(in, hi, N, Maximum<int>{});
    g.run();

    EXPECT_EQ(lo[0], expected_lo);
    EXPECT_EQ(hi[0], expected_hi);
}

// A reduction is an ordinary tracked task: a consumer that reads the output is
// ordered after the whole reduction chain, not just after its first level.
TEST(Unit_FixedOrderReduce, ConsumerSeesTheFinishedResult)
{
    constexpr std::size_t N = 40000;

    auto rt    = Runtime::create();
    auto in    = rt.buffer<int>(N);
    auto total = rt.buffer<int>(1);
    auto twice = rt.buffer<int>(1);

    for (std::size_t i = 0; i < N; ++i)
        in[i] = 1;
    total[0] = 0;
    twice[0] = 0;

    int* t = total.data();
    int* d = twice.data();

    auto g = rt.graph();
    g.add_reduce(in, total, N, Sum<int>{});
    g.add(Reads(total), Writes(twice), std::size_t{1},
          [t, d](std::size_t) { d[0] = t[0] * 2; });
    g.run();

    EXPECT_EQ(total[0], static_cast<int>(N));
    EXPECT_EQ(twice[0], static_cast<int>(N) * 2);
}

// Segmented reduce over ragged segments, including an empty one, which must
// yield the identity rather than reading past its (zero-length) window.
TEST(Unit_FixedOrderReduce, SegmentedReduceOverRaggedSegments)
{
    const std::vector<std::uint32_t> offsets = { 0, 3, 3, 10, 11, 1011 };
    const std::size_t segments = offsets.size() - 1;
    const std::size_t values_n = offsets.back();

    auto rt      = Runtime::create();
    auto values  = rt.buffer<float>(values_n);
    auto bounds  = rt.buffer<std::uint32_t>(offsets.size());
    auto out     = rt.buffer<float>(segments);

    std::vector<float> host(values_n);
    for (std::size_t i = 0; i < values_n; ++i)
    {
        host[i] = static_cast<float>(i % 13) * 0.25f;
        values[i] = host[i];
    }
    for (std::size_t i = 0; i < offsets.size(); ++i)
        bounds[i] = offsets[i];
    for (std::size_t s = 0; s < segments; ++s)
        out[s] = -1.0f;

    auto g = rt.graph();
    g.add_segmented_reduce(values, bounds, out, segments, Sum<float>{});
    g.run();

    for (std::size_t s = 0; s < segments; ++s)
    {
        const std::size_t begin = offsets[s];
        const std::size_t end = offsets[s + 1];
        float expected = 0.0f;
        if (end > begin)
        {
            expected = host[begin];
            for (std::size_t j = begin + 1; j < end; ++j)
                expected = expected + host[j];
        }
        EXPECT_TRUE(bit_equal(out[s], expected))
            << "segment " << s << ": got " << out[s] << ", expected " << expected;
    }
}

// Mis-sized arguments are rejected when the task is recorded, at the call site,
// rather than faulting on the device mid-run.
TEST(Unit_FixedOrderReduce, RejectsMisSizedArguments)
{
    auto rt      = Runtime::create();
    auto in      = rt.buffer<float>(16);
    auto out     = rt.buffer<float>(1);
    auto empty   = rt.buffer<float>(0);
    auto bounds  = rt.buffer<std::uint32_t>(2);

    auto g = rt.graph();
    EXPECT_THROW(g.add_reduce(in, out, std::size_t{17}, Sum<float>{}), SushiException);
    EXPECT_THROW(g.add_reduce(in, empty, std::size_t{4}, Sum<float>{}), SushiException);
    EXPECT_THROW(g.add_segmented_reduce(in, bounds, out, std::size_t{4}, Sum<float>{}),
                 SushiException);
}

// A LateCount reduce planned for a capacity must match the fixed-count overload
// bit for bit at every live count tested, since the design promises the same
// fold order for a live n as the fixed-count path would produce for that n.
TEST(Unit_FixedOrderReduce, LateBoundMatchesFixedCountExactly)
{
    constexpr std::size_t CAPACITY = 65537;
    const std::size_t counts[] = { 1, 255, 256, 257, 65537 };

    auto rt = Runtime::create();

    for (std::size_t n : counts)
    {
        auto in  = rt.buffer<double>(CAPACITY);
        auto out_fixed = rt.buffer<double>(1);
        auto out_late  = rt.buffer<double>(1);

        std::vector<double> host(CAPACITY, 0.0);
        for (std::size_t i = 0; i < n; ++i)
        {
            host[i] = static_cast<double>(i % 7) + 0.5;
            in[i] = host[i];
        }
        out_fixed[0] = -1.0;
        out_late[0]  = -1.0;

        auto g = rt.graph();
        g.add_reduce(in, out_fixed, n, Sum<double>{});

        LateCount lc;
        lc.capacity = CAPACITY;
        lc.host = [n]() { return n; };
        g.add_reduce(in, out_late, lc, Sum<double>{});
        g.run();

        EXPECT_TRUE(bit_equal(out_late[0], out_fixed[0]))
            << "n = " << n << ": late " << out_late[0] << ", fixed " << out_fixed[0];
    }
}

// A late-bound reduce whose live count changes between runs must not recompile:
// the topology (levels, scratch, node count) is fixed at the capacity, and only
// the live count varies per step.
TEST(Unit_FixedOrderReduce, LateBoundChangingCountKeepsOneCompile)
{
    constexpr std::size_t CAPACITY = 700000;

    auto rt  = Runtime::create();
    auto in  = rt.buffer<double>(CAPACITY);
    auto out = rt.buffer<double>(1);
    for (std::size_t i = 0; i < CAPACITY; ++i)
        in[i] = 1.0;

    std::size_t live_n = 1000;
    LateCount lc;
    lc.capacity = CAPACITY;
    lc.host = [&live_n]() { return live_n; };

    auto g = rt.graph();
    g.add_reduce(in, out, lc, Sum<double>{});

    g.run();
    EXPECT_DOUBLE_EQ(out[0], 1000.0);

    live_n = 700000;
    g.run();
    EXPECT_DOUBLE_EQ(out[0], 700000.0);

    live_n = 3;
    g.run();
    EXPECT_DOUBLE_EQ(out[0], 3.0);

    EXPECT_EQ(g.compile_count(), 1u)
        << "a live count change between runs must not trigger a recompile";
}

// A LateCount above its capacity fails the run rather than reading past the
// buffers it was planned for.
TEST(Unit_FixedOrderReduce, LateBoundAboveCapacityFailsTheRun)
{
    auto rt  = Runtime::create();
    auto in  = rt.buffer<double>(100);
    auto out = rt.buffer<double>(1);

    LateCount lc;
    lc.capacity = 100;
    lc.host = []() { return std::size_t{101}; };

    auto g = rt.graph();
    g.add_reduce(in, out, lc, Sum<double>{});
    EXPECT_THROW(g.run(), SushiException);
}
