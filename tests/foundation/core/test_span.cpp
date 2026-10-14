/**************************************************************************/
/* test_span.cpp                                                          */
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
 * @file test_span.cpp
 * @brief Tests SushiRuntime::span: construction, subviews, and the container binding rules.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>

#if __cplusplus >= 202002L
    #include <span>
#endif

namespace
{
    using SushiRuntime::span;

    constexpr int K_VALUES[] = {1, 2, 3, 4};

    /** @brief Sums the elements of a view of const ints. */
    int sum(span<const int> values)
    {
        int total = 0;
        for (int value : values)
        {
            total += value;
        }
        return total;
    }

    // The container binding rules as compile-time facts.
    static_assert(!std::is_constructible_v<span<int>, std::vector<int>&&>,
                  "a non-const span must not bind to an rvalue container");
    static_assert(std::is_constructible_v<span<const int>, std::vector<int>&&>,
                  "a const span must bind to an rvalue container");
    static_assert(std::is_constructible_v<span<int>, std::vector<int>&>,
                  "a non-const span must bind to an lvalue container");
    static_assert(span<const int>(K_VALUES).size() == 4,
                  "a span is usable in constant expressions");

#if __cplusplus >= 202002L
    // Under C++20 the class stays its own type and converts to and from std::span.
    static_assert(!std::is_same_v<span<int>, std::span<int>>,
                  "SushiRuntime::span is a class of its own under C++20");
    static_assert(std::is_convertible_v<std::span<int>, span<int>>,
                  "a std::span converts to SushiRuntime::span");
    static_assert(std::is_convertible_v<span<int>, std::span<int>>,
                  "SushiRuntime::span converts to std::span");
#endif

    TEST(Unit_Span, DefaultIsEmpty)
    {
        span<int> view;
        EXPECT_TRUE(view.empty());
        EXPECT_EQ(view.size(), 0U);
        EXPECT_TRUE(view.data() == nullptr);
    }

    TEST(Unit_Span, ConstructsFromPointerAndCount)
    {
        int values[] = {10, 20, 30};
        span<int> view(values, 3);
        EXPECT_EQ(view.data(), &values[0]);
        EXPECT_EQ(view.size(), 3U);
        EXPECT_EQ(view.size_bytes(), 3 * sizeof(int));
        EXPECT_EQ(view[1], 20);
    }

    TEST(Unit_Span, ConstructsFromCArray)
    {
        int values[] = {1, 2, 3, 4};
        span<int> view = values;
        EXPECT_EQ(view.size(), 4U);
        EXPECT_EQ(view.back(), 4);
    }

    TEST(Unit_Span, ConstructsFromLvalueVector)
    {
        std::vector<int> values{5, 6, 7};
        span<int> view = values;
        EXPECT_EQ(view.data(), values.data());
        EXPECT_EQ(view.size(), values.size());

        view[0] = 50;
        EXPECT_EQ(values[0], 50);
    }

    TEST(Unit_Span, ConstructsFromStdArray)
    {
        std::array<int, 3> values{8, 9, 10};
        span<int> view = values;
        EXPECT_EQ(view.size(), values.size());
        EXPECT_EQ(view.front(), 8);
    }

    TEST(Unit_Span, ConstViewOverConstContainer)
    {
        const std::vector<int> values{1, 2, 3};
        span<const int> view = values;
        EXPECT_EQ(view.size(), 3U);
        EXPECT_EQ(view.data(), values.data());
    }

    TEST(Unit_Span, ConstViewOverNonConstLvalue)
    {
        std::vector<int> values{1, 2, 3};
        span<const int> view = values;
        EXPECT_EQ(sum(view), 6);
    }

    TEST(Unit_Span, AcceptsTemporaryContainerForConstElements)
    {
        EXPECT_EQ(sum(std::vector<int>{1, 2, 3}), 6);
    }

    TEST(Unit_Span, FirstLastAndSubspanSelectElements)
    {
        span<const int> all(K_VALUES, 4);

        span<const int> head = all.first(2);
        EXPECT_EQ(head.size(), 2U);
        EXPECT_EQ(head[1], 2);

        span<const int> tail = all.last(2);
        EXPECT_EQ(tail.size(), 2U);
        EXPECT_EQ(tail.data(), K_VALUES + 2);

        span<const int> middle = all.subspan(1, 2);
        EXPECT_EQ(middle.size(), 2U);
        EXPECT_EQ(middle.front(), 2);
        EXPECT_EQ(middle.back(), 3);

        span<const int> rest = all.subspan(1);
        EXPECT_EQ(rest.size(), 3U);
        EXPECT_EQ(rest.back(), 4);
    }

#if __cplusplus >= 202002L
    TEST(Unit_Span, ConvertsToAndFromStdSpan)
    {
        std::array<int, 3> values{1, 2, 3};
        std::span<int> standard(values);

        span<int> view = standard;
        EXPECT_EQ(view.data(), values.data());
        EXPECT_EQ(view.size(), 3U);

        std::span<int> back = view;
        EXPECT_EQ(back.data(), values.data());
        EXPECT_EQ(back.size(), 3U);
    }
#endif
}
