/**************************************************************************/
/* refusing_offload_sink.hpp                                              */
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
 * @file refusing_offload_sink.hpp
 * @brief Declares RefusingOffloadSink, an OffloadSink that refuses every node it is handed back.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#pragma once

#include <atomic>
#include <stdexcept>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/task/offload_sink.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /**
         * @brief Refuses every node handed back for another dispatch, by throwing.
         *
         * resubmit() throws and counts the refusal; complete_remote() and fail() take their node
         * and do nothing, since the paths that refuse a node never reach them. This adapter is
         * the test double of the port, not a runtime sink.
         * @see modules/engine/task/README.md
         */
        class RefusingOffloadSink final : public OffloadSink
        {
            public:
                /** @brief Builds a sink that refused nothing yet. */
                RefusingOffloadSink() = default;

                /** @brief Destroys the sink through its own type; the base destructor is protected. */
                ~RefusingOffloadSink() = default;

                /**
                 * @brief Counts the refusal and throws.
                 * @throws std::runtime_error Always.
                 */
                void resubmit(Node& /*node*/) override
                {
                    refusals_.fetch_add(1, std::memory_order_relaxed);
                    throw std::runtime_error("the sink refuses every node");
                }

                /** @brief Takes the node and does nothing. */
                void complete_remote(Node& /*node*/) override
                {

                }

                /** @brief Takes the node and the error and does nothing. */
                void fail(Node& /*node*/, Error /*error*/) override
                {

                }

                /** @brief Returns how often resubmit() refused a node. */
                [[nodiscard]] int refusals() const noexcept
                {
                    return refusals_.load(std::memory_order_relaxed);
                }

            private:
                /// @brief Counts the calls to resubmit().
                std::atomic<int> refusals_{0};
        };

    } // namespace Internal::Graph
} // namespace SushiRuntime
