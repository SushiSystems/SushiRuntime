/**************************************************************************/
/* in_memory_offload_sink.hpp                                             */
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
 * @file in_memory_offload_sink.hpp
 * @brief Declares InMemoryOffloadSink, an OffloadSink that records the nodes it takes back.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#pragma once

#include <utility>
#include <vector>

#include <SushiRuntime/core/error.hpp>

#include <SushiRuntime/task/offload_sink.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        struct Node;

        /**
         * @brief Records the nodes an offload coordinator returns through the sink, with each error.
         *
         * The lists grow on every call, so this adapter is the test double of the port, not a
         * runtime implementation.
         * @see modules/engine/task/README.md
         */
        class InMemoryOffloadSink final : public OffloadSink
        {
            public:
                /** @brief Builds a sink with nothing recorded. */
                InMemoryOffloadSink() = default;

                /** @brief Destroys the sink through its own type; the base destructor is protected. */
                ~InMemoryOffloadSink() = default;

                /**
                 * @brief Records the node the coordinator gave back for another dispatch.
                 * @param node The node the call was given.
                 */
                void resubmit(Node& node) override
                {
                    resubmitted_.push_back(&node);
                }

                /**
                 * @brief Records the node a worker finished, as the call was given it.
                 * @param node The node the call was given.
                 */
                void complete_remote(Node& node) override
                {
                    completed_remote_.push_back(&node);
                }

                /**
                 * @brief Records the node that ended its run, with the error it ended with.
                 * @param node The node the call was given.
                 * @param error The error the call was given.
                 */
                void fail(Node& node, Error error) override
                {
                    failures_.emplace_back(&node, std::move(error));
                }

                /** @brief Returns every node given to resubmit(), in call order. */
                [[nodiscard]] const std::vector<Node*>& resubmitted() const noexcept
                {
                    return resubmitted_;
                }

                /** @brief Returns every node given to complete_remote(), in call order. */
                [[nodiscard]] const std::vector<Node*>& completed_remote() const noexcept
                {
                    return completed_remote_;
                }

                /** @brief Returns every node and error given to fail(), in call order. */
                [[nodiscard]] const std::vector<std::pair<Node*, Error>>& failures() const noexcept
                {
                    return failures_;
                }

            private:
                /** @brief Every node given to resubmit(), in call order. */
                std::vector<Node*> resubmitted_;

                /** @brief Every node given to complete_remote(), in call order. */
                std::vector<Node*> completed_remote_;

                /** @brief Every node and error given to fail(), in call order. */
                std::vector<std::pair<Node*, Error>> failures_;
        };

    } // namespace Internal::Graph
} // namespace SushiRuntime
