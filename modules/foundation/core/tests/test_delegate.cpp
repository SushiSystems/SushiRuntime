/**************************************************************************/
/* test_delegate.cpp                                                      */
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
 * @file test_delegate.cpp
 * @brief Tests Core::Delegate: empty state, invocation, results, moves and destruction.
 * @see modules/foundation/core/tests/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <type_traits>
#include <utility>
#include <gtest/gtest.h>
#include <SushiRuntime/core/delegate.hpp>

using SushiRuntime::Core::Delegate;

namespace
{
    TEST(Unit_Delegate, EmptyIsFalsy)
    {
        Delegate<int()> d;
        EXPECT_FALSE(static_cast<bool>(d));

        Delegate<int()> n = nullptr;
        EXPECT_FALSE(static_cast<bool>(n));
    }

    TEST(Unit_Delegate, InvokesAndReturns)
    {
        Delegate<int()> d = [] { return 42; };
        ASSERT_TRUE(static_cast<bool>(d));
        EXPECT_EQ(d(), 42);
    }

    TEST(Unit_Delegate, ForwardsArguments)
    {
        Delegate<int(int, int)> add = [](int a, int b) { return a + b; };
        EXPECT_EQ(add(3, 4), 7);
    }

    TEST(Unit_Delegate, CapturesState)
    {
        int base = 10;
        Delegate<int(int)> d = [base](int x) { return base + x; };
        EXPECT_EQ(d(5), 15);
    }

    TEST(Unit_Delegate, VoidReturn)
    {
        int sink = 0;
        Delegate<void(int)> d = [&sink](int v) { sink = v; };
        d(99);
        EXPECT_EQ(sink, 99);
    }

    TEST(Unit_Delegate, VoidSignatureDiscardsResultOfValueCallable)
    {
        int sink = 0;
        Delegate<void(int)> d = [&sink](int v)
        {
            sink = v;
            return v;
        };
        ASSERT_TRUE(static_cast<bool>(d));
        d(99);
        EXPECT_EQ(sink, 99);
    }

    TEST(Unit_Delegate, MoveLeavesSourceEmpty)
    {
        Delegate<int()> a = [] { return 7; };
        Delegate<int()> b = std::move(a);

        EXPECT_FALSE(static_cast<bool>(a));
        ASSERT_TRUE(static_cast<bool>(b));
        EXPECT_EQ(b(), 7);
    }

    TEST(Unit_Delegate, MoveAssignReplacesTarget)
    {
        Delegate<int()> a = [] { return 1; };
        Delegate<int()> b = [] { return 2; };
        a = std::move(b);
        EXPECT_EQ(a(), 2);
        EXPECT_FALSE(static_cast<bool>(b));
    }

    TEST(Unit_Delegate, DestroysTarget)
    {
        auto counter = std::make_shared<int>(0);
        std::weak_ptr<int> observer = counter;
        {
            // Destroying the Delegate destroys the lambda and its shared_ptr copy; see README.md.
            Delegate<void()> d = [counter] { (void)counter; };
            counter.reset();
            EXPECT_FALSE(observer.expired());
        }
        EXPECT_TRUE(observer.expired());
    }

    TEST(Unit_Delegate, HoldsMoveOnlyTarget)
    {
        auto owned = std::make_unique<int>(123);
        Delegate<int()> d =
            [p = std::move(owned)] { return *p; };
        EXPECT_EQ(d(), 123);

        Delegate<int()> moved = std::move(d);
        EXPECT_EQ(moved(), 123);
        EXPECT_FALSE(static_cast<bool>(d));
    }

    TEST(Unit_Delegate, HoldsNothrowMovableMoveOnlyTarget)
    {
        auto owned = std::make_unique<int>(321);
        auto target = [p = std::move(owned)] { return *p; };
        static_assert(std::is_nothrow_move_constructible_v<decltype(target)>,
                      "The target of this test must have a noexcept move constructor.");
        static_assert(!std::is_copy_constructible_v<decltype(target)>,
                      "The target of this test must be move-only.");

        Delegate<int()> d = std::move(target);
        Delegate<int()> moved = std::move(d);
        EXPECT_FALSE(static_cast<bool>(d));
        ASSERT_TRUE(static_cast<bool>(moved));
        EXPECT_EQ(moved(), 321);
    }
}
