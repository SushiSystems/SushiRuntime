/**************************************************************************/
/* test_interop_host_export.cpp                                           */
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
 * @file test_interop_host_export.cpp
 * @brief Tests export_host for alignment, registry routing, kernel writes and unsupported import.
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

/**
 * @brief An exported host region is aligned and rounded to the requested alignment.
 */
TEST(Integration_InteropHostExport, AlignsAndRoundsTheRegion)
{
    auto runtime = Runtime::create();
    auto shared = runtime.interop().export_host<std::uint32_t>(1000, DeviceIndex{0}, 4096);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(shared.address) % 4096u, 0u);
    EXPECT_EQ(shared.bytes % 4096u, 0u);
    EXPECT_GE(shared.bytes, 1000u * sizeof(std::uint32_t));
    EXPECT_EQ(static_cast<void*>(shared.buffer.data()), shared.address);
}

/**
 * @brief The USM registry routes the exported address back to its owning device.
 */
TEST(Integration_InteropHostExport, TheRegistryRoutesTheExportedAddress)
{
    auto runtime = Runtime::create();
    auto shared = runtime.interop().export_host<float>(16, DeviceIndex{0}, 4096);
    EXPECT_EQ(runtime.unsafe_context().memory().device_of(shared.address),
              std::optional<std::size_t>(0));
}

/**
 * @brief A kernel writing through the exported buffer is visible from the host.
 */
TEST(Integration_InteropHostExport, AKernelWritesWhatTheHostReads)
{
    auto runtime = Runtime::create();
    auto shared = runtime.interop().export_host<std::uint32_t>(257, DeviceIndex{0}, 4096);
    std::uint32_t* out = shared.buffer.data();
    runtime.graph()
        .add(Reads(), Writes(shared.buffer), 257,
             [out](std::size_t i) { out[i] = static_cast<std::uint32_t>(i * 3u); })
        .run();
    for (std::size_t i = 0; i < 257; ++i)
        ASSERT_EQ(shared.buffer[i], i * 3u);
}

/**
 * @brief Importing on a queue without the bindless extension refuses with unsupported.
 */
TEST(Integration_InteropHostExport, ImportOnAQueueWithoutTheExtensionThrowsUnsupported)
{
    auto runtime = Runtime::create();
    auto interop = runtime.interop();
    if (interop.capabilities(DeviceIndex{0}).device_import)
        GTEST_SKIP() << "device 0 imports external memory; this case covers the refusal only";
    Interop::ExternalMemoryDescriptor descriptor;
    try
    {
        interop.import_device<float>(descriptor, 4, DeviceIndex{0});
        FAIL() << "import_device returned on a device without import";
    }
    catch (const SushiException& error)
    {
        EXPECT_EQ(error.error().code, Errc::unsupported);
    }
}
