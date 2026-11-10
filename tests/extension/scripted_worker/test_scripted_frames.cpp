/**************************************************************************/
/* test_scripted_frames.cpp                                              */
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
 * @file test_scripted_frames.cpp
 * @brief Tests ScriptedFrames and the result frames: order, the last frame and a whole result.
 * @see tests/extension/scripted_worker/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/scripted_worker/scripted_frames.hpp>
#include <SushiRuntime/transport/message_type.hpp>

using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Returns a frame of a type, with no payload. */
    OutboundFrame frame_of(MessageType type)
    {
        OutboundFrame frame;
        frame.type = type;
        return frame;
    }

    TEST(Small_ScriptedFrames, YieldsItsFramesInOrderAndMarksTheLast)
    {
        ScriptedFrames source(std::vector<OutboundFrame>{frame_of(MessageType::Ping),
                                                         frame_of(MessageType::Pong)});
        OutboundFrame frame;

        EXPECT_EQ(source.next(frame), FrameSourceStatus::Frame);
        EXPECT_EQ(frame.type, MessageType::Ping);
        EXPECT_EQ(source.next(frame), FrameSourceStatus::FinalFrame);
        EXPECT_EQ(frame.type, MessageType::Pong);
        EXPECT_EQ(source.next(frame), FrameSourceStatus::Finished);
    }

    TEST(Small_ScriptedFrames, AWholeResultHoldsOneChunkPerOutputThenTheSuccess)
    {
        Wire::TaskDispatchMessage dispatch;
        dispatch.task_id = 7;
        dispatch.outputs = {BufferHandle{}, BufferHandle{}};

        const std::vector<OutboundFrame> frames = whole_result(dispatch);

        ASSERT_EQ(frames.size(), 3u);
        EXPECT_EQ(frames[0].type, MessageType::BufferChunk);
        EXPECT_EQ(frames[1].type, MessageType::BufferChunk);
        EXPECT_EQ(frames[2].type, MessageType::TaskResult);
    }
} // namespace
