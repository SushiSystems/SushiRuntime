/**************************************************************************/
/* test_node_description.cpp                                              */
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
 * @file test_node_description.cpp
 * @brief Tests NodeDescription: a kernel of unknown width on device 0, with no regions or bindings.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/node_description.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests the defaults of a description. */
TEST(Small_NodeDescription, DefaultsDescribeAnEmptyKernel)
{
    const Detail::NodeDescription description;
    EXPECT_EQ(description.kind, Detail::NodeKind::Kernel);
    EXPECT_EQ(description.width, K_WIDTH_UNKNOWN);
    EXPECT_EQ(description.device, 0u);
    EXPECT_TRUE(description.reads.empty());
    EXPECT_TRUE(description.writes.empty());
    EXPECT_EQ(description.bindings, nullptr);
    EXPECT_FALSE(static_cast<bool>(description.state_access));
}
