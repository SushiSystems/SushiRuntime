/**************************************************************************/
/* test_device_pass.cpp                                                   */
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
 * @file test_device_pass.cpp
 * @brief Tests that compiled_for_device_v answers whether the unit compiles for the device.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/api/detail/device_pass.hpp>

using namespace SushiRuntime;

/** @brief Tests that the answer is false only for the DPC++ headers compiled without -fsycl. */
TEST(Small_DevicePass, AnswersFalseOnlyForDpcppWithoutFsycl)
{
#if defined(__SYCL_COMPILER_VERSION) && !defined(SYCL_LANGUAGE_VERSION)
    EXPECT_FALSE(Detail::compiled_for_device_v<int>);
#else
    EXPECT_TRUE(Detail::compiled_for_device_v<int>);
#endif
}
