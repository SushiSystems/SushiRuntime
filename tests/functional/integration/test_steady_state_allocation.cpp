/****************************************************************/
/* test_steady_state_allocation.cpp                             */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// Makes the steady-state allocation cost of a tick measurable, and then holds
// it to a shape rather than to a number.
//
// A frame loop runs the same compiled plan thousands of times. Anything it
// allocates per tick is a malloc on the simulation thread's critical path and a
// slow drift toward heap fragmentation, and neither shows up in a throughput
// benchmark that runs the graph once over a large input. The counter below is a
// replacement global operator new, gated by a THREAD-LOCAL flag so it observes
// only the thread driving the run: worker threads allocate for reasons of their
// own (event lists, SYCL submissions) and their noise would drown the signal.
//
// What is asserted is deliberately not "zero", and on Windows it cannot be:
// SushiRuntime is a shared library, so this file's replacement operator new
// only sees allocations made by code that is itself compiled into this test
// executable, never code compiled into the DLL. The command-group functions
// handed to sycl::handler::parallel_for are templates instantiated wherever
// they are written — inline in SushiRuntime/api/graph/graph.hpp, so their
// object code lands in whichever translation unit includes that header — and
// SYCL's own StoreLambda() heap-allocates the stored kernel object on every
// submission (llvm-sycl/include/sycl/handler.hpp, MHostKernel.reset(new
// detail::HostKernel<...>(...))). The scheduler runs that submission from a
// worker thread, so the counter's thread-local flag never sees it; the chain
// runs it synchronously from the calling thread (src/execution/chain_run.cpp,
// ChainRun::submit_node()), so it does. The true rule is therefore not "the
// chain allocates nothing per tick" — it never could, once SYCL's own kernel
// storage is inline — but "the chain adds no more allocation than SYCL's own
// submission of the same kernel already costs on this thread", which the
// baseline below measures directly instead of assuming.
//
// Four relative claims matter, and they are the ones asserted:
//
//   1. The value-returning run() costs at least one more allocation per tick
//      than the reuse form. That is the RunReport itself, and it makes the
//      counter demonstrably able to see the path it is measuring.
//   2. The reuse form's per-tick growth (four times the ticks minus one times
//      the ticks) is no more than a plain sycl::queue's per-tick growth
//      submitting the identical kernel, in order, on the same device and
//      context from this thread. Anything the chain adds beyond bare SYCL is
//      the runtime's own cost, and that is what must not scale.
//   3. and 4. The same "does not scale with ticks" claim the reuse form used
//      to carry, unchanged, on a run forced onto the scheduler
//      (RuntimeSettings::force_scheduler): there the submission runs on a
//      worker thread, so this file's counter cannot see it at all, and the
//      count must stay flat because it stays zero.
//
// The raw per-tick figures — chain, baseline, and scheduler-forced — are
// recorded as test properties, so every number is visible even where it
// cannot be asserted.

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

// Detect a build whose sanitizer runtime owns the allocator. Both spellings are
// checked for each sanitizer because Clang and IntelLLVM do not agree on which
// they define: the __has_feature form is the Clang-native one, the __SANITIZE_*
// macros are the GCC-style predefined ones, and a given toolchain may offer only
// one of them. Testing a single spelling is how the ThreadSanitizer lane slipped
// through -- the guard below existed, but only for AddressSanitizer.
// tests/common/test_main.cpp uses the same idiom for SR_TSAN_BUILD.
#if defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#    define SR_SANITIZER_OWNS_ALLOCATOR 1
#  endif
#endif
#if !defined(SR_SANITIZER_OWNS_ALLOCATOR)
#  if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#    define SR_SANITIZER_OWNS_ALLOCATOR 1
#  endif
#endif

#if defined(SR_SANITIZER_OWNS_ALLOCATOR)

// AddressSanitizer and ThreadSanitizer each replace operator new/delete
// themselves -- ASan to place its redzones and track ownership, TSan to attach
// its own bookkeeping. A second replacement in this translation unit is not a
// measurement — it is a duplicate definition of the same symbols, which fails at
// link: on Windows against ASan, and on Linux against libclang_rt.tsan_cxx.a.
//
// The suite is skipped rather than adapted. Counting could be kept alive under a
// sanitizer by installing allocator hooks instead of replacing operator new, but
// the numbers would then include the sanitizer's own allocations, and this suite
// asserts on exact steady-state counts. A test that runs while measuring
// something other than what it claims is worse than one that says it was skipped.
// The target itself stays in the sanitizer lanes: it still compiles, links and
// runs there, which is what keeps this guard honest about what it costs.
TEST(Integration_SteadyStateAllocation, SkippedUnderSanitizer)
{
    GTEST_SKIP() << "This sanitizer owns operator new; the allocation counter "
                    "cannot coexist with it. Run this suite in an ordinary build.";
}

#else

namespace
{
    // Constant-initialized, so these are live before any dynamic initialization
    // that might allocate. The flag is per-thread; the counter is shared only
    // because the counting thread is the only one that ever touches it.
    thread_local bool g_counting_this_thread = false;
    std::atomic<std::size_t> g_allocation_count{0};

    // Counts an allocation when the calling thread asked to be observed.
    inline void note_allocation() noexcept
    {
        if (g_counting_this_thread)
            g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }

    constexpr std::size_t N            = 4096;
    constexpr std::size_t WARMUP_TICKS = 8;
    constexpr std::size_t TICKS        = 64;
}

// Every replaced form is paired with the matching deallocation form, so nothing
// allocated here can be freed by a different allocator. The over-aligned
// (std::align_val_t) forms are deliberately NOT replaced: leaving the whole
// aligned family at its default keeps that pairing intact too, at the cost of
// not counting aligned allocations — which the run path does not make.
void* operator new(std::size_t size)
{
    note_allocation();
    void* p = std::malloc(size != 0 ? size : 1);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t size)
{
    note_allocation();
    void* p = std::malloc(size != 0 ? size : 1);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept              { std::free(p); }
void operator delete[](void* p) noexcept            { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace
{
    // Runs @p ticks steps and returns how many allocations the calling thread
    // made doing it. The graph is warmed first so compilation and first-run
    // capacity growth land outside the measured window — the question is what a
    // tick costs in the steady state, not what the first one costs.
    std::size_t allocations_for_reuse_ticks(API::Graph& g, std::size_t ticks)
    {
        Core::RunReport report;
        for (std::size_t i = 0; i < WARMUP_TICKS; ++i)
            g.run(report);

        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting_this_thread = true;
        for (std::size_t i = 0; i < ticks; ++i)
            g.run(report);
        g_counting_this_thread = false;

        return g_allocation_count.load(std::memory_order_relaxed);
    }

    // The same, through the value-returning run() — one fresh RunReport per tick.
    std::size_t allocations_for_value_ticks(API::Graph& g, std::size_t ticks)
    {
        for (std::size_t i = 0; i < WARMUP_TICKS; ++i)
            g.run();

        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting_this_thread = true;
        for (std::size_t i = 0; i < ticks; ++i)
        {
            const Core::RunReport report = g.run();
            (void)report;
        }
        g_counting_this_thread = false;

        return g_allocation_count.load(std::memory_order_relaxed);
    }

    // What bare SYCL costs, on this thread, to submit the same per-element kernel
    // @p ticks times, in order, on @p queue — the figure the chain's own cost is
    // judged against. @p queue must be an in-order queue on the plan's device, so
    // this measures the same submission path graph.hpp's parallel_for takes,
    // without the chain's own bookkeeping (dependency waits, the event ledger)
    // around it. Warmed up the same way and for the same reason as the reuse
    // form above: the first submission's one-time cost is not the steady state.
    template <class F>
    std::size_t allocations_for_baseline_ticks(sycl::queue& queue, std::size_t n, F fn,
                                               std::size_t ticks)
    {
        const auto submit_once = [&]
        {
            queue.submit([&](sycl::handler& h)
            {
                h.parallel_for(sycl::range<1>(n), [fn](sycl::id<1> idx) { fn(idx[0]); });
            });
            queue.wait();
        };

        for (std::size_t i = 0; i < WARMUP_TICKS; ++i)
            submit_once();

        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting_this_thread = true;
        for (std::size_t i = 0; i < ticks; ++i)
            submit_once();
        g_counting_this_thread = false;

        return g_allocation_count.load(std::memory_order_relaxed);
    }
}

// The counter must be able to see the thing it is measuring before any claim
// made with it means anything. A single vector allocation is unmissable.
TEST(Integration_SteadyStateAllocation, TheCounterObservesTheCallingThread)
{
    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting_this_thread = true;
    {
        std::string forced(256, 'x');
        EXPECT_FALSE(forced.empty());
    }
    g_counting_this_thread = false;

    EXPECT_GT(g_allocation_count.load(std::memory_order_relaxed), 0u)
        << "the replacement operator new is not being called; every allocation "
           "claim in this file would pass vacuously";
}

// The four properties the header comment describes.
TEST(Integration_SteadyStateAllocation, ReuseTicksDoNotAllocatePerTick)
{
    const auto kernel = [](float* p) { return [p](std::size_t i) { p[i] = static_cast<float>(i) * 0.5f; }; };

    auto rt  = Runtime::create();               // rebalancer off, profiling off, chain path
    auto buf = rt.buffer<float>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0.0f;

    float* p = buf.data();
    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N, kernel(p));

    const std::size_t reuse_short = allocations_for_reuse_ticks(g, TICKS);
    const std::size_t reuse_long  = allocations_for_reuse_ticks(g, TICKS * 4);
    const std::size_t value_short = allocations_for_value_ticks(g, TICKS);

    // Same device, same context, same in-order queue the chain itself submits
    // to (Execution::RuntimeContext::chain_queue()) — so this measures what
    // SYCL's own submission of the identical kernel costs on this thread, with
    // none of the chain's own bookkeeping around it.
    sycl::queue& baseline_queue = rt.unsafe_context().chain_queue(0, /*profiling=*/false);
    const std::size_t baseline_short = allocations_for_baseline_ticks(baseline_queue, N, kernel(p), TICKS);
    const std::size_t baseline_long  = allocations_for_baseline_ticks(baseline_queue, N, kernel(p), TICKS * 4);

    RecordProperty("reuse_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(reuse_short));
    RecordProperty("reuse_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(reuse_long));
    RecordProperty("value_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(value_short));
    RecordProperty("baseline_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(baseline_short));
    RecordProperty("baseline_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(baseline_long));

    // 1. The report the value form returns is built fresh every tick; the reuse
    //    form refills one the caller owns. The difference is at least one
    //    allocation per tick, and observing it proves the counter reaches the
    //    run path rather than only the test's own code.
    EXPECT_GE(value_short, reuse_short + TICKS)
        << "the value-returning run() did not cost a per-tick allocation more "
           "than run(report); either the counter does not see the run path or "
           "the reuse overload is not actually reusing";

    // 2. The chain's own per-tick growth must not exceed bare SYCL's per-tick
    //    growth submitting the same kernel on the same queue. Growth beyond
    //    that is the runtime's own allocation, not SYCL's, and that is the
    //    only piece the chain controls.
    const auto chain_growth    = static_cast<std::ptrdiff_t>(reuse_long) - static_cast<std::ptrdiff_t>(reuse_short);
    const auto baseline_growth = static_cast<std::ptrdiff_t>(baseline_long) - static_cast<std::ptrdiff_t>(baseline_short);
    EXPECT_LE(chain_growth, baseline_growth)
        << "the chain's per-tick allocation growth (" << reuse_short << " -> " << reuse_long
        << ") exceeds bare SYCL's growth submitting the same kernel on the same queue ("
        << baseline_short << " -> " << baseline_long << ")";

    // 3. The scheduler runs the same kernel from a worker thread, so this
    //    file's counter cannot see any of it. The "does not scale" claim the
    //    reuse form used to carry unconditionally still holds here, because it
    //    now holds trivially — the count stays flat because it stays zero.
    API::RuntimeConfig forced;
    forced.force_scheduler = true;
    auto scheduled_rt  = Runtime::create({}, forced);
    auto scheduled_buf = scheduled_rt.buffer<float>(N);
    for (std::size_t i = 0; i < N; ++i)
        scheduled_buf[i] = 0.0f;

    auto sg = scheduled_rt.graph();
    sg.add(API::Reads{}, API::Writes(scheduled_buf), N, kernel(scheduled_buf.data()));

    const std::size_t scheduler_short = allocations_for_reuse_ticks(sg, TICKS);
    const std::size_t scheduler_long  = allocations_for_reuse_ticks(sg, TICKS * 4);

    RecordProperty("scheduler_reuse_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(scheduler_short));
    RecordProperty("scheduler_reuse_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(scheduler_long));

    EXPECT_TRUE(scheduler_long == 0 || scheduler_long < 4 * scheduler_short)
        << "a scheduler-forced run's steady-state allocation scales with tick count: "
        << scheduler_short << " over " << TICKS << " ticks, " << scheduler_long
        << " over " << (TICKS * 4);
}

// The same claim on the asynchronous path, which is the one an overlapped frame
// loop actually uses: submit, do unrelated work, settle into a report it owns.
TEST(Integration_SteadyStateAllocation, AsyncWaitIntoAReusedReportIsAmortised)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<float>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0.0f;

    float* p = buf.data();
    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N,
          [p](std::size_t i) { p[i] = static_cast<float>(i) * 0.25f; });

    Core::RunReport report;
    for (std::size_t i = 0; i < WARMUP_TICKS; ++i)
    {
        auto step = g.run_async();
        step.wait(report);
    }

    const auto measure = [&](std::size_t ticks)
    {
        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting_this_thread = true;
        for (std::size_t i = 0; i < ticks; ++i)
        {
            auto step = g.run_async();
            step.wait(report);
        }
        g_counting_this_thread = false;
        return g_allocation_count.load(std::memory_order_relaxed);
    };

    const std::size_t shorter = measure(TICKS);
    const std::size_t longer  = measure(TICKS * 4);

    RecordProperty("async_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(shorter));

    EXPECT_TRUE(longer == 0 || longer < 4 * shorter)
        << "an overlapped tick allocates per step: " << shorter << " over "
        << TICKS << " ticks, " << longer << " over " << (TICKS * 4);
}

// G1 task 5, spec §E item 3: a profiled run's event ledger allocates only for
// a new shape, never on a later run replaying the same one.
TEST(Integration_SteadyStateAllocation, ProfiledReuseTicksDoNotAllocatePerTick)
{
    const auto kernel = [](int* p) { return [p](std::size_t i) { p[i] = static_cast<int>(i); }; };

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add(API::Reads{}, API::Writes(buf), N, kernel(p));

    const std::size_t reuse_short = allocations_for_reuse_ticks(g, TICKS);
    const std::size_t reuse_long  = allocations_for_reuse_ticks(g, TICKS * 4);

    // Same profiling flavour of the same in-order queue the chain itself
    // submits to, so the baseline carries whatever profiling itself costs too.
    sycl::queue& baseline_queue = rt.unsafe_context().chain_queue(0, /*profiling=*/true);
    const std::size_t baseline_short = allocations_for_baseline_ticks(baseline_queue, N, kernel(p), TICKS);
    const std::size_t baseline_long  = allocations_for_baseline_ticks(baseline_queue, N, kernel(p), TICKS * 4);

    RecordProperty("profiled_reuse_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(reuse_short));
    RecordProperty("profiled_reuse_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(reuse_long));
    RecordProperty("profiled_baseline_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(baseline_short));
    RecordProperty("profiled_baseline_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(baseline_long));

    const auto chain_growth    = static_cast<std::ptrdiff_t>(reuse_long) - static_cast<std::ptrdiff_t>(reuse_short);
    const auto baseline_growth = static_cast<std::ptrdiff_t>(baseline_long) - static_cast<std::ptrdiff_t>(baseline_short);
    EXPECT_LE(chain_growth, baseline_growth)
        << "the profiled chain's per-tick allocation growth (" << reuse_short << " -> " << reuse_long
        << ") exceeds bare SYCL's growth submitting the same kernel on the same queue ("
        << baseline_short << " -> " << baseline_long << ")";

    API::RuntimeConfig forced;
    forced.force_scheduler = true;
    auto scheduled_rt  = Runtime::create({}, forced);
    auto scheduled_buf = scheduled_rt.buffer<int>(N);

    auto sg = scheduled_rt.graph();
    sg.set_profiling_requested(true);
    sg.add(API::Reads{}, API::Writes(scheduled_buf), N, kernel(scheduled_buf.data()));

    const std::size_t scheduler_short = allocations_for_reuse_ticks(sg, TICKS);
    const std::size_t scheduler_long  = allocations_for_reuse_ticks(sg, TICKS * 4);

    RecordProperty("profiled_scheduler_allocations_per_" + std::to_string(TICKS) + "_ticks",
                   static_cast<int>(scheduler_short));
    RecordProperty("profiled_scheduler_allocations_per_" + std::to_string(TICKS * 4) + "_ticks",
                   static_cast<int>(scheduler_long));

    EXPECT_TRUE(scheduler_long == 0 || scheduler_long < 4 * scheduler_short)
        << "a scheduler-forced profiled run's steady-state allocation scales with tick count: "
        << scheduler_short << " over " << TICKS << " ticks, " << scheduler_long << " over "
        << (TICKS * 4);
}

#endif // !defined(SR_SANITIZER_OWNS_ALLOCATOR)
