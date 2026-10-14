/**************************************************************************/
/* test_late_controls_access.cpp                                          */
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
 * @file test_late_controls_access.cpp
 * @brief Tests engine_controls() and surface_controls(): one object under two names, sharing its owner.
 * @see tests/surface/engine_handles/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/task/late_bindings.hpp>

#include <SushiRuntime/engine_handles/late_controls_access.hpp>

using namespace SushiRuntime;

/** @brief Tests that the surface share and the engine share name one binding and one owner. */
TEST(Unit_LateControlsAccess, BothSharesNameOneBinding)
{
    const auto bindings = std::make_shared<Internal::Graph::LateBindings>();
    const std::shared_ptr<Detail::LateControls> controls = Detail::surface_controls(bindings);
    EXPECT_EQ(bindings.use_count(), 2) << "the surface share does not own the binding";

    const std::shared_ptr<Internal::Graph::LateBindings> back = Detail::engine_controls(controls);
    EXPECT_EQ(back.get(), bindings.get());
    EXPECT_EQ(Detail::engine_controls(controls.get()), bindings.get());
    back->count.store(5);
    EXPECT_EQ(bindings->count.load(), 5u);
}

/** @brief Tests that null converts to null. */
TEST(Unit_LateControlsAccess, NullStaysNull)
{
    EXPECT_EQ(Detail::surface_controls(nullptr), nullptr);
    EXPECT_EQ(Detail::engine_controls(std::shared_ptr<Detail::LateControls>()), nullptr);
}
