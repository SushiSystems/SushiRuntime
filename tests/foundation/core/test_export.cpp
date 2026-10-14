/**************************************************************************/
/* test_export.cpp                                                        */
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
 * @file test_export.cpp
 * @brief Tests the export macros: each one is defined, and SR_API spells SUSHIRUNTIME_API.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstring>

#include <gtest/gtest.h>

#include <SushiRuntime/core/export.hpp>

#define SR_STRINGIFY_EXPANDED(text) SR_STRINGIFY(text)
#define SR_STRINGIFY(text) #text

#if !defined(SUSHIRUNTIME_API) || !defined(SR_API) || !defined(SR_TYPE_API)
#error "export.hpp must define SUSHIRUNTIME_API, SR_API and SR_TYPE_API"
#endif

/** @brief Tests that the short spelling expands to the same attribute as the long one. */
TEST(Unit_Export, ShortSpellingExpandsLikeTheLongOne)
{
    EXPECT_STREQ(SR_STRINGIFY_EXPANDED(SR_API), SR_STRINGIFY_EXPANDED(SUSHIRUNTIME_API));
}

/** @brief Tests that a consumer of the library imports, which a test program is. */
TEST(Unit_Export, AConsumerImportsTheSymbols)
{
#if defined(_WIN32) || defined(__CYGWIN__)
    EXPECT_NE(std::strstr(SR_STRINGIFY_EXPANDED(SUSHIRUNTIME_API), "dllimport"), nullptr);
#else
    EXPECT_NE(std::strstr(SR_STRINGIFY_EXPANDED(SUSHIRUNTIME_API), "visibility"), nullptr);
#endif
}
