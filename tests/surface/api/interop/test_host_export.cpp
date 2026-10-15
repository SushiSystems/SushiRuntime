/**************************************************************************/
/* test_host_export.cpp                                                   */
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
 * @file test_host_export.cpp
 * @brief Tests exporting host memory a device and the host both reach.
 * @see tests/surface/api/interop/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

using namespace SushiRuntime;

/**
 * @brief An exported host region is aligned and rounded to the requested alignment.
 */
TEST(Medium_InteropHostExport, AlignsAndRoundsTheRegion)
{
    auto runtime = Runtime::create().value();
    auto shared = runtime.interop().export_host<std::uint32_t>(1000, DeviceIndex{0}, 4096).value();
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(shared.address) % 4096u, 0u);
    EXPECT_EQ(shared.bytes % 4096u, 0u);
    EXPECT_GE(shared.bytes, 1000u * sizeof(std::uint32_t));
    EXPECT_EQ(static_cast<void*>(shared.buffer.data()), shared.address);
}

/**
 * @brief The USM registry routes the exported address back to its owning device.
 */
TEST(Medium_InteropHostExport, TheRegistryRoutesTheExportedAddress)
{
    auto runtime = Runtime::create().value();
    auto shared = runtime.interop().export_host<float>(16, DeviceIndex{0}, 4096).value();
    Internal::Memory::USMAllocator& memory =
        Internal::ContextAccess::context(runtime).memory();
    EXPECT_EQ(memory.device_of(shared.address), std::optional<std::size_t>(0));
}

/**
 * @brief A kernel writing through the exported buffer is visible from the host.
 */
TEST(Medium_InteropHostExport, AKernelWritesWhatTheHostReads)
{
    auto runtime = Runtime::create().value();
    auto shared = runtime.interop().export_host<std::uint32_t>(257, DeviceIndex{0}, 4096).value();
    std::uint32_t* out = shared.buffer.data();
    runtime.graph()
        .add(257, Reads(), Writes(shared.buffer),
             [out](std::size_t i) { out[i] = static_cast<std::uint32_t>(i * 3u); })
        .run().value();
    for (std::size_t i = 0; i < 257; ++i)
        ASSERT_EQ(shared.buffer[i], i * 3u);
}

/**
 * @brief Importing on a queue without the bindless extension returns Unsupported.
 */
TEST(Medium_InteropHostExport, ImportOnAQueueWithoutTheExtensionReturnsUnsupported)
{
    auto runtime = Runtime::create().value();
    auto interop = runtime.interop();
    if (interop.capabilities(DeviceIndex{0}).value().device_import)
        GTEST_SKIP() << "device 0 imports external memory; this case covers the refusal only";
    ExternalMemoryDescriptor descriptor;
    const auto imported = interop.import_device<float>(descriptor, 4, DeviceIndex{0});
    ASSERT_FALSE(imported.has_value()) << "import_device returned on a device without import";
    EXPECT_EQ(imported.error().code, ErrorCode::Unsupported);
}

/**
 * @brief An alignment that is not a power of two stops the process.
 */
TEST(Medium_InteropHostExportDeathTest, AnAlignmentThatIsNotAPowerOfTwoStops)
{
    auto runtime = Runtime::create().value();

    EXPECT_DEATH(
        static_cast<void>(runtime.interop().export_host<float>(16, DeviceIndex{0}, 4097)),
        "export_host: alignment 4097 is not a power of two");
}

/**
 * @brief A byte size that overflows when rounded up to the alignment stops the process.
 */
TEST(Medium_InteropHostExportDeathTest, ASizeThatOverflowsWhenRoundedStops)
{
    auto runtime = Runtime::create().value();
    const std::size_t most = std::numeric_limits<std::size_t>::max();

    EXPECT_DEATH(
        static_cast<void>(runtime.interop().export_host<std::uint8_t>(most, DeviceIndex{0}, 4096)),
        "bytes overflow std::size_t when rounded up to the alignment of 4096");
}

/**
 * @brief An export no device can hold returns AllocationFailed and leaves the runtime usable.
 */
TEST(Medium_InteropHostExport, AnExportNoDeviceCanSatisfyReturnsAllocationFailed)
{
    auto runtime = Runtime::create().value();
    // Half the address space in bytes: it rounds without overflow and no device has the room.
    const std::size_t count = std::numeric_limits<std::size_t>::max() / 2;

    const auto refused = runtime.interop().export_host<std::uint8_t>(count, DeviceIndex{0}, 4096);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::AllocationFailed);

    const auto exported = runtime.interop().export_host<float>(16, DeviceIndex{0}, 4096);
    EXPECT_TRUE(exported.has_value());
}
