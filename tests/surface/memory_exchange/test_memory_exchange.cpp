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

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>
#include <SushiRuntime/memory/allocation.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>

#include <SushiRuntime/memory_exchange/memory_exchange.hpp>

using namespace SushiRuntime;

/** @brief Tests that device 0 reports its capabilities. */
TEST(Integration_MemoryExchange, CapabilitiesOfDeviceZero)
{
    const auto context = SushiRuntime::Test::make_context();
    EXPECT_TRUE(Internal::MemoryExchange::query_capabilities(*context, 0).has_value());
}

/** @brief Tests that a host export gives an allocation of the size asked when the device can export. */
TEST(Integration_MemoryExchange, AHostExportGivesTheSizeAsked)
{
    const auto context = SushiRuntime::Test::make_context();
    const InteropCapabilities capabilities =
        Internal::MemoryExchange::query_capabilities(*context, 0).value();
    if (!capabilities.host_export)
        GTEST_SKIP() << "device 0 cannot export host memory";
    const Result<Internal::Memory::Allocation> exported =
        Internal::MemoryExchange::export_host_memory(context, 4096, 0, 4096);
    ASSERT_TRUE(exported.has_value()) << exported.error().message;
    EXPECT_EQ(exported.value().bytes(), 4096u);
}

/** @brief Tests that an import naming no handle kind is refused with InvalidAccess. */
TEST(Integration_MemoryExchange, AnImportWithNoKindIsRefused)
{
    const auto context = SushiRuntime::Test::make_context();
    ExternalMemoryDescriptor descriptor;
    descriptor.allocation_bytes = 4096;
    const Result<Internal::Memory::Allocation> imported =
        Internal::MemoryExchange::import_device_memory(context, descriptor, 4096, 0);
    ASSERT_FALSE(imported.has_value());
    EXPECT_EQ(imported.error().code, ErrorCode::InvalidAccess);
    EXPECT_NE(imported.error().message.find("Unspecified"), std::string::npos);
}
