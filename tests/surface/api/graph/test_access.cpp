/**************************************************************************/
/* test_access.cpp                                                        */
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
 * @file test_access.cpp
 * @brief Tests Access: the regions it keeps in order and the observers of both sets.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/graph/access.hpp>

using namespace SushiRuntime;

/** @brief Tests that regions given directly keep their order and carry no observer. */
TEST(Unit_Access, RegionsKeepTheirOrder)
{
    int first = 0;
    int second = 0;
    const Access access({BufferRegion(&first), BufferRegion(&second)}, {BufferRegion(&second)});
    ASSERT_EQ(access.reads.size(), 2u);
    EXPECT_EQ(access.reads[0].base, &first);
    EXPECT_EQ(access.reads[1].base, &second);
    ASSERT_EQ(access.writes.size(), 1u);
    EXPECT_TRUE(access.lives.empty());
}

/** @brief Tests that handles given through Reads and Writes bring the observers of both. */
TEST(Unit_Access, HandlesBringTheirObservers)
{
    auto runtime = Runtime::create().value();
    auto in = runtime.buffer<int>(4).value();
    auto out = runtime.buffer<int>(4).value();
    const Access access{Reads(in), Writes(out)};
    EXPECT_EQ(access.reads.size(), 1u);
    EXPECT_EQ(access.writes.size(), 1u);
    EXPECT_EQ(access.lives.size(), 2u);
}
