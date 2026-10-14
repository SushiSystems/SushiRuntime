/**************************************************************************/
/* test_queue_flavour.cpp                                                 */
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
 * @file test_queue_flavour.cpp
 * @brief Tests QueueFlavour: the plain default and equality over both fields.
 * @see tests/platform/device/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/device/queue_flavour.hpp>

using SushiRuntime::Internal::Device::QueueFlavour;

/** @brief Tests that the default is the plain queue: out of order, no profiling. */
TEST(Unit_QueueFlavour, TheDefaultIsThePlainQueue)
{
    constexpr QueueFlavour plain;
    static_assert(!plain.in_order && !plain.profiling, "the default asks for nothing");
    EXPECT_EQ(plain, QueueFlavour{});
}

/** @brief Tests that two flavours are equal only when both fields are. */
TEST(Unit_QueueFlavour, EqualityComparesBothFields)
{
    EXPECT_EQ((QueueFlavour{true, true}), (QueueFlavour{true, true}));
    EXPECT_NE((QueueFlavour{true, false}), (QueueFlavour{false, false}));
    EXPECT_NE((QueueFlavour{false, true}), (QueueFlavour{false, false}));
    EXPECT_NE((QueueFlavour{true, false}), (QueueFlavour{false, true}));
}
