/**************************************************************************/
/* test_element_layout.cpp                                                */
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
 * @file test_element_layout.cpp
 * @brief Tests ElementLayout: which layouts are raw bytes and when two layouts are equal.
 * @see tests/foundation/handle/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/element_layout.hpp>

using SushiRuntime::ElementLayout;

namespace
{
    /** @brief Tests that a layout of one byte aligned to one byte is raw bytes and no other layout is. */
    TEST(Small_ElementLayout, OnlyOneByteAlignedToOneByteIsRawBytes)
    {
        EXPECT_TRUE(ElementLayout().is_bytes());
        EXPECT_FALSE((ElementLayout{4, 4}).is_bytes());
        EXPECT_FALSE((ElementLayout{1, 2}).is_bytes());
    }

    /** @brief Tests that two layouts are equal exactly when size and alignment both match. */
    TEST(Small_ElementLayout, LayoutsAreEqualWhenSizeAndAlignmentBothMatch)
    {
        EXPECT_EQ((ElementLayout{8, 4}), (ElementLayout{8, 4}));
        EXPECT_NE((ElementLayout{8, 4}), (ElementLayout{8, 8}));
        EXPECT_NE((ElementLayout{8, 4}), (ElementLayout{4, 4}));
    }
} // namespace
