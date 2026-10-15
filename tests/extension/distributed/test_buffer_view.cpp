/**************************************************************************/
/* test_buffer_view.cpp                                                   */
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
 * @file test_buffer_view.cpp
 * @brief Tests BufferView: the bytes it names, read as an element type.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/distributed/buffer_view.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that as() returns the address typed, and a default view names nothing. */
TEST(Medium_BufferView, AsTypesTheAddress)
{
    float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    const BufferView view{values, sizeof(values)};
    EXPECT_EQ(view.as<float>(), values);
    EXPECT_EQ(view.as<const float>()[2], 3.0f);
    EXPECT_EQ(view.size_bytes, 4 * sizeof(float));
    EXPECT_EQ(BufferView().data, nullptr);
}
