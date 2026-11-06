/**************************************************************************/
/* test_scripted_capability_probe.cpp                                     */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_scripted_capability_probe.cpp
 * @brief Tests ScriptedCapabilityProbe: queued steps are served in order and counted.
 * @see tests/surface/memory_exchange/README.md
 * @author Mustafa Garip
 */

#include <stdexcept>

#include <gtest/gtest.h>

#include <SushiRuntime/interop/interop_capabilities.hpp>
#include <SushiRuntime/memory_exchange/scripted_capability_probe.hpp>

using SushiRuntime::InteropCapabilities;
using SushiRuntime::Internal::MemoryExchange::ScriptedCapabilityProbe;

/** @brief Tests that queued answers come back in queue order, and are counted. */
TEST(Small_ScriptedCapabilityProbe, ReturnsTheQueuedAnswersInOrder)
{
    ScriptedCapabilityProbe probe;
    InteropCapabilities first;
    first.host_export = true;
    InteropCapabilities second;
    second.host_export = false;
    probe.answer_next(first);
    probe.answer_next(second);

    EXPECT_TRUE(probe.next_answer().host_export);
    EXPECT_FALSE(probe.next_answer().host_export);
    EXPECT_EQ(probe.calls(), 2u);
}

/** @brief Tests that a queued throw is raised by its own call, not the next one. */
TEST(Small_ScriptedCapabilityProbe, RaisesTheQueuedFailureThenAnswersTheNext)
{
    ScriptedCapabilityProbe probe;
    InteropCapabilities answer;
    answer.host_export = true;
    probe.throw_next("probe failed");
    probe.answer_next(answer);

    EXPECT_THROW(static_cast<void>(probe.next_answer()), std::runtime_error);
    EXPECT_TRUE(probe.next_answer().host_export);
    EXPECT_EQ(probe.calls(), 2u);
}

/** @brief Tests that a call with nothing queued fails, and is still counted. */
TEST(Small_ScriptedCapabilityProbe, RefusesACallWithNothingQueued)
{
    ScriptedCapabilityProbe probe;

    EXPECT_THROW(static_cast<void>(probe.next_answer()), std::logic_error);
    EXPECT_EQ(probe.calls(), 1u);
}
