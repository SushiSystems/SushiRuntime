/**************************************************************************/
/* benchmark_residency_resolve.cpp                                        */
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
 * @file benchmark_residency_resolve.cpp
 * @brief Measures resolve() and resolve_on_device() of a residency directory from eight threads.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_resolver.hpp>
#include <SushiRuntime/handle/handle_status.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/task/handle_location.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;
using namespace SushiRuntime::Internal::Memory;

namespace
{
    constexpr std::uint32_t K_UNIVERSE = 11;

    /** @brief Handles minted, one instance each; four chunks of ResidencyDirectory::K_CHUNK_ENTRIES. */
    constexpr std::size_t K_HANDLE_COUNT = 4096;

    /** @brief Threads that resolve at the same time. */
    constexpr std::size_t K_THREADS = 8;

    /** @brief Passes over every handle, per thread and phase. */
    constexpr std::size_t K_ROUNDS = 1024;

    /** @brief Device whose memory space holds every instance and is resolved. */
    constexpr std::size_t K_DEVICE = 0;

    using Clock = std::chrono::steady_clock;

    /** @brief What one thread measured over its passes. */
    struct ThreadResult
    {
        Clock::time_point begin{};          /**< When the thread left the start gate. */
        Clock::time_point end{};            /**< When the thread finished its last pass. */
        std::uint64_t resolves = 0;         /**< Resolves the thread made. */
        std::uint64_t mismatches = 0;       /**< Resolves whose address was not the minted one. */
    };

    /**
     * @brief Resolves every handle K_ROUNDS times with @p lookup and checks each address.
     *
     * Lock-free: ResidencyDirectory::resolve() reads atomics and fences, and resolve_on_device()
     * calls resolve(). Neither takes writer_mutex_, which only mint, retire, describe, set_instance
     * and drop_instance take, none of them inside a timed pass.
     */
    template <typename Lookup>
    ThreadResult resolve_passes(const std::vector<GlobalHandle>& handles,
                                const std::vector<void*>& expected,
                                const std::atomic<bool>& start_gate, Lookup lookup)
    {
        while (!start_gate.load(std::memory_order_acquire))
            std::this_thread::yield();

        ThreadResult result;
        result.begin = Clock::now();
        for (std::size_t round = 0; round < K_ROUNDS; ++round)
            for (std::size_t index = 0; index < handles.size(); ++index)
                if (lookup(handles[index]) != expected[index])
                    ++result.mismatches;
        result.end = Clock::now();
        result.resolves = static_cast<std::uint64_t>(K_ROUNDS) * handles.size();
        return result;
    }

    /**
     * @brief Runs one phase on K_THREADS threads and prints the timing of each thread and the total.
     * @return The number of resolves whose address was not the minted one, summed over the threads.
     */
    template <typename Lookup>
    std::uint64_t run_phase(const char* label, const std::vector<GlobalHandle>& handles,
                            const std::vector<void*>& expected, Lookup lookup)
    {
        std::atomic<bool> start_gate{false};
        std::vector<ThreadResult> results(K_THREADS);
        std::vector<std::thread> threads;
        threads.reserve(K_THREADS);
        for (std::size_t thread = 0; thread < K_THREADS; ++thread)
            threads.emplace_back([&results, &handles, &expected, &start_gate, &lookup, thread]() {
                results[thread] = resolve_passes(handles, expected, start_gate, lookup);
            });
        start_gate.store(true, std::memory_order_release);
        for (std::thread& thread : threads)
            thread.join();

        std::uint64_t mismatches = 0;
        std::uint64_t total_resolves = 0;
        Clock::time_point first_begin = results.front().begin;
        Clock::time_point last_end = results.front().end;
        std::printf("%s, %zu handles, %zu threads, %zu rounds\n", label, handles.size(), K_THREADS, K_ROUNDS);
        for (std::size_t thread = 0; thread < K_THREADS; ++thread)
        {
            const ThreadResult& result = results[thread];
            const double thread_ns =
                std::chrono::duration<double, std::nano>(result.end - result.begin).count();
            std::printf("  thread %zu: %8.2f ns per resolve\n", thread,
                        thread_ns / static_cast<double>(result.resolves));
            mismatches += result.mismatches;
            total_resolves += result.resolves;
            if (result.begin < first_begin)
                first_begin = result.begin;
            if (result.end > last_end)
                last_end = result.end;
        }
        const double wall_seconds = std::chrono::duration<double>(last_end - first_begin).count();
        std::printf("  total: %llu resolves in %.6f s, %.3e resolves per second, %llu mismatches\n",
                    static_cast<unsigned long long>(total_resolves), wall_seconds,
                    static_cast<double>(total_resolves) / wall_seconds,
                    static_cast<unsigned long long>(mismatches));
        return mismatches;
    }
} // namespace

/**
 * @brief Resolves K_HANDLE_COUNT handles from K_THREADS threads, in two phases.
 *
 * Each handle has an instance on device K_DEVICE. Every resolve must return the minted address.
 * The timed passes take no lock (see resolve_passes()).
 */
TEST(Benchmark_ResidencyResolve, EightThreadsResolveEveryMintedAddress)
{
    ResidencyDirectory directory(K_UNIVERSE);
    const MemorySpaceID space = device_memory_space(K_DEVICE);

    std::vector<int> storage(K_HANDLE_COUNT);
    std::vector<GlobalHandle> handles;
    std::vector<void*> expected;
    handles.reserve(K_HANDLE_COUNT);
    expected.reserve(K_HANDLE_COUNT);
    for (std::size_t index = 0; index < K_HANDLE_COUNT; ++index)
    {
        const MintOutcome outcome = directory.mint(HandleKind::Buffer, HandleFlags::None);
        ASSERT_EQ(outcome.status, HandleStatus::Ok);
        void* const address = &storage[index];
        ASSERT_EQ(directory.set_instance(outcome.handle, space, address), HandleStatus::Ok);
        handles.push_back(outcome.handle);
        expected.push_back(address);
    }

    const std::uint64_t resolve_mismatches =
        run_phase("resolve", handles, expected, [&directory, space](GlobalHandle handle) {
            return directory.resolve(handle, space).address;
        });
    EXPECT_EQ(resolve_mismatches, 0U);

    const std::uint64_t device_mismatches =
        run_phase("resolve_on_device", handles, expected, [&directory](GlobalHandle handle) {
            return resolve_on_device(directory, handle, K_DEVICE);
        });
    EXPECT_EQ(device_mismatches, 0U);
}
