/**************************************************************************/
/* test_external_handle_kind_set.cpp                                      */
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
 * @file test_external_handle_kind_set.cpp
 * @brief Tests ExternalHandleKindSet: membership, removal and comparison by members.
 * @see tests/surface/interop/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/interop/external_handle_kind_set.hpp>
#include <SushiRuntime/interop/external_memory.hpp>

using SushiRuntime::ExternalHandleKind;
using SushiRuntime::ExternalHandleKindSet;

/** @brief Tests that a set holds the kind inserted and no other. */
TEST(Small_ExternalHandleKindSet, HoldsOnlyTheKindInserted)
{
    ExternalHandleKindSet kinds;

    kinds.insert(ExternalHandleKind::OpaqueFd);

    EXPECT_FALSE(kinds.is_empty());
    EXPECT_TRUE(kinds.contains(ExternalHandleKind::OpaqueFd));
    EXPECT_FALSE(kinds.contains(ExternalHandleKind::OpaqueWin32));
    EXPECT_FALSE(kinds.contains(ExternalHandleKind::Unspecified));
}

/** @brief Tests that a set is empty again once its only kind is erased. */
TEST(Small_ExternalHandleKindSet, IsEmptyAgainAfterErasingItsKind)
{
    ExternalHandleKindSet kinds;
    kinds.insert(ExternalHandleKind::OpaqueFd);

    kinds.erase(ExternalHandleKind::OpaqueFd);

    EXPECT_TRUE(kinds.is_empty());
    EXPECT_EQ(kinds, ExternalHandleKindSet());
}

/** @brief Tests that inserting a kind the set already holds keeps one member. */
TEST(Small_ExternalHandleKindSet, InsertingAKindTwiceKeepsOneMember)
{
    ExternalHandleKindSet kinds;
    kinds.insert(ExternalHandleKind::OpaqueWin32);
    const ExternalHandleKindSet before = kinds;

    kinds.insert(ExternalHandleKind::OpaqueWin32);

    EXPECT_EQ(kinds, before);
}

/** @brief Tests that erasing one kind leaves another kind in the set. */
TEST(Small_ExternalHandleKindSet, EraseRemovesOnlyItsKind)
{
    ExternalHandleKindSet kinds;
    kinds.insert(ExternalHandleKind::OpaqueFd);
    kinds.insert(ExternalHandleKind::OpaqueWin32);

    kinds.erase(ExternalHandleKind::OpaqueWin32);

    EXPECT_FALSE(kinds.contains(ExternalHandleKind::OpaqueWin32));
    EXPECT_TRUE(kinds.contains(ExternalHandleKind::OpaqueFd));
}

/** @brief Tests that two sets compare equal exactly when they hold the same kinds. */
TEST(Small_ExternalHandleKindSet, SetsCompareByMembers)
{
    ExternalHandleKindSet fd;
    fd.insert(ExternalHandleKind::OpaqueFd);
    ExternalHandleKindSet win32;
    win32.insert(ExternalHandleKind::OpaqueWin32);

    EXPECT_NE(fd, win32);
    EXPECT_FALSE(fd == win32);
    win32.insert(ExternalHandleKind::OpaqueFd);
    win32.erase(ExternalHandleKind::OpaqueWin32);
    EXPECT_EQ(fd, win32);
}

// The constexpr insert holds its kind and adds no other; the compiler checks it, no test runs it.
static_assert(
    []()
    {
        ExternalHandleKindSet built;
        built.insert(ExternalHandleKind::OpaqueFd);
        return built.contains(ExternalHandleKind::OpaqueFd) && !built.contains(ExternalHandleKind::OpaqueWin32);
    }(),
    "a constexpr insert must hold its kind and no other");
