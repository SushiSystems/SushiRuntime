/**************************************************************************/
/* test_offload_handles.cpp                                               */
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
 * @file test_offload_handles.cpp
 * @brief Tests OffloadHandles: the inputs come first, the outputs after them.
 * @see tests/extension/offload_coordination/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/transport/buffer_handle.hpp>

#include <SushiRuntime/offload_coordination/offload_handles.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that the two views split the handles at the input count. */
TEST(Medium_OffloadHandles, InputsThenOutputs)
{
    OffloadHandles handles;
    handles.handles = {BufferHandle{GlobalBufferID{1}, 1, 8}, BufferHandle{GlobalBufferID{2}, 1, 8},
                       BufferHandle{GlobalBufferID{3}, 1, 8}};
    handles.input_count = 1;
    ASSERT_EQ(handles.inputs().size(), 1u);
    EXPECT_EQ(handles.inputs()[0].id, GlobalBufferID{1});
    ASSERT_EQ(handles.outputs().size(), 2u);
    EXPECT_EQ(handles.outputs()[1].id, GlobalBufferID{3});
    handles.outputs()[0].version = 4;
    EXPECT_EQ(handles.handles[1].version, 4u) << "the outputs view writes the handle itself";
}
