/**************************************************************************/
/* test_plan.cpp                                                          */
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
 * @file test_plan.cpp
 * @brief Tests Detail::Plan: an opaque name the surface holds only by pointer.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <type_traits>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/plan.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that the name is a class type held by pointer. */
TEST(Small_Plan, IsHeldByPointerOnly)
{
    const Detail::Plan* plan = nullptr;
    static_assert(std::is_class_v<Detail::Plan>, "the name is a class type");
    EXPECT_EQ(plan, nullptr);
}
