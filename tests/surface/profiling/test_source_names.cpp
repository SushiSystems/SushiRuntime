/**************************************************************************/
/* test_source_names.cpp                                                  */
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
 * @file test_source_names.cpp
 * @brief Tests K_DEVICE_SOURCE_NAME and that the registered source carries it.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <SushiProfiler/json/json_reader.hpp>
#include <SushiProfiler/registry/registry.hpp>
#include <SushiProfiler/sources/source.hpp>

#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/profiling/register_profiler_parts.hpp>
#include <SushiRuntime/profiling/source_names.hpp>

using SushiRuntime::K_DEVICE_SOURCE_NAME;
using SushiRuntime::Result;
using SushiRuntime::Runtime;

/** @brief Tests the spelling a configuration and a record carry. */
TEST(Small_SourceNames, TheDeviceSourceIsNamedSushiruntimeDevice)
{
    EXPECT_EQ(K_DEVICE_SOURCE_NAME, std::string_view("sushiruntime.device"));
}

/** @brief Tests that the registry makes the source under the constant and that the source reports it. */
TEST(Medium_SourceNames, TheSourceTheRegistryMakesCarriesThatName)
{
    Result<Runtime> created = Runtime::create();
    if (!created)
        GTEST_SKIP() << "No SYCL device: " << created.error().message;

    SushiProfiler::Registry registry;
    const SushiRuntime::RuntimeProfilerParts parts =
        SushiRuntime::register_profiler_parts(registry, *created);
    ASSERT_TRUE(parts.is_registered());

    SushiProfiler::Made<SushiProfiler::Source> made =
        registry.make_source(K_DEVICE_SOURCE_NAME, *SushiProfiler::read_json("{}").value);
    ASSERT_TRUE(made.error.empty()) << made.error;
    ASSERT_NE(made.part, nullptr);
    EXPECT_EQ(made.part->name(), K_DEVICE_SOURCE_NAME);
}
