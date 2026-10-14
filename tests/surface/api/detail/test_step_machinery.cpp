/**************************************************************************/
/* test_step_machinery.cpp                                                */
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
 * @file test_step_machinery.cpp
 * @brief Tests StepMachinery: one view per state, and the fault records it reads.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/api/detail/step_machinery.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that the same state gives the same view twice. */
TEST(Unit_StepMachinery, OneViewPerState)
{
    auto runtime = Runtime::create().value();
    auto state = runtime.state<int>(4).value();
    Detail::StepMachinery machinery;
    EXPECT_EQ(machinery.view_for(state), machinery.view_for(state));
}

/** @brief Tests that a clean record reports nothing and a window fault reports an error. */
TEST(Unit_StepMachinery, AFaultRecordBecomesAnError)
{
    std::uint32_t clean[Detail::K_DEVICE_FAULT_WORDS] = {};
    std::uint32_t faulted[Detail::K_DEVICE_FAULT_WORDS] = {};
    Detail::StepMachinery machinery;
    machinery.register_fault_record(clean);
    EXPECT_FALSE(machinery.take_fault().has_value());
    Detail::record_device_fault(faulted, Detail::K_DEVICE_FAULT_WINDOW, 6, 3, 8);
    machinery.register_fault_record(faulted);
    EXPECT_TRUE(machinery.take_fault().has_value());
}
