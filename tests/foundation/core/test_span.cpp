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
 * @file test_span.cpp
 * @brief Tests SushiRuntime::span: construction, subviews, and the container binding rules.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <iterator>
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

    TEST(Small_Span, DefaultIsEmpty)
    {
        span<int> view;
        EXPECT_TRUE(view.empty());
        EXPECT_EQ(view.size(), 0U);
        EXPECT_TRUE(view.data() == nullptr);
    }

    TEST(Small_Span, ConstructsFromPointerAndCount)
    {
        int values[] = {10, 20, 30};
        span<int> view(values, 3);
        EXPECT_EQ(view.data(), &values[0]);
        EXPECT_EQ(view.size(), 3U);
        EXPECT_EQ(view.size_bytes(), 3 * sizeof(int));
        EXPECT_EQ(view[1], 20);
    }

    /** @brief Expects a span built from @p container to alias its elements. */
    template <typename Container>
    void expect_span_aliases(Container& container)
    {
        span<int> view = container;
        EXPECT_EQ(view.data(), std::data(container));
        EXPECT_EQ(view.size(), std::size(container));
    }

    /** @brief Expects a const span built from @p container to alias its elements. */
    template <typename Container>
    void expect_const_span_aliases(Container& container)
    {
        span<const int> view = container;
        EXPECT_EQ(view.data(), std::data(container));
        EXPECT_EQ(view.size(), std::size(container));
    }

    TEST(Small_Span, ConstructsFromAnLvalueContiguousContainer)
    {
        int c_array[] = {1, 2, 3, 4};
        std::vector<int> vector{5, 6, 7};
        std::array<int, 3> std_array{8, 9, 10};

        expect_span_aliases(c_array);
        expect_span_aliases(vector);
        expect_span_aliases(std_array);
    }

    TEST(Small_Span, AnLvalueVectorSpanWritesThrough)
    {
        std::vector<int> values{5, 6, 7};
        span<int> view = values;

        view[0] = 50;
        EXPECT_EQ(values[0], 50);
    }

    TEST(Small_Span, ConstViewAliasesAConstOrNonConstContainer)
    {
        const std::vector<int> constant{1, 2, 3};
        std::vector<int> mutable_values{1, 2, 3};

        expect_const_span_aliases(constant);
        expect_const_span_aliases(mutable_values);
    }

    TEST(Small_Span, AcceptsTemporaryContainerForConstElements)
    {
        EXPECT_EQ(sum(std::vector<int>{1, 2, 3}), 6);
    }

    TEST(Small_Span, FirstSelectsTheLeadingElements)
    {
        span<const int> all(K_VALUES, 4);

        span<const int> head = all.first(2);
        EXPECT_EQ(head.size(), 2U);
        EXPECT_EQ(head[1], 2);
    }

    TEST(Small_Span, LastSelectsTheTrailingElements)
    {
        span<const int> all(K_VALUES, 4);

        span<const int> tail = all.last(2);
        EXPECT_EQ(tail.size(), 2U);
        EXPECT_EQ(tail.data(), K_VALUES + 2);
    }

    TEST(Small_Span, SubspanSelectsAWindow)
    {
        span<const int> all(K_VALUES, 4);

        span<const int> middle = all.subspan(1, 2);
        EXPECT_EQ(middle.size(), 2U);
        EXPECT_EQ(middle.front(), 2);
        EXPECT_EQ(middle.back(), 3);

        span<const int> rest = all.subspan(1);
        EXPECT_EQ(rest.size(), 3U);
        EXPECT_EQ(rest.back(), 4);
    }

#if __cplusplus >= 202002L
    TEST(Small_Span, ConvertsToAndFromStdSpan)
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
