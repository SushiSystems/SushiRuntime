/**************************************************************************/
/* test_buffer_handle.cpp                                                 */
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
 * @file test_buffer_handle.cpp
 * @brief Tests BufferHandle: it names its buffer by a GlobalHandle, and a default one names none.
 * @see tests/extension/transport/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that a default buffer handle names no buffer and holds no version or size. */
TEST(Small_BufferHandle, ADefaultHandleNamesNoBuffer)
{
    const BufferHandle handle;

    EXPECT_TRUE(handle.id.is_null());
    EXPECT_EQ(handle.version, 0u);
    EXPECT_EQ(handle.size_bytes, 0u);
}

/** @brief Tests that a buffer handle keeps all 128 bits of the global handle it names. */
TEST(Small_BufferHandle, AHandleKeepsTheWholeGlobalHandle)
{
    const GlobalHandle named(0x11223344u, 0x0506u, HandleKind::Buffer, HandleFlags::HostOnly, 7u,
                             0x0A0B0C0Du);
    BufferHandle handle;
    handle.id = named;

    EXPECT_FALSE(handle.id.is_null());
    EXPECT_EQ(handle.id, named);
    EXPECT_EQ(handle.id.to_high_word(), named.to_high_word());
    EXPECT_EQ(handle.id.to_low_word(), named.to_low_word());
}
