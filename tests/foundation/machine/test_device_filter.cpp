/**************************************************************************/
/* test_device_filter.cpp                                                 */
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
 * @file test_device_filter.cpp
 * @brief Tests DeviceFilter: every class accepted, double precision not required.
 * @see tests/foundation/machine/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/machine/device_filter.hpp>

using namespace SushiRuntime;

/** @brief Tests the defaults of the filter. */
TEST(Unit_DeviceFilter, DefaultsAcceptEveryClass)
{
    const DeviceFilter filter;
    EXPECT_TRUE(filter.include_cpu);
    EXPECT_TRUE(filter.include_gpu);
    EXPECT_TRUE(filter.include_accelerator);
    EXPECT_FALSE(filter.require_fp64);
}
