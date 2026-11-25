/**************************************************************************/
/* test_page_protection.cpp                                               */
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
 * @file test_page_protection.cpp
 * @brief Tests the page calls that map, protect and unmap guarded memory.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/page_protection.hpp>

using SushiRuntime::Internal::Memory::map_pages;
using SushiRuntime::Internal::Memory::page_bytes;
using SushiRuntime::Internal::Memory::PageAccess;
using SushiRuntime::Internal::Memory::protect_pages;
using SushiRuntime::Internal::Memory::unmap_pages;

namespace
{
    /** @brief Reads one byte through an address. */
    unsigned char read_byte(const void* address)
    {
        return *static_cast<const volatile unsigned char*>(address);
    }

    /** @brief Writes one byte through an address. */
    void write_byte(void* address, unsigned char value)
    {
        *static_cast<volatile unsigned char*>(address) = value;
    }
} // namespace

/** @brief Tests that read-write pages start zeroed and take a write. */
TEST(Small_PageProtection, ReadWritePagesStartZeroedAndTakeAWrite)
{
    void* const first = map_pages(page_bytes(), PageAccess::ReadWrite);
    ASSERT_NE(first, nullptr);
    const unsigned char before = read_byte(first);

    write_byte(first, 0x6B);

    EXPECT_EQ(before, 0u);
    EXPECT_EQ(read_byte(first), 0x6B);
    unmap_pages(first, page_bytes());
}

/** @brief Tests that pages closed and opened again keep the bytes written before. */
TEST(Small_PageProtection, PagesOpenedAgainKeepTheirBytes)
{
    void* const first = map_pages(page_bytes(), PageAccess::ReadWrite);
    ASSERT_NE(first, nullptr);
    write_byte(first, 0x2D);

    const bool closed = protect_pages(first, page_bytes(), PageAccess::None);
    const bool opened = protect_pages(first, page_bytes(), PageAccess::ReadWrite);

    EXPECT_TRUE(closed);
    EXPECT_TRUE(opened);
    EXPECT_EQ(read_byte(first), 0x2D);
    unmap_pages(first, page_bytes());
}

/** @brief Tests that a read of pages with no access kills the process. A death test starts a child process. */
TEST(Medium_PageProtectionDeathTest, AReadOfPagesWithNoAccessFaults)
{
    void* const first = map_pages(page_bytes(), PageAccess::None);
    ASSERT_NE(first, nullptr);

    EXPECT_DEATH(static_cast<void>(read_byte(first)), "");

    unmap_pages(first, page_bytes());
}
