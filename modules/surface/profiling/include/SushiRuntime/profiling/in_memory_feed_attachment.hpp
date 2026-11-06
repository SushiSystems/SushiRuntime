/**************************************************************************/
/* in_memory_feed_attachment.hpp                                          */
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
 * @file in_memory_feed_attachment.hpp
 * @brief Declares InMemoryFeedAttachment, a FeedAttachment that only records its calls.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/profiling/device_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Profiling
    {
        /**
         * @brief Records each attach() and detach() in order; attach() returns the refusal.
         *
         * It starts and stops nothing. The refusal is empty until refuse_with() sets one;
         * an empty refusal accepts.
         * @see modules/surface/profiling/README.md
         */
        class InMemoryFeedAttachment final : public FeedAttachment
        {
            public:
                /** @brief One call the attachment received. */
                enum class Call : std::uint8_t
                {
                    Attach, /**< attach() was called. */
                    Detach, /**< detach() was called. */
                };

                /**
                 * @brief Records an attach call and returns the refusal set.
                 * @return The refusal text; empty when the attachment accepts.
                 */
                [[nodiscard]] std::string attach() override
                {
                    calls_.push_back(Call::Attach);
                    return refusal_;
                }

                /** @brief Records a detach call. */
                void detach() noexcept override
                {
                    calls_.push_back(Call::Detach);
                }

                /**
                 * @brief Sets the text attach() returns from now on.
                 * @param reason Why the feed is refused; empty to accept.
                 */
                void refuse_with(std::string reason)
                {
                    refusal_ = std::move(reason);
                }

                /**
                 * @brief Returns the calls received so far, oldest first.
                 * @return The calls, in the order they were made.
                 */
                [[nodiscard]] const std::vector<Call>& calls() const noexcept
                {
                    return calls_;
                }

            private:
                /** @brief The text attach() returns. */
                std::string refusal_;

                /** @brief Every call received, in order. */
                std::vector<Call> calls_;
        };
    } // namespace Internal::Profiling
} // namespace SushiRuntime
