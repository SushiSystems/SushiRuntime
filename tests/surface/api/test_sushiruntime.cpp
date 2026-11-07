/**************************************************************************/
/* test_sushiruntime.cpp                                                  */
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
 * @file test_sushiruntime.cpp
 * @brief Tests that the library allocates nothing on the heap once a run reaches steady state.
 * @see tests/surface/api/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/report/run_observer.hpp>

#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

// Each sanitizer is detected by __has_feature and by its __SANITIZE_ macro; see README.md.
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

#if !defined(SR_SANITIZER_OWNS_ALLOCATOR)

namespace
{
    // Both variables are constant-initialized, live before dynamic initialization; see README.md.
    thread_local bool g_counting_this_thread = false;
    std::atomic<std::size_t> g_allocation_count{0};

    // Counts an allocation when the calling thread asked to be observed.
    inline void note_allocation() noexcept
    {
        if (g_counting_this_thread)
            g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }

    // A Windows DLL keeps its own operator new, so the counter stops at its edge; see README.md.
#if defined(_WIN32)
    constexpr bool K_COUNTER_SEES_THE_LIBRARY = false;
#else
    constexpr bool K_COUNTER_SEES_THE_LIBRARY = true;
#endif

    constexpr std::size_t N            = 4096;
    constexpr std::size_t K_WARMUP_TICKS = 8;
    constexpr std::size_t K_TICKS        = 64;
}

// Each replaced form has its matching delete, and aligned forms stay default; see README.md.
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
    /**
     * @brief Counts the runs it observes and keeps nothing, so it allocates nothing itself.
     *
     * Adding it turns device timing on, which is what a profiled run is.
     */
    class CountingObserver final : public RunObserver
    {
        public:
            /** @brief Counts one run. */
            void on_run(const RunObservation& observation) noexcept override
            {
                static_cast<void>(observation);
                runs_.fetch_add(1, std::memory_order_relaxed);
            }

            /** @brief Returns how many runs were observed. */
            [[nodiscard]] std::size_t runs() const noexcept
            {
                return runs_.load(std::memory_order_relaxed);
            }

        private:
            /** @brief The count. */
            std::atomic<std::size_t> runs_{0};
    };

    /** @brief Warms @p g up, then returns the allocations the calling thread made over @p ticks runs into one reused report. */
    std::size_t allocations_for_reuse_ticks(Graph& g, std::size_t ticks)
    {
        RunReport report;
        for (std::size_t i = 0; i < K_WARMUP_TICKS; ++i)
            g.run(report).value();

        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting_this_thread = true;
        for (std::size_t i = 0; i < ticks; ++i)
            g.run(report).value();
        g_counting_this_thread = false;

        return g_allocation_count.load(std::memory_order_relaxed);
    }

    /** @brief The same through the value-returning run(), one fresh RunReport per tick. */
    std::size_t allocations_for_value_ticks(Graph& g, std::size_t ticks)
    {
        for (std::size_t i = 0; i < K_WARMUP_TICKS; ++i)
            g.run().value();

        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting_this_thread = true;
        for (std::size_t i = 0; i < ticks; ++i)
        {
            const RunReport report = g.run().value();
            (void)report;
        }
        g_counting_this_thread = false;

        return g_allocation_count.load(std::memory_order_relaxed);
    }

    /** @brief The same through run_async() and a wait into one report the caller owns. */
    std::size_t allocations_for_async_ticks(Graph& g, std::size_t ticks)
    {
        RunReport report;
        for (std::size_t i = 0; i < K_WARMUP_TICKS; ++i)
        {
            auto step = g.run_async().value();
            step.wait(report).value();
        }

        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting_this_thread = true;
        for (std::size_t i = 0; i < ticks; ++i)
        {
            auto step = g.run_async().value();
            step.wait(report).value();
        }
        g_counting_this_thread = false;

        return g_allocation_count.load(std::memory_order_relaxed);
    }

    /** @brief Tells whether the count for four times the ticks stays below four times the count for one, so nothing scales per tick. */
    bool is_amortised(std::size_t shorter, std::size_t longer)
    {
        return longer == 0 || longer < 4 * shorter;
    }
} // namespace

/** @brief Tests that the counter observes an allocation on the calling thread. */
TEST(Medium_SteadyStateAllocation, TheCounterObservesTheCallingThread)
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

/** @brief Fixture of the allocation claims about a run, which need a counter that sees the library. */
class Medium_SteadyStateReuse : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Skips on a toolchain whose counts are unstable and on a build whose counter cannot see the library. */
        void SetUp() override
        {
#if defined(__ADAPTIVECPP__) || defined(__HIPSYCL__)
            GTEST_SKIP() << "AdaptiveCpp's submission allocates a different number of times per call, so "
                            "the counts these tests compare are not stable there; see README.md.";
#endif
            if (!K_COUNTER_SEES_THE_LIBRARY)
                GTEST_SKIP() << "the allocation counter does not see the library in this build; see README.md";
        }

        /** @brief Allocates a float buffer of N zeros and records into @p graph a kernel that fills it. */
        Buffer<float> record_fill(Graph& graph)
        {
            auto buf = runtime().buffer<float>(N).value();
            TestSupport::fill_with(buf, 0.0f);
            float* p = buf.data();
            graph.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<float>(i) * 0.5f; });
            return buf;
        }
};

/** @brief Tests that the value-returning run() allocates at least one more per tick than a run into a reused report. */
TEST_F(Medium_SteadyStateReuse, ARunIntoAReusedReportSavesAnAllocationPerTick)
{
    auto g = runtime().graph();
    const auto buf = record_fill(g);

    const std::size_t reuse = allocations_for_reuse_ticks(g, K_TICKS);
    const std::size_t value = allocations_for_value_ticks(g, K_TICKS);

    EXPECT_GE(value, reuse + K_TICKS)
        << "the value-returning run() did not cost a per-tick allocation more "
           "than run(report); either the counter does not see the run path or "
           "the reuse overload is not actually reusing";
}

/** @brief Tests that a run forced onto the scheduler does not allocate more per tick as the ticks grow. */
TEST_F(Medium_SteadyStateReuse, ASchedulerForcedRunDoesNotAllocatePerTick)
{
    Runtime& scheduled = TestSupport::RuntimeEnvironment::layer_runtime(true);
    auto buf = scheduled.buffer<float>(N).value();
    TestSupport::fill_with(buf, 0.0f);
    float* p = buf.data();
    auto g = scheduled.graph();
    g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<float>(i) * 0.5f; });

    const std::size_t shorter = allocations_for_reuse_ticks(g, K_TICKS);
    const std::size_t longer  = allocations_for_reuse_ticks(g, K_TICKS * 4);

    EXPECT_TRUE(is_amortised(shorter, longer))
        << "a scheduler-forced run's steady-state allocation scales with tick count: "
        << shorter << " over " << K_TICKS << " ticks, " << longer << " over " << (K_TICKS * 4);
}

/** @brief Tests that an overlapped run waited into a reused report does not allocate more per tick as the ticks grow. */
TEST_F(Medium_SteadyStateReuse, AnAsyncWaitIntoAReusedReportIsAmortised)
{
    auto g = runtime().graph();
    const auto buf = record_fill(g);

    const std::size_t shorter = allocations_for_async_ticks(g, K_TICKS);
    const std::size_t longer  = allocations_for_async_ticks(g, K_TICKS * 4);

    EXPECT_TRUE(is_amortised(shorter, longer))
        << "an overlapped tick allocates per step: " << shorter << " over "
        << K_TICKS << " ticks, " << longer << " over " << (K_TICKS * 4);
}

/** @brief Tests that a profiled run forced onto the scheduler does not allocate more per tick as the ticks grow. */
TEST_F(Medium_SteadyStateReuse, AProfiledSchedulerForcedRunDoesNotAllocatePerTick)
{
    Runtime& scheduled = TestSupport::RuntimeEnvironment::layer_runtime(true);
    auto buf = scheduled.buffer<int>(N).value();
    int* p = buf.data();
    auto observer = std::make_shared<CountingObserver>();
    const RunObserverHandle handle = scheduled.add_run_observer(observer);
    auto g = scheduled.graph();
    g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });

    const std::size_t shorter = allocations_for_reuse_ticks(g, K_TICKS);
    const std::size_t longer  = allocations_for_reuse_ticks(g, K_TICKS * 4);

    EXPECT_TRUE(is_amortised(shorter, longer))
        << "a scheduler-forced profiled run's steady-state allocation scales with tick count: "
        << shorter << " over " << K_TICKS << " ticks, " << longer << " over " << (K_TICKS * 4);
}

#endif // !defined(SR_SANITIZER_OWNS_ALLOCATOR)
