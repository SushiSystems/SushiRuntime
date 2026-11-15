/**************************************************************************/
/* test_bound_operands.cpp                                                */
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
 * @file test_bound_operands.cpp
 * @brief Tests BoundOperands: typed access by operand position, checked against the buffer's layout.
 * @see tests/foundation/handle/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/bound_operands.hpp>

using SushiRuntime::BoundOperands;
using SushiRuntime::ElementLayout;
using SushiRuntime::OperandStatus;

namespace
{
    /** @brief The layout of a buffer of float. */
    constexpr ElementLayout K_FLOAT_LAYOUT{sizeof(float), alignof(float)};

    /** @brief The layout of a buffer of raw bytes. */
    constexpr ElementLayout K_BYTE_LAYOUT{1, 1};

    /** @brief Holds one operand table of a float buffer, an aligned byte buffer and an absent buffer. */
    struct Table
    {
        std::array<float, 4> floats{1.0F, 2.0F, 3.0F, 4.0F}; /**< Buffer of float. */
        alignas(8) std::array<unsigned char, 32> bytes{};     /**< Buffer of raw bytes, aligned for any element. */
        std::array<void*, 3> pointers{floats.data(), bytes.data(), nullptr}; /**< Operand 2 has no instance. */
        std::array<ElementLayout, 3> layouts{K_FLOAT_LAYOUT, K_BYTE_LAYOUT, K_FLOAT_LAYOUT}; /**< Layout of each operand. */

        /** @brief Returns the view of the table. */
        BoundOperands view() noexcept
        {
            return BoundOperands(pointers.data(), layouts.data(), pointers.size());
        }
    };

    /** @brief Tests that an operand is viewed as its own element type at its table address. */
    TEST(Small_BoundOperands, ATypedBufferIsViewedAsItsElementType)
    {
        Table table;

        const auto operand = table.view().at<float>(0);

        EXPECT_EQ(operand.status, OperandStatus::Ok);
        EXPECT_EQ(operand.address, table.floats.data());
    }

    /** @brief Tests that const does not change which element type an operand is viewed as. */
    TEST(Small_BoundOperands, ATypeWithConstIsViewedAsTheSameElementType)
    {
        Table table;

        const auto operand = table.view().at<const float>(0);

        EXPECT_EQ(operand.status, OperandStatus::Ok);
        EXPECT_EQ(operand.address, table.floats.data());
    }

    /** @brief Tests that a typed buffer refuses an element type of another size. */
    TEST(Small_BoundOperands, ATypedBufferRefusesAnElementTypeOfAnotherSize)
    {
        Table table;

        const auto operand = table.view().at<double>(0);

        EXPECT_EQ(operand.status, OperandStatus::ElementMismatch);
        EXPECT_EQ(operand.address, nullptr);
    }

    /** @brief Tests that a typed buffer refuses an element type of the same size and another alignment. */
    TEST(Small_BoundOperands, ATypedBufferRefusesAnElementTypeOfAnotherAlignment)
    {
        struct alignas(8) Wide
        {
            float value;
        };
        std::array<void*, 1> pointers{nullptr};
        const std::array<ElementLayout, 1> layouts{ElementLayout{sizeof(Wide), 4}};
        const BoundOperands view(pointers.data(), layouts.data(), 1);

        const auto operand = view.at<Wide>(0);

        EXPECT_EQ(operand.status, OperandStatus::ElementMismatch);
    }

    /** @brief Tests that a buffer of raw bytes is viewed as any element type at an aligned offset. */
    TEST(Small_BoundOperands, ARawByteBufferIsViewedAsAnyElementTypeAtAnAlignedOffset)
    {
        Table table;

        const auto operand = table.view().at<double>(1, 8);

        EXPECT_EQ(operand.status, OperandStatus::Ok);
        EXPECT_EQ(operand.address, reinterpret_cast<double*>(table.bytes.data() + 8));
    }

    /** @brief Tests that a raw byte buffer refuses an offset that is not aligned for the element type. */
    TEST(Small_BoundOperands, ARawByteBufferRefusesAnOffsetThatIsNotAlignedForTheType)
    {
        Table table;

        const auto operand = table.view().at<float>(1, 2);

        EXPECT_EQ(operand.status, OperandStatus::Misaligned);
        EXPECT_EQ(operand.address, nullptr);
    }

    /** @brief Tests that a typed buffer is offset by whole elements only. */
    TEST(Small_BoundOperands, ATypedBufferIsOffsetByWholeElementsOnly)
    {
        Table table;

        const auto whole = table.view().at<float>(0, 2 * sizeof(float));
        const auto partial = table.view().at<float>(0, 2);

        EXPECT_EQ(whole.address, table.floats.data() + 2);
        EXPECT_EQ(partial.status, OperandStatus::Misaligned);
    }

    /** @brief Tests that a position past the last operand is refused. */
    TEST(Small_BoundOperands, APositionPastTheLastOperandIsRefused)
    {
        Table table;

        const auto operand = table.view().at<float>(3);

        EXPECT_EQ(operand.status, OperandStatus::OutOfRange);
        EXPECT_EQ(operand.address, nullptr);
    }

    /** @brief Tests that an operand whose buffer has no instance reads as a null address that is not an error. */
    TEST(Small_BoundOperands, AnOperandWithNoInstanceReadsAsNull)
    {
        Table table;

        const auto operand = table.view().at<float>(2, sizeof(float));

        EXPECT_EQ(operand.status, OperandStatus::Ok);
        EXPECT_EQ(operand.address, nullptr);
    }

    /** @brief Tests that a view of no operand is empty and refuses every position. */
    TEST(Small_BoundOperands, AViewOfNoOperandIsEmptyAndRefusesEveryPosition)
    {
        const BoundOperands view;

        EXPECT_TRUE(view.is_empty());
        EXPECT_EQ(view.size(), 0U);
        EXPECT_EQ(view.at<float>(0).status, OperandStatus::OutOfRange);
    }

    /** @brief Tests that a view made of bytes treats every operand as a raw byte buffer. */
    TEST(Small_BoundOperands, AViewOfBytesTreatsEveryOperandAsRawBytes)
    {
        Table table;
        const BoundOperands view = BoundOperands::of_bytes(table.pointers.data() + 1, 1);

        const auto operand = view.at<double>(0);

        EXPECT_EQ(view.layout_of(0), K_BYTE_LAYOUT);
        EXPECT_EQ(operand.address, reinterpret_cast<double*>(table.bytes.data()));
    }

    /** @brief Tests that untyped() returns the table entry unchanged. */
    TEST(Small_BoundOperands, UntypedReturnsTheTableEntryUnchanged)
    {
        Table table;

        EXPECT_EQ(table.view().untyped(1), table.bytes.data());
    }
} // namespace
