/**************************************************************************/
/* test_step_run.cpp                                                      */
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
 * @file test_step_run.cpp
 * @brief Tests StepPredicate and the context queries of step_run.hpp.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/step_run.hpp>
#include <SushiRuntime/context/context_handle_access.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that the predicate forwards the completed count to the wrapped callable. */
TEST(Medium_StepRun, ThePredicateForwardsTheCount)
{
    std::size_t limit = 3;
    auto keep_going = [&limit](std::size_t completed) { return completed < limit; };
    const Detail::StepPredicate predicate(keep_going);
    EXPECT_TRUE(predicate(2));
    EXPECT_FALSE(predicate(3));
}

/** @brief Tests that an idle runtime has no run in progress, and cancelling it is harmless. */
TEST(Medium_StepRun, AnIdleRuntimeHasNoRunInProgress)
{
    auto runtime = Runtime::create().value();
    const ContextHandle& context = Detail::ContextHandleAccess::handle_of(runtime);
    EXPECT_FALSE(Detail::run_in_progress(context));
    Detail::cancel_run(context);
    EXPECT_FALSE(Detail::run_in_progress(context));
}
