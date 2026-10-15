/**************************************************************************/
/* test_context.cpp                                                       */
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
 * @file test_context.cpp
 * @brief Tests Detail::Context: an opaque name the surface holds only by pointer.
 * @see tests/surface/context/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/context/context.hpp>

using namespace SushiRuntime;

/** @brief Tests that the name is held by pointer and by shared pointer without a definition. */
TEST(Small_Context, IsHeldByPointerOnly)
{
    const Detail::Context* pointer = nullptr;
    const std::shared_ptr<Detail::Context> shared;
    static_assert(std::is_class_v<Detail::Context>, "the name is a class type");
    EXPECT_EQ(pointer, nullptr);
    EXPECT_EQ(shared, nullptr);
}
