/**************************************************************************/
/* test_emit_host.cpp                                                     */
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
 * @file test_emit_host.cpp
 * @brief Tests host_node(): a host description that runs its function on the calling thread.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_host.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests the fields of the description and that its work runs once. */
TEST(Unit_EmitHost, AHostNodeRunsItsFunction)
{
    auto runtime = Runtime::create().value();
    int calls = 0;
    int target = 0;
    const Detail::NodeDescription description = Detail::host_node(
        {}, {BufferRegion(&target)}, [&calls] { ++calls; }, 5);
    EXPECT_EQ(description.kind, Detail::NodeKind::Host);
    EXPECT_EQ(description.width, 5u);
    ASSERT_EQ(description.writes.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Detail::WorkerWork>(description.work));
    Testing::run_description(runtime, description);
    EXPECT_EQ(calls, 1);
}
