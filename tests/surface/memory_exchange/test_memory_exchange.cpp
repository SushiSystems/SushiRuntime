/**************************************************************************/
/* test_memory_exchange.cpp                                               */
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
 * @file test_memory_exchange.cpp
 * @brief Tests the memory exchange: capabilities, a host export, and an import that names no mechanism.
 * @see tests/surface/memory_exchange/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <SushiRuntime/core/allocation.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/interop/external_handle_kind_set.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>

#include <SushiRuntime/memory_exchange/capability_cache.hpp>
#include <SushiRuntime/memory_exchange/memory_exchange.hpp>

using namespace SushiRuntime;

/** @brief Tests that device 0 reports its capabilities. */
TEST(Medium_MemoryExchange, CapabilitiesOfDeviceZero)
{
    const auto context = SushiRuntime::Test::make_context();
    Internal::MemoryExchange::CapabilityCache cache;
    EXPECT_TRUE(Internal::MemoryExchange::query_capabilities(*context, cache, 0).has_value());
}

/** @brief Tests that a host export gives an allocation of the size asked when the device can export. */
TEST(Medium_MemoryExchange, AHostExportGivesTheSizeAsked)
{
    const auto context = SushiRuntime::Test::make_context();
    Internal::MemoryExchange::CapabilityCache cache;
    const InteropCapabilities capabilities =
        Internal::MemoryExchange::query_capabilities(*context, cache, 0).value();
    if (!capabilities.host_export)
        GTEST_SKIP() << "device 0 cannot export host memory";
    const Result<Detail::Allocation> exported =
        Internal::MemoryExchange::export_host_memory(context, 4096, 0, 4096);
    ASSERT_TRUE(exported.has_value()) << exported.error().message;
    EXPECT_EQ(exported.value().bytes(), 4096u);
}

/** @brief Tests that an import naming no handle kind is refused with InvalidAccess. */
TEST(Medium_MemoryExchange, AnImportWithNoKindIsRefused)
{
    const auto context = SushiRuntime::Test::make_context();
    Internal::MemoryExchange::CapabilityCache cache;
    const InteropCapabilities capabilities =
        Internal::MemoryExchange::query_capabilities(*context, cache, 0).value();
    if (!capabilities.device_import)
        GTEST_SKIP() << "device 0 cannot import, so its descriptor is never read";
    ExternalMemoryDescriptor descriptor;
    descriptor.allocation_bytes = 4096;
    const Result<Detail::Allocation> imported =
        Internal::MemoryExchange::import_device_memory(context, cache, descriptor, 4096, 0);
    ASSERT_FALSE(imported.has_value());
    EXPECT_EQ(imported.error().code, ErrorCode::InvalidAccess);
    EXPECT_NE(imported.error().message.find("Unspecified"), std::string::npos);
}

/** @brief Tests that a kind device 0 does not import is refused before SYCL sees it, by name. */
TEST(Medium_MemoryExchangeImport, AKindTheDeviceDoesNotImportIsRefusedByName)
{
    const auto context = SushiRuntime::Test::make_context();
    Internal::MemoryExchange::CapabilityCache cache;
    const InteropCapabilities capabilities =
        Internal::MemoryExchange::query_capabilities(*context, cache, 0).value();

    const std::pair<ExternalHandleKind, const char*> candidates[] = {
        {ExternalHandleKind::OpaqueWin32, "OpaqueWin32"},
        {ExternalHandleKind::OpaqueFd, "OpaqueFd"}};
    const std::pair<ExternalHandleKind, const char*>* refused = nullptr;
    for (const auto& candidate : candidates)
    {
        if (!capabilities.importable_kinds.contains(candidate.first))
            refused = &candidate;
    }
    if (refused == nullptr)
        GTEST_SKIP() << "device 0 imports every handle kind";

    ExternalMemoryDescriptor descriptor;
    descriptor.kind = refused->first;
    descriptor.handle = 1;
    descriptor.allocation_bytes = 4096;
    if (capabilities.device_uuid)
        descriptor.device = *capabilities.device_uuid;
    const Result<Detail::Allocation> imported =
        Internal::MemoryExchange::import_device_memory(context, cache, descriptor, 4096, 0);

    ASSERT_FALSE(imported.has_value());
    EXPECT_EQ(imported.error().code,
              capabilities.device_import ? ErrorCode::InvalidAccess : ErrorCode::Unsupported);
    EXPECT_NE(imported.error().message.find(refused->second), std::string::npos)
        << imported.error().message;
}

