/**************************************************************************/
/* test_dispatch_events.cpp                                               */
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
 * @file test_dispatch_events.cpp
 * @brief Tests DispatchEventScope, which collects the events a task body hands over.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/task/dispatch_events.hpp>

using SushiRuntime::Internal::DispatchEvents::DispatchEventScope;

/** @brief Tests that an event handed over inside a scope is collected, and one outside it is not. */
TEST(Small_DispatchEventScope, CollectsOnlyWhileTheScopeIsOpen)
{
    std::vector<sycl::event> sink;

    DispatchEventScope::note(sycl::event{});
    EXPECT_TRUE(sink.empty()) << "no scope is open yet";
    {
        DispatchEventScope scope(sink);
        DispatchEventScope::note(sycl::event{});
        DispatchEventScope::note(sycl::event{});
    }
    DispatchEventScope::note(sycl::event{});

    EXPECT_EQ(sink.size(), 2u);
}

/** @brief Tests that an inner scope collects its own events and the outer one is restored after it. */
TEST(Small_DispatchEventScope, NestedScopesRestoreTheOuterOne)
{
    std::vector<sycl::event> outer;
    std::vector<sycl::event> inner;
    {
        DispatchEventScope outer_scope(outer);
        DispatchEventScope::note(sycl::event{});
        {
            DispatchEventScope inner_scope(inner);
            DispatchEventScope::note(sycl::event{});
            DispatchEventScope::note(sycl::event{});
        }
        DispatchEventScope::note(sycl::event{});
    }

    EXPECT_EQ(outer.size(), 2u);
    EXPECT_EQ(inner.size(), 2u);
}

/** @brief Tests that a scope belongs to its thread: another thread's events do not reach it. */
TEST(Small_DispatchEventScope, AScopeBelongsToItsThread)
{
    std::vector<sycl::event> sink;
    DispatchEventScope scope(sink);

    std::thread other([]() { DispatchEventScope::note(sycl::event{}); });
    other.join();
    DispatchEventScope::note(sycl::event{});

    EXPECT_EQ(sink.size(), 1u) << "only the event handed over on the scope's own thread is collected";
}

/** @brief Tests that handing over with no scope open does nothing and does not throw. */
TEST(Small_DispatchEventScope, HandingOverWithNoScopeDoesNothing)
{
    EXPECT_NO_THROW(DispatchEventScope::note(sycl::event{}));
}
