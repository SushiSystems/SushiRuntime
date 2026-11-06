/**************************************************************************/
/* test_context_handle_access.cpp                                         */
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
 * @file test_context_handle_access.cpp
 * @brief Tests ContextHandleAccess: wrap, share and the handle an owner holds.
 * @see tests/surface/context/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/context/context.hpp>
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/context/context_handle_access.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Holds a handle under the member name the accessor reads. */
    struct Owner
    {
        ContextHandle context_; /**< The handle handle_of() returns. */
    };
}

/** @brief Tests that an empty share gives an empty handle and shares nothing back. */
TEST(Small_ContextHandleAccess, AnEmptyShareGivesAnEmptyHandle)
{
    const ContextHandle handle = Detail::ContextHandleAccess::wrap(nullptr);
    EXPECT_FALSE(handle);
    EXPECT_EQ(Detail::ContextHandleAccess::share(handle), nullptr);
}

/** @brief Tests that handle_of() returns the owner's handle itself, not a copy. */
TEST(Small_ContextHandleAccess, HandleOfReturnsTheOwnersHandle)
{
    const Owner owner;
    EXPECT_EQ(&Detail::ContextHandleAccess::handle_of(owner), &owner.context_);
}
