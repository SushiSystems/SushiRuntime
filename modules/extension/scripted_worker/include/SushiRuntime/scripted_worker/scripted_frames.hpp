/**************************************************************************/
/* scripted_frames.hpp                                                   */
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
 * @file scripted_frames.hpp
 * @brief Declares ScriptedFrames and the frames of a worker's result, for a ScriptedWorker.
 * @see modules/extension/scripted_worker/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /** @brief Returns the frame of one output chunk that carries a whole buffer. */
        inline OutboundFrame output_chunk_frame(std::uint64_t task_id, const BufferHandle& handle,
                                         span<const std::byte> bytes)
        {
            Wire::BufferChunkMessage chunk;
            chunk.purpose = Wire::ChunkPurpose::TaskOutput;
            chunk.task_id = task_id;
            chunk.handle = handle;
            chunk.data = bytes;

            OutboundFrame frame;
            frame.type = MessageType::BufferChunk;
            frame.flags = Wire::K_FRAME_FLAG_LAST_CHUNK;
            SR_VERIFY(chunk.encode(frame.payload) == Wire::CodecStatus::Ok,
                      "output_chunk_frame: the chunk does not encode.");
            return frame;
        }

        /** @brief Returns the frame of a result that reports a task as succeeded. */
        inline OutboundFrame success_frame(const Wire::TaskDispatchMessage& dispatch)
        {
            Wire::TaskResultMessage result;
            result.task_id = dispatch.task_id;
            result.status = Wire::TaskResultStatus::Succeeded;
            result.outputs = dispatch.outputs;

            OutboundFrame frame;
            frame.type = MessageType::TaskResult;
            SR_VERIFY(result.encode(frame.payload) == Wire::CodecStatus::Ok,
                      "success_frame: the result does not encode.");
            return frame;
        }

        /**
         * @brief Returns the frames of a whole result: one zeroed chunk per output at its
         *        dispatched size, then success_frame().
         */
        inline std::vector<OutboundFrame> whole_result(const Wire::TaskDispatchMessage& dispatch)
        {
            std::vector<OutboundFrame> frames;
            for (const BufferHandle& output : dispatch.outputs)
            {
                const std::vector<std::byte> zeros(static_cast<std::size_t>(output.size_bytes));
                frames.push_back(output_chunk_frame(
                    dispatch.task_id, output, span<const std::byte>(zeros.data(), zeros.size())));
            }
            frames.push_back(success_frame(dispatch));
            return frames;
        }

        /**
         * @brief Yields a fixed list of frames as one stream, in order.
         */
        class ScriptedFrames final : public FrameSource
        {
            public:
                /** @brief Takes the frames the stream yields. */
                explicit ScriptedFrames(std::vector<OutboundFrame> frames) noexcept
                    : frames_(std::move(frames))
                {

                }

                /** @brief Hands over the next frame without copying its payload. */
                [[nodiscard]] FrameSourceStatus next(OutboundFrame& frame) noexcept override
                {
                    if (next_ == frames_.size())
                        return FrameSourceStatus::Finished;

                    frame.type = frames_[next_].type;
                    frame.flags = frames_[next_].flags;
                    frame.payload.swap(frames_[next_].payload);
                    ++next_;
                    return next_ == frames_.size() ? FrameSourceStatus::FinalFrame
                                                   : FrameSourceStatus::Frame;
                }

            private:
                /// @brief The frames, in the order they leave.
                std::vector<OutboundFrame> frames_;

                /// @brief The index of the frame the next call hands over.
                std::size_t next_ = 0;
        };

    } // namespace Distributed
} // namespace SushiRuntime
