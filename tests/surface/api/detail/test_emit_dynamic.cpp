/**************************************************************************/
/* test_emit_dynamic.cpp                                                  */
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
 * @file test_emit_dynamic.cpp
 * @brief Tests dynamic_node() and with_device_slots(): the live window and the bound device slots.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_dynamic.hpp>
#include <SushiRuntime/api/detail/late_window.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/task/handle_location.hpp>
#include <SushiRuntime/task/kernel_pointers.hpp>

#include "description_runner.hpp"
#include "support/buffer_contents.hpp"
#include "support/host_runtime.hpp"
#include "support/handle_table.hpp"

using namespace SushiRuntime;

/** @brief Fixture of the dynamic-node test, which runs a description on a host runtime. */
class Small_EmitDynamic : public TestSupport::HostRuntimeTest
{
};

/** @brief Tests that the kernel runs over the live count and keeps its bindings. */
TEST_F(Small_EmitDynamic, TheKernelRunsOverTheLiveCount)
{
    constexpr std::size_t K_CAPACITY = 8;
    constexpr std::size_t K_LIVE = 5;
    auto data = runtime().buffer<int>(K_CAPACITY).value();
    TestSupport::fill_with(data, 0);
    int* const words = data.data();
    const std::shared_ptr<Detail::LateControls> controls =
        Testing::bound_controls(sized([] { return K_LIVE; }));
    const Detail::NodeDescription description = Detail::dynamic_node(
        controls, nullptr, {}, {TestSupport::region_at(words)}, K_CAPACITY,
        [words](std::size_t index) { words[index] = 1; }, K_CAPACITY);

    description.host_form();

    EXPECT_EQ(description.kind, Detail::NodeKind::Dynamic);
    EXPECT_EQ(description.bindings, controls);
    EXPECT_EQ(TestSupport::contents(data), TestSupport::window_of<int>(K_CAPACITY, 0, K_LIVE, 1, 0));
}

/** @brief Tests that with_device_slots() puts the count slot, then the base slot, in the pointer table and leaves the node fixed. */
TEST_F(Small_EmitDynamic, DeviceSlotsJoinThePointerTableCountFirst)
{
    auto counter = runtime().buffer<std::uint32_t>(2, DeviceIndex{0}, Residency::Host).value();
    auto bases = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    const Dynamic window = sized_from_device(counter, 1).based_at_device(bases, 0);

    const Detail::NodeDescription description =
        Detail::with_device_slots(window, Detail::NodeDescription{});

    EXPECT_EQ(description.pointer_handles, (std::vector<GlobalHandle>{counter.handle(), bases.handle()}));
    EXPECT_EQ(description.pointer_homes,
              (std::vector<void*>{static_cast<void*>(counter.data()), static_cast<void*>(bases.data())}));
    EXPECT_FALSE(description.rebindable) << "the kernel body captured addresses of its own";
}

/** @brief Tests that a Dynamic with no device slot adds nothing to the pointer table. */
TEST_F(Small_EmitDynamic, AWindowWithNoDeviceSlotAddsNoPointer)
{
    const Detail::NodeDescription description =
        Detail::with_device_slots(sized([] { return std::size_t{3}; }), Detail::NodeDescription{});

    EXPECT_TRUE(description.pointer_handles.empty());
    EXPECT_TRUE(description.pointer_homes.empty());
}

/** @brief Tests that the window reads its device count slot through the table the dispatching node bound. */
TEST_F(Small_EmitDynamic, TheWindowReadsTheSlotTheNodeBound)
{
    auto counter = runtime().buffer<std::uint32_t>(2, DeviceIndex{0}, Residency::Host).value();
    std::vector<BufferRegion> reads;
    std::vector<LivenessObserver> lives;
    const std::shared_ptr<Detail::LateControls> controls =
        Detail::make_late_bindings(sized_from_device(counter, 1), reads, lives);
    Internal::Memory::ResidencyDirectory directory(0);
    const GlobalHandle handle = directory.mint(HandleKind::Buffer, HandleFlags::None).handle;
    std::uint32_t instance[2] = {};
    ASSERT_EQ(directory.set_instance(handle, Internal::Graph::device_memory_space(1), instance),
              HandleStatus::Ok);
    Internal::Graph::KernelPointers pointers(
        {Internal::Graph::KernelPointerSlot{handle, counter.data(), 0}}, directory);

    pointers.bind(1);
    const Internal::Graph::KernelPointerScope scope(pointers);
    const Detail::LateWindow window = Detail::read_late_window(controls.get());

    EXPECT_EQ(window.count_slot, &instance[1]);
}
