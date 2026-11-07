/**************************************************************************/
/* single_frame.hpp                                                       */
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
 * @file single_frame.hpp
 * @brief Declares SingleFrame, a stream of one frame whose flag byte a control frame cannot carry.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <utility>

#include <SushiRuntime/transport/transport.hpp>

namespace TestSupport
{
    /**
     * @brief Yields one frame, with any flag byte, as a stream.
     */
    class SingleFrame final : public SushiRuntime::Distributed::FrameSource
    {
        public:
            /** @brief Takes the frame the stream yields. */
            explicit SingleFrame(SushiRuntime::Distributed::OutboundFrame frame) noexcept
                : frame_(std::move(frame))
            {

            }

            /** @brief Hands over the frame on the first call and nothing after. */
            [[nodiscard]] SushiRuntime::Distributed::FrameSourceStatus next(
                SushiRuntime::Distributed::OutboundFrame& frame) noexcept override
            {
                if (is_sent_)
                    return SushiRuntime::Distributed::FrameSourceStatus::Finished;

                is_sent_ = true;
                frame.type = frame_.type;
                frame.flags = frame_.flags;
                frame.payload.swap(frame_.payload);
                return SushiRuntime::Distributed::FrameSourceStatus::FinalFrame;
            }

        private:
            /// @brief The frame to hand over.
            SushiRuntime::Distributed::OutboundFrame frame_;

            /// @brief Whether next() handed it over.
            bool is_sent_ = false;
    };
} // namespace TestSupport
