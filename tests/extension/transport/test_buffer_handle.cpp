/**************************************************************************/
/* test_buffer_handle.cpp                                                 */
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
 * @file test_buffer_handle.cpp
 * @brief Tests GlobalBufferID and BufferHandle: the invalid id 0, equality and the defaults.
 * @see tests/extension/transport/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/transport/buffer_handle.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that id 0 is invalid and ids compare by value. */
TEST(Medium_BufferHandle, IDZeroIsInvalid)
{
    static_assert(!GlobalBufferID{}.valid(), "the default id names no buffer");
    static_assert(GlobalBufferID{3}.valid(), "a non-zero id names a buffer");
    EXPECT_EQ(GlobalBufferID{3}, GlobalBufferID{3});
    EXPECT_NE(GlobalBufferID{3}, GlobalBufferID{4});
}

/** @brief Tests the defaults of a handle. */
TEST(Medium_BufferHandle, DefaultsNameNoBuffer)
{
    const BufferHandle handle;
    EXPECT_FALSE(handle.id.valid());
    EXPECT_EQ(handle.version, 0u);
    EXPECT_EQ(handle.size_bytes, 0u);
}
