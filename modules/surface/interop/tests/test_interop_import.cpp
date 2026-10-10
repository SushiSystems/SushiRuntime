/**************************************************************************/
/* test_interop_import.cpp                                                */
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
 * @file test_interop_import.cpp
 * @brief Tests what import_device refuses and what capabilities() reports about a queue.
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/SushiRuntime.h>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

using namespace SushiRuntime;

namespace
{
#if defined(_WIN32)
    /// @brief The handle kind this platform imports.
    constexpr Interop::ExternalHandleKind K_NATIVE_KIND = Interop::ExternalHandleKind::OpaqueWin32;
    /// @brief The handle kind this platform cannot import.
    constexpr Interop::ExternalHandleKind K_FOREIGN_KIND = Interop::ExternalHandleKind::OpaqueFd;
#else
    /// @brief The handle kind this platform imports.
    constexpr Interop::ExternalHandleKind K_NATIVE_KIND = Interop::ExternalHandleKind::OpaqueFd;
    /// @brief The handle kind this platform cannot import.
    constexpr Interop::ExternalHandleKind K_FOREIGN_KIND = Interop::ExternalHandleKind::OpaqueWin32;
#endif

    /// @brief Size the test descriptors state for the exported allocation.
    constexpr std::uint64_t K_ALLOCATION_BYTES = 64;

    /** @brief Returns the first device whose device_import equals @p imports, if any. */
    std::optional<std::size_t> first_device_where_import_is(Runtime& runtime, bool imports)
    {
        const std::size_t count = runtime.advanced().device_count();
        for (std::size_t index = 0; index < count; ++index)
            if (runtime.interop().capabilities(DeviceIndex{index}).device_import == imports)
                return index;
        return std::nullopt;
    }

    /** @brief Builds a descriptor that device @p index would accept, were its handle real. */
    Interop::ExternalMemoryDescriptor acceptable_descriptor(Runtime& runtime, std::size_t index)
    {
        Interop::ExternalMemoryDescriptor descriptor{};
        descriptor.kind = K_NATIVE_KIND;
        descriptor.allocation_bytes = K_ALLOCATION_BYTES;
        descriptor.offset = 0;
        const auto uuid = runtime.interop().capabilities(DeviceIndex{index}).device_uuid;
        if (uuid)
            descriptor.device = *uuid;
        return descriptor;
    }

    /** @brief Imports @p count floats and returns the refusal, failing when there is none. */
    Core::Error refusal_of(Runtime& runtime, const Interop::ExternalMemoryDescriptor& descriptor,
                           std::size_t count, std::size_t index)
    {
        try
        {
            static_cast<void>(
                runtime.interop().import_device<float>(descriptor, count, DeviceIndex{index}));
            ADD_FAILURE() << "import_device accepted the descriptor";
        }
        catch (const SushiException& refusal)
        {
            return refusal.error();
        }
        return Core::Error{};
    }
} // namespace

TEST(Unit_InteropImport, HostExportReportsWhetherTheQueueOffersHostUsm)
{
    auto runtime = Runtime::create();
    const std::size_t count = runtime.advanced().device_count();

    for (std::size_t index = 0; index < count; ++index)
    {
        const sycl::device device = runtime.unsafe_context().devices().queue(index).get_device();
        EXPECT_EQ(runtime.interop().capabilities(DeviceIndex{index}).host_export,
                  device.has(sycl::aspect::usm_host_allocations))
            << "device " << index;
    }
}

TEST(Unit_InteropImport, ADeviceWithoutImportIsRefusedByItsIndex)
{
    auto runtime = Runtime::create();
    const std::optional<std::size_t> index = first_device_where_import_is(runtime, false);
    if (!index)
        GTEST_SKIP() << "every device imports external memory";

    const Core::Error refusal =
        refusal_of(runtime, acceptable_descriptor(runtime, *index), 4, *index);

    EXPECT_EQ(refusal.code, Errc::unsupported);
    EXPECT_NE(refusal.message.find("device " + std::to_string(*index)), std::string::npos)
        << refusal.message;
    EXPECT_NE(refusal.message.find("device_import is false"), std::string::npos)
        << refusal.message;
}

TEST(Unit_InteropImport, AHandleKindOfAnotherPlatformIsRefusedByName)
{
    auto runtime = Runtime::create();
    const std::optional<std::size_t> index = first_device_where_import_is(runtime, true);
    if (!index)
        GTEST_SKIP() << "no device imports external memory";

    Interop::ExternalMemoryDescriptor descriptor = acceptable_descriptor(runtime, *index);
    descriptor.kind = K_FOREIGN_KIND;
    const Core::Error refusal = refusal_of(runtime, descriptor, 4, *index);

    EXPECT_EQ(refusal.code, Errc::invalid_access);
    EXPECT_NE(refusal.message.find("descriptor.kind is"), std::string::npos) << refusal.message;
}

TEST(Unit_InteropImport, ARegionPastTheAllocationIsRefusedWithItsNumbers)
{
    auto runtime = Runtime::create();
    const std::optional<std::size_t> index = first_device_where_import_is(runtime, true);
    if (!index)
        GTEST_SKIP() << "no device imports external memory";

    Interop::ExternalMemoryDescriptor descriptor = acceptable_descriptor(runtime, *index);
    descriptor.offset = 60;
    const Core::Error past_the_end = refusal_of(runtime, descriptor, 4, *index);

    EXPECT_EQ(past_the_end.code, Errc::invalid_access);
    EXPECT_NE(past_the_end.message.find("16 bytes at offset 60"), std::string::npos)
        << past_the_end.message;
    EXPECT_NE(past_the_end.message.find("allocation of 64 bytes"), std::string::npos)
        << past_the_end.message;

    // An offset beyond the allocation must not wrap the subtraction; see README.md.
    descriptor.offset = K_ALLOCATION_BYTES + 1;
    const Core::Error offset_outside = refusal_of(runtime, descriptor, 0, *index);
    EXPECT_EQ(offset_outside.code, Errc::invalid_access);
    EXPECT_NE(offset_outside.message.find("offset 65"), std::string::npos)
        << offset_outside.message;
}

TEST(Unit_InteropImport, AnotherDevicesUuidIsRefusedWithBothUuids)
{
    auto runtime = Runtime::create();
    const std::optional<std::size_t> index = first_device_where_import_is(runtime, true);
    if (!index)
        GTEST_SKIP() << "no device imports external memory";
    if (!runtime.interop().capabilities(DeviceIndex{*index}).device_uuid)
        GTEST_SKIP() << "the importing device reports no UUID";

    Interop::ExternalMemoryDescriptor descriptor = acceptable_descriptor(runtime, *index);
    descriptor.device.bytes[15] = static_cast<std::uint8_t>(descriptor.device.bytes[15] ^ 0xFFu);
    const Core::Error refusal = refusal_of(runtime, descriptor, 4, *index);

    EXPECT_EQ(refusal.code, Errc::invalid_access);
    EXPECT_NE(refusal.message.find("descriptor.device is"), std::string::npos) << refusal.message;
    EXPECT_NE(refusal.message.find("and device " + std::to_string(*index) + " is"),
              std::string::npos)
        << refusal.message;
}
