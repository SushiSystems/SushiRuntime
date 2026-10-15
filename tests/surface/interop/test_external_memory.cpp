/**************************************************************************/
/* test_external_memory.cpp                                               */
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
 * @file test_external_memory.cpp
 * @brief Tests ExternalMemoryDescriptor: an unspecified handle of no bytes by default.
 * @see tests/surface/interop/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/interop/device_uuid.hpp>
#include <SushiRuntime/interop/external_memory.hpp>

using namespace SushiRuntime;

/** @brief Tests the defaults of a descriptor and the three handle kinds. */
TEST(Small_ExternalMemory, DefaultsNameNoHandle)
{
    const ExternalMemoryDescriptor descriptor;
    EXPECT_EQ(descriptor.kind, ExternalHandleKind::Unspecified);
    EXPECT_EQ(descriptor.handle, 0u);
    EXPECT_EQ(descriptor.allocation_bytes, 0u);
    EXPECT_EQ(descriptor.offset, 0u);
    EXPECT_EQ(descriptor.device, DeviceUUID{});
    static_assert(std::is_same_v<std::underlying_type_t<ExternalHandleKind>, std::uint8_t>, "one byte");
    EXPECT_NE(ExternalHandleKind::OpaqueWin32, ExternalHandleKind::OpaqueFd);
}
