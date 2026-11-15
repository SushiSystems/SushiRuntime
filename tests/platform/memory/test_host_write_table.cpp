/**************************************************************************/
/* test_host_write_table.cpp                                              */
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
 * @file test_host_write_table.cpp
 * @brief Tests HostWriteTable: the marks of host writes, kept apart from the residency directory.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/host_write_table.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Memory;

namespace
{
    constexpr std::uint32_t K_UNIVERSE = 7;

    /** @brief Returns a handle minted in directory, failing the test when none is minted. */
    GlobalHandle mint_buffer(ResidencyDirectory& directory)
    {
        const MintOutcome outcome = directory.mint(HandleKind::Buffer, HandleFlags::None);
        EXPECT_EQ(outcome.status, HandleStatus::Ok);
        return outcome.handle;
    }

    /** @brief Mints count buffers in directory and returns the last. */
    GlobalHandle mint_buffers(ResidencyDirectory& directory, std::uint32_t count)
    {
        GlobalHandle last = GlobalHandle::null();
        for (std::uint32_t i = 0; i < count; ++i)
            last = mint_buffer(directory);
        return last;
    }
} // namespace

/** @brief Tests that a host write marks only the written handle, at the new generation. */
TEST(Small_HostWriteTable, AHostWriteMarksOnlyTheWrittenHandleAtTheNewGeneration)
{
    ResidencyDirectory directory(K_UNIVERSE);
    HostWriteTable table(directory);
    const GlobalHandle written = mint_buffer(directory);
    const GlobalHandle untouched = mint_buffer(directory);

    ASSERT_EQ(table.record_host_write(written), HandleStatus::Ok);

    EXPECT_EQ(table.host_write_generation(), 1U);
    EXPECT_EQ(table.host_write_of(written), 1U);
    EXPECT_EQ(table.host_write_of(untouched), 0U);
}

/** @brief Tests that a second write on a handle raises its mark and the generation. */
TEST(Small_HostWriteTable, ASecondWriteRaisesTheMarkOfTheSameHandle)
{
    ResidencyDirectory directory(K_UNIVERSE);
    HostWriteTable table(directory);
    const GlobalHandle handle = mint_buffer(directory);

    ASSERT_EQ(table.record_host_write(handle), HandleStatus::Ok);
    ASSERT_EQ(table.record_host_write(handle), HandleStatus::Ok);

    EXPECT_EQ(table.host_write_generation(), 2U);
    EXPECT_EQ(table.host_write_of(handle), 2U);
}

/** @brief Tests that the mark of a retired handle is not seen on its reused slot or by the old handle. */
TEST(Small_HostWriteTable, ARetiredHandlesHostWriteIsNotSeenOnItsReusedSlot)
{
    ResidencyDirectory directory(K_UNIVERSE);
    HostWriteTable table(directory);
    const GlobalHandle retired = mint_buffer(directory);
    ASSERT_EQ(table.record_host_write(retired), HandleStatus::Ok);
    ASSERT_EQ(directory.retire(retired), HandleStatus::Ok);

    const GlobalHandle reused = mint_buffer(directory);

    ASSERT_EQ(reused.slot(), retired.slot());
    EXPECT_EQ(table.host_write_of(reused), 0U);
    EXPECT_EQ(table.host_write_of(retired), 0U);
}

/** @brief Tests that a write on a retired handle is refused and raises no generation. */
TEST(Small_HostWriteTable, AHostWriteOnARetiredHandleIsRefusedAndRaisesNoGeneration)
{
    ResidencyDirectory directory(K_UNIVERSE);
    HostWriteTable table(directory);
    const GlobalHandle retired = mint_buffer(directory);
    ASSERT_EQ(directory.retire(retired), HandleStatus::Ok);

    EXPECT_EQ(table.record_host_write(retired), HandleStatus::StaleHandle);
    EXPECT_EQ(table.host_write_generation(), 0U);
}

/** @brief Tests that the null handle is refused and reads as unwritten. */
TEST(Small_HostWriteTable, TheNullHandleIsRefused)
{
    ResidencyDirectory directory(K_UNIVERSE);
    HostWriteTable table(directory);

    EXPECT_EQ(table.record_host_write(GlobalHandle::null()), HandleStatus::NullHandle);
    EXPECT_EQ(table.host_write_of(GlobalHandle::null()), 0U);
}

/** @brief Tests that a mark in a later chunk is kept beside one in the first chunk. */
TEST(Small_HostWriteTable, MarksSurviveGrowthPastOneChunk)
{
    ResidencyDirectory directory(K_UNIVERSE);
    HostWriteTable table(directory);
    const GlobalHandle first = mint_buffer(directory);
    ASSERT_EQ(table.record_host_write(first), HandleStatus::Ok);

    const GlobalHandle last = mint_buffers(directory, HostWriteTable::K_CHUNK_MARKS);
    ASSERT_EQ(table.record_host_write(last), HandleStatus::Ok);

    EXPECT_EQ(table.host_write_of(first), 1U);
    EXPECT_EQ(table.host_write_of(last), 2U);
}
