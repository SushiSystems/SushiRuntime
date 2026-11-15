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
 * @brief Tests dispatch_split(): each part runs with its device's queue and the pointers bound there.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <optional>
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

    /** @brief Records what one slice call received. */
    struct SliceCall
    {
        SplitPart part;                /**< The part. */
        sycl::queue* queue = nullptr;  /**< The queue it received. */
        std::vector<void*> pointers;   /**< The pointers it received, copied. */
    };

    /** @brief Tests that each part receives its device's queue and the pointers bound on that device, then the combine sees the parts in order. */
    TEST(Small_SplitDispatch, EachPartRunsOnItsDeviceWithThePointersBoundThere)
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
        split.slice = [&calls](const SplitPart& part, sycl::queue& queue, span<void* const> bound)
        {
            calls.push_back(SliceCall{part, &queue, std::vector<void*>(bound.begin(), bound.end())});
        };
        split.combine = [&combined](span<const SplitPart> parts) { combined.assign(parts.begin(), parts.end()); };
        const std::vector<SplitDispatchPart> parts = {{0, SplitPart{0, 0, 6, 0, 2}}, {1, SplitPart{0, 6, 10, 1, 2}}};

        dispatch_split(split, span<const SplitDispatchPart>(parts.data(), parts.size()), &pointers,
                       [&](std::size_t device) -> sycl::queue& { return device == 0 ? *first : *second; });

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
    TEST(Small_SplitDispatch, ANodeWithNoPointersGivesAnEmptyTable)
    {
        std::optional<sycl::queue> queue = cpu_queue();
        if (!queue)
            GTEST_SKIP() << "No CPU device.";
        std::vector<std::size_t> sizes;
        NodeSplit split;
        split.extent = 4;
        split.slice = [&sizes](const SplitPart&, sycl::queue&, span<void* const> bound) { sizes.push_back(bound.size()); };
        const std::vector<SplitDispatchPart> parts = {{0, SplitPart{0, 0, 4, 0, 1}}};

        dispatch_split(split, span<const SplitDispatchPart>(parts.data(), parts.size()), nullptr,
                       [&](std::size_t) -> sycl::queue& { return *queue; });

        EXPECT_EQ(sizes, std::vector<std::size_t>{0});
    }
} // namespace
