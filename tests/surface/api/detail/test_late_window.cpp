/**************************************************************************/
/* test_late_window.cpp                                                   */
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
 * @file test_late_window.cpp
 * @brief Tests resolve_live_range() and read_late_window(): windows inside and past the capacity.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/api/detail/late_window.hpp>
#include <SushiRuntime/engine_handles/late_controls_access.hpp>
#include <SushiRuntime/task/late_bindings.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that a window inside the capacity resolves to itself. */
TEST(Unit_LateWindow, AWindowInsideTheCapacityResolves)
{
    Detail::LateWindow window;
    window.base = 2;
    window.count = 5;
    const std::optional<Detail::LiveRange> range = Detail::resolve_live_range(window, 8, nullptr);
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->base, 2u);
    EXPECT_EQ(range->count, 5u);
}

/** @brief Tests that no host count covers the rest of the capacity from the base. */
TEST(Unit_LateWindow, NoHostCountTakesTheRestOfTheCapacity)
{
    Detail::LateWindow window;
    window.base = 3;
    window.rest_of_capacity = true;
    const std::optional<Detail::LiveRange> range = Detail::resolve_live_range(window, 8, nullptr);
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->count, 5u);
}

/** @brief Tests that a window past the capacity is refused and recorded. */
TEST(Unit_LateWindow, AWindowPastTheCapacityIsAFault)
{
    std::uint32_t fault[Detail::K_DEVICE_FAULT_WORDS] = {};
    Detail::LateWindow window;
    window.base = 6;
    window.count = 3;
    EXPECT_FALSE(Detail::resolve_live_range(window, 8, fault).has_value());
    EXPECT_EQ(fault[0], Detail::K_DEVICE_FAULT_WINDOW);
    EXPECT_EQ(fault[1], 6u);
    EXPECT_EQ(fault[2], 3u);
    EXPECT_EQ(fault[3], 8u);
}

/** @brief Tests that the window read from bindings carries the count and the base set on them. */
TEST(Unit_LateWindow, TheWindowIsReadFromTheBindings)
{
    auto bindings = std::make_shared<Internal::Graph::LateBindings>();
    const auto controls = Detail::surface_controls(bindings);
    EXPECT_TRUE(Detail::read_late_window(controls.get()).rest_of_capacity);
    bindings->count.store(4);
    bindings->base.store(1);
    const Detail::LateWindow window = Detail::read_late_window(controls.get());
    EXPECT_FALSE(window.rest_of_capacity);
    EXPECT_EQ(window.count, 4u);
    EXPECT_EQ(window.base, 1u);
}
