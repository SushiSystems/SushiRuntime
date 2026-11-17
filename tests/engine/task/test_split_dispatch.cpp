/**************************************************************************/
/* test_split_dispatch.cpp                                                */
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
 * @file test_split_dispatch.cpp
 * @brief Tests dispatch_split() and join_split(): parts run at once on their devices and the join waits on their events only.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/task/handle_location.hpp>
#include <SushiRuntime/task/split_dispatch.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /** @brief Returns a queue on a CPU device, or nothing when the machine offers none. */
    std::optional<sycl::queue> cpu_queue()
    {
        try
        {
            return sycl::queue(sycl::cpu_selector_v);
        }
        catch (const sycl::exception&)
        {
            return std::nullopt;
        }
    }

    /** @brief Returns the pointers of @p operands, in position order. */
    std::vector<void*> pointers_of(const BoundOperands& operands)
    {
        std::vector<void*> pointers;
        for (std::size_t position = 0; position < operands.size(); ++position)
            pointers.push_back(operands.untyped(position));
        return pointers;
    }

    /** @brief Records what one slice call received. */
    struct SliceCall
    {
        SplitPart part;                /**< The part. */
        sycl::queue* queue = nullptr;  /**< The queue it received. */
        std::vector<void*> pointers;   /**< The pointers it received, copied. */
    };

    /** @brief Tests that each part receives its device's queue and the pointers bound on that device, then the combine sees the parts in order. */
    TEST(Medium_SplitDispatch, EachPartRunsOnItsDeviceWithThePointersBoundThere)
    {
        std::optional<sycl::queue> first = cpu_queue();
        std::optional<sycl::queue> second = cpu_queue();
        if (!first || !second)
            GTEST_SKIP() << "No CPU device.";
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        int copy = 2;
        const MintOutcome minted = directory.mint(HandleKind::Buffer, HandleFlags::None);
        ASSERT_EQ(minted.status, HandleStatus::Ok);
        ASSERT_EQ(directory.set_instance(minted.handle, device_memory_space(0), &home), HandleStatus::Ok);
        ASSERT_EQ(directory.set_instance(minted.handle, device_memory_space(1), &copy), HandleStatus::Ok);
        KernelPointers pointers({KernelPointerSlot{minted.handle, &home, 0}}, directory);

        std::vector<SliceCall> calls;
        std::vector<SplitPart> combined;
        NodeSplit split;
        split.extent = 10;
        split.slice = [&calls](const SplitPart& part, sycl::queue& queue, span<const sycl::event>, BoundOperands bound)
        {
            calls.push_back(SliceCall{part, &queue, pointers_of(bound)});
            return sycl::event();
        };
        split.combine = [&combined](span<const SplitPart> parts) { combined.assign(parts.begin(), parts.end()); };
        const std::vector<SplitDispatchPart> parts = {{0, SplitPart{0, 0, 6, 0, 2}}, {1, SplitPart{0, 6, 10, 1, 2}}};
        const span<const SplitDispatchPart> placed(parts.data(), parts.size());

        const std::vector<sycl::event> events = dispatch_split(
            split, placed, span<const sycl::event>(), &pointers,
            [&](std::size_t device) -> sycl::queue& { return device == 0 ? *first : *second; });
        EXPECT_TRUE(combined.empty());
        join_split(split, placed, span<const sycl::event>(events.data(), events.size()));

        ASSERT_EQ(events.size(), 2u);
        ASSERT_EQ(calls.size(), 2u);
        EXPECT_EQ(calls[0].part, parts[0].part);
        EXPECT_EQ(calls[0].queue, &*first);
        EXPECT_EQ(calls[0].pointers, std::vector<void*>{&home});
        EXPECT_EQ(calls[1].part, parts[1].part);
        EXPECT_EQ(calls[1].queue, &*second);
        EXPECT_EQ(calls[1].pointers, std::vector<void*>{&copy});
        EXPECT_EQ(combined, (std::vector<SplitPart>{parts[0].part, parts[1].part}));
    }

    /** @brief Tests that a node with no pointers gives each slice an empty table. */
    TEST(Medium_SplitDispatch, ANodeWithNoPointersGivesAnEmptyTable)
    {
        std::optional<sycl::queue> queue = cpu_queue();
        if (!queue)
            GTEST_SKIP() << "No CPU device.";
        std::vector<std::size_t> sizes;
        NodeSplit split;
        split.extent = 4;
        split.slice = [&sizes](const SplitPart&, sycl::queue&, span<const sycl::event>, BoundOperands bound)
        {
            sizes.push_back(bound.size());
            return sycl::event();
        };
        const std::vector<SplitDispatchPart> parts = {{0, SplitPart{0, 0, 4, 0, 1}}};

        (void)dispatch_split(split, span<const SplitDispatchPart>(parts.data(), parts.size()), span<const sycl::event>(),
                             nullptr, [&](std::size_t) -> sycl::queue& { return *queue; });

        EXPECT_EQ(sizes, std::vector<std::size_t>{0});
    }

    /** @brief Opens once and lets every host step that waits on it run. */
    class Gate final
    {
        public:
            /** @brief Lets the waiting steps run. */
            void open() noexcept
            {
                open_.store(true, std::memory_order_release);
            }

            /** @brief Returns once the gate is open; the size's timeout ends a test that never opens it. */
            void pass() const noexcept
            {
                while (!open_.load(std::memory_order_acquire))
                    std::this_thread::yield();
            }

        private:
            std::atomic<bool> open_{false}; /**< Whether the gate is open. */
    };

    /** @brief Submits a host step that passes the gate and then calls done, and returns its event. */
    template <typename Done>
    sycl::event gated_step(sycl::queue& queue, span<const sycl::event> dependencies, const Gate& gate, Done done)
    {
        const std::vector<sycl::event> after(dependencies.begin(), dependencies.end());
        return queue.submit([&](sycl::handler& handler)
        {
            handler.depends_on(after);
            const auto step = [&gate, done]()
            {
                gate.pass();
                done();
            };
#if defined(__ACPP__) || defined(__HIPSYCL__)
            handler.AdaptiveCpp_enqueue_custom_operation([step](auto&&) { step(); });
#else
            handler.host_task(step);
#endif
        });
    }

    /** @brief Appends entries to a log from several threads. */
    class Log final
    {
        public:
            /** @brief Appends entry. */
            void add(std::string entry)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                entries_.push_back(std::move(entry));
            }

            /** @brief Returns the entries so far. */
            [[nodiscard]] std::vector<std::string> entries()
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return entries_;
            }

        private:
            std::mutex mutex_;                 /**< Guards entries_. */
            std::vector<std::string> entries_; /**< The entries in arrival order. */
    };

    /** @brief Tests that every part is submitted before any completes, and that the combine runs after all of them. */
    TEST(Medium_SplitDispatch, EveryPartIsSubmittedBeforeAnyCompletes)
    {
        std::optional<sycl::queue> first = cpu_queue();
        std::optional<sycl::queue> second = cpu_queue();
        if (!first || !second)
            GTEST_SKIP() << "No CPU device.";
        Gate gate;
        Log log;
        NodeSplit split;
        split.extent = 3;
        split.slice = [&gate, &log](const SplitPart& part, sycl::queue& queue, span<const sycl::event> dependencies,
                                    BoundOperands)
        {
            const std::string index = std::to_string(part.index);
            log.add("submit " + index);
            return gated_step(queue, dependencies, gate, [&log, index] { log.add("complete " + index); });
        };
        split.combine = [&log](span<const SplitPart>) { log.add("combine"); };
        const std::vector<SplitDispatchPart> parts = {
            {0, SplitPart{0, 0, 1, 0, 3}}, {1, SplitPart{0, 1, 2, 1, 3}}, {0, SplitPart{0, 2, 3, 2, 3}}};
        const span<const SplitDispatchPart> placed(parts.data(), parts.size());

        const std::vector<sycl::event> events = dispatch_split(
            split, placed, span<const sycl::event>(), nullptr,
            [&](std::size_t device) -> sycl::queue& { return device == 0 ? *first : *second; });
        EXPECT_EQ(log.entries(), (std::vector<std::string>{"submit 0", "submit 1", "submit 2"}));
        gate.open();
        join_split(split, placed, span<const sycl::event>(events.data(), events.size()));

        const std::vector<std::string> entries = log.entries();
        ASSERT_EQ(entries.size(), 7u);
        const std::vector<std::string> completions(entries.begin() + 3, entries.begin() + 6);
        const std::vector<std::string> every_part = {"complete 0", "complete 1", "complete 2"};
        EXPECT_TRUE(std::is_permutation(completions.begin(), completions.end(), every_part.begin()));
        EXPECT_EQ(entries.back(), "combine");
    }

    /** @brief Tests that a part waits on the events it is given before it runs. */
    TEST(Medium_SplitDispatch, APartRunsAfterItsDependencies)
    {
        std::optional<sycl::queue> queue = cpu_queue();
        if (!queue)
            GTEST_SKIP() << "No CPU device.";
        Gate producer_gate;
        Gate open_gate;
        open_gate.open();
        Log log;
        const sycl::event producer = gated_step(*queue, span<const sycl::event>(), producer_gate,
                                                [&log] { log.add("producer"); });
        NodeSplit split;
        split.extent = 1;
        split.slice = [&open_gate, &log](const SplitPart&, sycl::queue& on, span<const sycl::event> dependencies,
                                         BoundOperands)
        {
            return gated_step(on, dependencies, open_gate, [&log] { log.add("part"); });
        };
        const std::vector<SplitDispatchPart> parts = {{0, SplitPart{0, 0, 1, 0, 1}}};
        const span<const SplitDispatchPart> placed(parts.data(), parts.size());

        const std::vector<sycl::event> events = dispatch_split(
            split, placed, span<const sycl::event>(&producer, 1), nullptr, [&](std::size_t) -> sycl::queue& { return *queue; });
        producer_gate.open();
        join_split(split, placed, span<const sycl::event>(events.data(), events.size()));

        EXPECT_EQ(log.entries(), (std::vector<std::string>{"producer", "part"}));
    }

    /** @brief Tests that the join waits on the parts' events only, not on unrelated work in flight on the same queue. */
    TEST(Medium_SplitDispatch, TheJoinDoesNotWaitOnUnrelatedWorkOnThePartsQueue)
    {
        std::optional<sycl::queue> queue = cpu_queue();
        if (!queue)
            GTEST_SKIP() << "No CPU device.";
        Gate unrelated_gate;
        std::atomic<bool> unrelated_done{false};
        const sycl::event unrelated = gated_step(*queue, span<const sycl::event>(), unrelated_gate,
                                                 [&unrelated_done] { unrelated_done.store(true); });
        NodeSplit split;
        split.extent = 1;
        split.slice = [](const SplitPart&, sycl::queue& on, span<const sycl::event> dependencies, BoundOperands)
        {
            const std::vector<sycl::event> after(dependencies.begin(), dependencies.end());
            return on.submit([&](sycl::handler& handler)
            {
                handler.depends_on(after);
                handler.single_task([] {});
            });
        };
        const std::vector<SplitDispatchPart> parts = {{0, SplitPart{0, 0, 1, 0, 1}}};
        const span<const SplitDispatchPart> placed(parts.data(), parts.size());

        const std::vector<sycl::event> events = dispatch_split(
            split, placed, span<const sycl::event>(), nullptr, [&](std::size_t) -> sycl::queue& { return *queue; });
        join_split(split, placed, span<const sycl::event>(events.data(), events.size()));

        EXPECT_FALSE(unrelated_done.load());
        unrelated_gate.open();
        sycl::event finished = unrelated;
        finished.wait();
        EXPECT_TRUE(unrelated_done.load());
    }
} // namespace
