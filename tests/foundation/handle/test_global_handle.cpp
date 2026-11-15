/**************************************************************************/
/* test_global_handle.cpp                                                 */
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
 * @file test_global_handle.cpp
 * @brief Tests GlobalHandle's bit layout, order and hash, and that the module includes no SYCL.
 * @see tests/foundation/handle/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/global_handle.hpp>

using SushiRuntime::GlobalHandle;
using SushiRuntime::HandleFlags;
using SushiRuntime::HandleKind;

namespace
{
    /** @brief Expects handle to keep its fields through the two packed words. */
    void expect_round_trip(const GlobalHandle& handle)
    {
        const GlobalHandle rebuilt =
            GlobalHandle::from_words(handle.to_high_word(), handle.to_low_word());
        EXPECT_EQ(rebuilt.universe(), handle.universe());
        EXPECT_EQ(rebuilt.machine(), handle.machine());
        EXPECT_EQ(rebuilt.kind(), handle.kind());
        EXPECT_EQ(rebuilt.flags(), handle.flags());
        EXPECT_EQ(rebuilt.generation(), handle.generation());
        EXPECT_EQ(rebuilt.slot(), handle.slot());
        EXPECT_EQ(rebuilt, handle);
    }

    /** @brief Returns the folder of the module's public headers, found from this file's path. */
    std::filesystem::path header_folder()
    {
        const std::filesystem::path test_file(__FILE__);
        const std::filesystem::path repository = test_file.parent_path() / ".." / ".." / "..";
        return repository / "modules" / "foundation" / "handle" / "include" / "SushiRuntime" /
               "handle";
    }

    /** @brief Returns text in lower case. */
    std::string lower_case(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char letter)
                       { return static_cast<char>(std::tolower(letter)); });
        return text;
    }
}

TEST(Small_GlobalHandle, TheDefaultHandleIsNull)
{
    EXPECT_TRUE(GlobalHandle().is_null());
    EXPECT_EQ(GlobalHandle(), GlobalHandle::null());
    EXPECT_FALSE(GlobalHandle(0, 0, HandleKind::Buffer, HandleFlags::None, 1, 0).is_null());
}

TEST(Small_GlobalHandle, EachFieldRoundTripsAtItsExtremes)
{
    expect_round_trip(GlobalHandle(UINT32_MAX, 0, HandleKind::Buffer, HandleFlags::None, 0, 0));
    expect_round_trip(GlobalHandle(0, UINT16_MAX, HandleKind::Buffer, HandleFlags::None, 0, 0));
    expect_round_trip(GlobalHandle(0, 0, HandleKind::Reserved, HandleFlags::None, 0, 0));
    expect_round_trip(GlobalHandle(0, 0, static_cast<HandleKind>(0xFF), HandleFlags::None, 0, 0));
    expect_round_trip(GlobalHandle(0, 0, HandleKind::Buffer, static_cast<HandleFlags>(0xFF), 0, 0));
    expect_round_trip(GlobalHandle(0, 0, HandleKind::Buffer, HandleFlags::None, UINT32_MAX, 0));
    expect_round_trip(GlobalHandle(0, 0, HandleKind::Buffer, HandleFlags::None, 0, UINT32_MAX));
    expect_round_trip(GlobalHandle(UINT32_MAX, UINT16_MAX, static_cast<HandleKind>(0xFF),
                                   static_cast<HandleFlags>(0xFF), UINT32_MAX, UINT32_MAX));
}

TEST(Small_GlobalHandle, EachFieldSitsAtItsDocumentedBits)
{
    const GlobalHandle handle(0x11223344U, 0x5566U, HandleKind::Image,
                              HandleFlags::DoNotCache | HandleFlags::HostOnly, 0x8899AABBU,
                              0xCCDDEEFFU);
    EXPECT_EQ(handle.to_high_word(), 0x1122334455660205ULL);
    EXPECT_EQ(handle.to_low_word(), 0x8899AABBCCDDEEFFULL);
}

TEST(Small_GlobalHandle, FlagsCombineAndTest)
{
    const HandleFlags flags = HandleFlags::Borrowed | HandleFlags::HostOnly;
    EXPECT_TRUE(SushiRuntime::has_flags(flags, HandleFlags::Borrowed));
    EXPECT_TRUE(SushiRuntime::has_flags(flags, HandleFlags::HostOnly));
    EXPECT_FALSE(SushiRuntime::has_flags(flags, HandleFlags::DoNotCache));
}

TEST(Small_GlobalHandle, OrderComparesUniverseMachineSlotGenerationKindFlags)
{
    const std::vector<GlobalHandle> ascending = {
        GlobalHandle(0, 0, HandleKind::Buffer, HandleFlags::None, 1, 0),
        GlobalHandle(0, 0, HandleKind::Buffer, HandleFlags::DoNotCache, 1, 0),
        GlobalHandle(0, 0, HandleKind::State, HandleFlags::None, 1, 0),
        GlobalHandle(0, 0, HandleKind::Buffer, HandleFlags::None, 2, 0),
        GlobalHandle(0, 0, HandleKind::Buffer, HandleFlags::None, 1, 1),
        GlobalHandle(0, 1, HandleKind::Buffer, HandleFlags::None, 1, 0),
        GlobalHandle(1, 0, HandleKind::Buffer, HandleFlags::None, 1, 0)};
    for (std::size_t lower = 0; lower < ascending.size(); ++lower)
    {
        for (std::size_t upper = 0; upper < ascending.size(); ++upper)
        {
            SCOPED_TRACE(testing::Message() << lower << " against " << upper);
            EXPECT_EQ(ascending[lower] < ascending[upper], lower < upper);
            EXPECT_EQ(ascending[lower] > ascending[upper], lower > upper);
            EXPECT_EQ(ascending[lower] <= ascending[upper], lower <= upper);
            EXPECT_EQ(ascending[lower] >= ascending[upper], lower >= upper);
            EXPECT_EQ(ascending[lower] == ascending[upper], lower == upper);
            EXPECT_EQ(ascending[lower] != ascending[upper], lower != upper);
        }
    }
    std::vector<GlobalHandle> shuffled(ascending.rbegin(), ascending.rend());
    std::sort(shuffled.begin(), shuffled.end());
    EXPECT_EQ(shuffled, ascending);
}

TEST(Small_GlobalHandle, EqualHandlesHashEqualAndTheHashIsFixed)
{
    const std::hash<GlobalHandle> hash;
    const GlobalHandle handle(7, 3, HandleKind::State, HandleFlags::Borrowed, 9, 42);
    EXPECT_EQ(hash(handle), hash(GlobalHandle(7, 3, HandleKind::State, HandleFlags::Borrowed, 9,
                                              42)));
    EXPECT_EQ(hash(GlobalHandle::null()), hash(GlobalHandle()));

    std::unordered_set<std::size_t> seen;
    for (std::uint32_t slot = 0; slot < 1024; ++slot)
        seen.insert(hash(GlobalHandle(1, 0, HandleKind::Buffer, HandleFlags::None, 1, slot)));
    EXPECT_EQ(seen.size(), 1024U);
}

TEST(Small_HandleModule, IncludesNoSYCLHeaderAndNoOtherModule)
{
    const std::filesystem::path folder = header_folder();
    ASSERT_TRUE(std::filesystem::is_directory(folder)) << folder.string();

    int headers = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(folder))
    {
        if (!entry.is_regular_file())
            continue;
        ++headers;
        std::ifstream file(entry.path());
        std::string line;
        while (std::getline(file, line))
        {
            if (line.find("#include") == std::string::npos)
                continue;
            SCOPED_TRACE(entry.path().filename().string() + ": " + line);
            EXPECT_EQ(lower_case(line).find("sycl"), std::string::npos);
            if (line.find("SushiRuntime/") != std::string::npos)
                EXPECT_NE(line.find("<SushiRuntime/handle/"), std::string::npos);
        }
    }
    EXPECT_GE(headers, 4);
}
