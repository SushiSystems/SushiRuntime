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
#include <limits>
#include <optional>
#include <string>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/context_access.hpp>

using namespace SushiRuntime;

namespace
{
#if defined(_WIN32)
    /// @brief The handle kind this platform imports.
    constexpr ExternalHandleKind K_NATIVE_KIND = ExternalHandleKind::OpaqueWin32;
    /// @brief The handle kind this platform cannot import.
    constexpr ExternalHandleKind K_FOREIGN_KIND = ExternalHandleKind::OpaqueFd;
#else
    /// @brief The handle kind this platform imports.
    constexpr ExternalHandleKind K_NATIVE_KIND = ExternalHandleKind::OpaqueFd;
    /// @brief The handle kind this platform cannot import.
    constexpr ExternalHandleKind K_FOREIGN_KIND = ExternalHandleKind::OpaqueWin32;
#endif

    /// @brief Size the test descriptors state for the exported allocation.
    constexpr std::uint64_t K_ALLOCATION_BYTES = 64;

    /// @brief Handle value that passes the descriptor checks; no test hands it to a device.
    constexpr std::uint64_t K_PLACEHOLDER_HANDLE = 1;

    /** @brief Returns the first device whose device_import equals @p imports, if any. */
    std::optional<std::size_t> first_device_where_import_is(Runtime& runtime, bool imports)
    {
        const std::size_t count = runtime.advanced().device_count();
        for (std::size_t index = 0; index < count; ++index)
            if (runtime.interop().capabilities(DeviceIndex{index}).value().device_import == imports)
                return index;
        return std::nullopt;
    }

    /** @brief Builds a descriptor that device @p index would accept, were its handle real. */
    ExternalMemoryDescriptor acceptable_descriptor(Runtime& runtime, std::size_t index)
    {
        ExternalMemoryDescriptor descriptor{};
        descriptor.kind = K_NATIVE_KIND;
        descriptor.handle = K_PLACEHOLDER_HANDLE;
        descriptor.allocation_bytes = K_ALLOCATION_BYTES;
        descriptor.offset = 0;
        const auto uuid = runtime.interop().capabilities(DeviceIndex{index}).value().device_uuid;
        if (uuid)
            descriptor.device = *uuid;
        return descriptor;
    }

    /** @brief Imports @p count floats and returns the refusal, failing when there is none. */
    Error refusal_of(Runtime& runtime, const ExternalMemoryDescriptor& descriptor,
                     std::size_t count, std::size_t index)
    {
        const auto imported =
            runtime.interop().import_device<float>(descriptor, count, DeviceIndex{index});
        if (imported.has_value())
        {
            ADD_FAILURE() << "import_device accepted the descriptor";
            return Error{};
        }
        return imported.error();
    }
} // namespace

TEST(Unit_InteropImport, HostExportReportsWhetherTheQueueOffersHostUSM)
{
    auto runtime = Runtime::create().value();
    const std::size_t count = runtime.advanced().device_count();

    for (std::size_t index = 0; index < count; ++index)
    {
        Internal::Execution::RuntimeContext& context =
            SushiRuntime::Detail::ContextAccess::context(runtime);
        const sycl::device device = context.devices().queue(index).get_device();
        EXPECT_EQ(runtime.interop().capabilities(DeviceIndex{index}).value().host_export,
                  device.has(sycl::aspect::usm_host_allocations))
            << "device " << index;
    }
}

TEST(Unit_InteropImport, ADeviceWithoutImportIsRefusedByItsIndex)
{
    auto runtime = Runtime::create().value();
    const std::optional<std::size_t> index = first_device_where_import_is(runtime, false);
    if (!index)
        GTEST_SKIP() << "every device imports external memory";

    const Error refusal =
        refusal_of(runtime, acceptable_descriptor(runtime, *index), 4, *index);

    EXPECT_EQ(refusal.code, ErrorCode::Unsupported);
    EXPECT_NE(refusal.message.find("device " + std::to_string(*index)), std::string::npos)
        << refusal.message;
    EXPECT_NE(refusal.message.find("device_import is false"), std::string::npos)
        << refusal.message;
}

TEST(Unit_InteropImport, ADeviceThatImportsReportsItsUUID)
{
    auto runtime = Runtime::create().value();
    const std::size_t count = runtime.advanced().device_count();

    for (std::size_t index = 0; index < count; ++index)
    {
        const InteropCapabilities capabilities =
            runtime.interop().capabilities(DeviceIndex{index}).value();
        if (capabilities.device_import)
            EXPECT_TRUE(capabilities.device_uuid.has_value()) << "device " << index;
    }
}

TEST(Unit_InteropImport, ADefaultDescriptorIsRefusedForItsUnstatedKind)
{
    auto runtime = Runtime::create().value();
    const std::optional<std::size_t> index = first_device_where_import_is(runtime, true);
    if (!index)
        GTEST_SKIP() << "no device imports external memory";

    const ExternalMemoryDescriptor descriptor;
    const Error refusal = refusal_of(runtime, descriptor, 4, *index);

    EXPECT_EQ(refusal.code, ErrorCode::InvalidAccess);
    EXPECT_NE(refusal.message.find("descriptor.kind is Unspecified"), std::string::npos)
        << refusal.message;
    EXPECT_NE(refusal.message.find("the exporter must state it"), std::string::npos)
        << refusal.message;
}

#if defined(_WIN32)
TEST(Unit_InteropImport, ANullWin32HandleIsRefusedByName)
{
    auto runtime = Runtime::create().value();
    const std::optional<std::size_t> index = first_device_where_import_is(runtime, true);
    if (!index)
        GTEST_SKIP() << "no device imports external memory";

    ExternalMemoryDescriptor descriptor = acceptable_descriptor(runtime, *index);
    descriptor.handle = 0;
    const Error refusal = refusal_of(runtime, descriptor, 4, *index);

    EXPECT_EQ(refusal.code, ErrorCode::InvalidAccess);
    EXPECT_NE(refusal.message.find("descriptor.handle is null"), std::string::npos)
        << refusal.message;
}
#else
TEST(Unit_InteropImport, AFileDescriptorAboveIntIsRefusedWithItsValue)
{
    auto runtime = Runtime::create().value();
    const std::optional<std::size_t> index = first_device_where_import_is(runtime, true);
    if (!index)
        GTEST_SKIP() << "no device imports external memory";

    ExternalMemoryDescriptor descriptor = acceptable_descriptor(runtime, *index);
    descriptor.handle = static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1;
    const Error refusal = refusal_of(runtime, descriptor, 4, *index);

    EXPECT_EQ(refusal.code, ErrorCode::InvalidAccess);
    EXPECT_NE(refusal.message.find("descriptor.handle is " + std::to_string(descriptor.handle)),
              std::string::npos)
        << refusal.message;
}
#endif

TEST(Unit_InteropImport, ZeroElementsImportNothingAndReturnAnEmptyBuffer)
{
    auto runtime = Runtime::create().value();
    const std::optional<std::size_t> index = first_device_where_import_is(runtime, true);
    if (!index)
        GTEST_SKIP() << "no device imports external memory";

    const Buffer<float> buffer = runtime.interop().import_device<float>(
        acceptable_descriptor(runtime, *index), 0, DeviceIndex{*index}).value();

    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0u);
}

TEST(Unit_InteropImport, AHandleKindOfAnotherPlatformIsRefusedByName)
{
    auto runtime = Runtime::create().value();
    const std::optional<std::size_t> index = first_device_where_import_is(runtime, true);
    if (!index)
        GTEST_SKIP() << "no device imports external memory";

    ExternalMemoryDescriptor descriptor = acceptable_descriptor(runtime, *index);
    descriptor.kind = K_FOREIGN_KIND;
    const Error refusal = refusal_of(runtime, descriptor, 4, *index);

    EXPECT_EQ(refusal.code, ErrorCode::InvalidAccess);
    EXPECT_NE(refusal.message.find("descriptor.kind is"), std::string::npos) << refusal.message;
}

TEST(Unit_InteropImport, ARegionPastTheAllocationIsRefusedWithItsNumbers)
{
    auto runtime = Runtime::create().value();
    const std::optional<std::size_t> index = first_device_where_import_is(runtime, true);
    if (!index)
        GTEST_SKIP() << "no device imports external memory";

    ExternalMemoryDescriptor descriptor = acceptable_descriptor(runtime, *index);
    descriptor.offset = 60;
    const Error past_the_end = refusal_of(runtime, descriptor, 4, *index);

    EXPECT_EQ(past_the_end.code, ErrorCode::InvalidAccess);
    EXPECT_NE(past_the_end.message.find("16 bytes at offset 60"), std::string::npos)
        << past_the_end.message;
    EXPECT_NE(past_the_end.message.find("allocation of 64 bytes"), std::string::npos)
        << past_the_end.message;

    // An offset beyond the allocation must not wrap the subtraction; see README.md.
    descriptor.offset = K_ALLOCATION_BYTES + 1;
    const Error offset_outside = refusal_of(runtime, descriptor, 0, *index);
    EXPECT_EQ(offset_outside.code, ErrorCode::InvalidAccess);
    EXPECT_NE(offset_outside.message.find("offset 65"), std::string::npos)
        << offset_outside.message;
}

TEST(Unit_InteropImport, AnotherDevicesUUIDIsRefusedWithBothUUIDs)
{
    auto runtime = Runtime::create().value();
    const std::optional<std::size_t> index = first_device_where_import_is(runtime, true);
    if (!index)
        GTEST_SKIP() << "no device imports external memory";
    if (!runtime.interop().capabilities(DeviceIndex{*index}).value().device_uuid)
        GTEST_SKIP() << "the importing device reports no UUID";

    ExternalMemoryDescriptor descriptor = acceptable_descriptor(runtime, *index);
    descriptor.device.bytes[15] = static_cast<std::uint8_t>(descriptor.device.bytes[15] ^ 0xFFu);
    const Error refusal = refusal_of(runtime, descriptor, 4, *index);

    EXPECT_EQ(refusal.code, ErrorCode::InvalidAccess);
    EXPECT_NE(refusal.message.find("descriptor.device is"), std::string::npos) << refusal.message;
    EXPECT_NE(refusal.message.find("and device " + std::to_string(*index) + " is"),
              std::string::npos)
        << refusal.message;
}
