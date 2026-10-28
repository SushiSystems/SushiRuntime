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
using SushiRuntime::K_NAMED_HANDLE_KINDS;

/** @brief Tests that a default set holds no kind. */
TEST(Small_ExternalHandleKindSet, ADefaultSetIsEmpty)
{
    const ExternalHandleKindSet kinds;
    EXPECT_TRUE(kinds.is_empty());
    EXPECT_FALSE(kinds.contains(ExternalHandleKind::Unspecified));
    for (const ExternalHandleKind kind : K_NAMED_HANDLE_KINDS)
        EXPECT_FALSE(kinds.contains(kind));
}

/** @brief Tests that a set holds the kind inserted and no other, and is empty once it is erased. */
TEST(Small_ExternalHandleKindSet, AKindSetHoldsExactlyTheKindsInserted)
{
    ExternalHandleKindSet kinds;
    kinds.insert(ExternalHandleKind::OpaqueFd);

    EXPECT_FALSE(kinds.is_empty());
    EXPECT_TRUE(kinds.contains(ExternalHandleKind::OpaqueFd));
    EXPECT_FALSE(kinds.contains(ExternalHandleKind::OpaqueWin32));
    EXPECT_FALSE(kinds.contains(ExternalHandleKind::Unspecified));

    kinds.erase(ExternalHandleKind::OpaqueFd);

    EXPECT_TRUE(kinds.is_empty());
    EXPECT_EQ(kinds, ExternalHandleKindSet());
}

/** @brief Tests that a second insert keeps one member and erasing an absent kind changes nothing. */
TEST(Small_ExternalHandleKindSet, InsertAndEraseAreIdempotent)
{
    ExternalHandleKindSet once;
    once.insert(ExternalHandleKind::OpaqueWin32);
    ExternalHandleKindSet twice = once;
    twice.insert(ExternalHandleKind::OpaqueWin32);
    EXPECT_EQ(twice, once);

    twice.erase(ExternalHandleKind::OpaqueFd);
    EXPECT_EQ(twice, once) << "erasing a kind the set does not hold removed another";
}

/** @brief Tests that erasing one kind leaves the others in place. */
TEST(Small_ExternalHandleKindSet, EraseRemovesOnlyItsKind)
{
    ExternalHandleKindSet kinds;
    for (const ExternalHandleKind kind : K_NAMED_HANDLE_KINDS)
        kinds.insert(kind);
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

/** @brief Tests that the set works in a constant expression. */
TEST(Small_ExternalHandleKindSet, WorksAtCompileTime)
{
    constexpr ExternalHandleKindSet kinds = []()
    {
        ExternalHandleKindSet built;
        built.insert(ExternalHandleKind::OpaqueFd);
        return built;
    }();
    static_assert(kinds.contains(ExternalHandleKind::OpaqueFd), "the constexpr insert did not hold");
    static_assert(!kinds.contains(ExternalHandleKind::OpaqueWin32), "the insert added another kind");
    EXPECT_FALSE(kinds.is_empty());
}
