/**************************************************************************/
/* in_memory_offload_coordinator.hpp                                      */
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
 * @file in_memory_offload_coordinator.hpp
 * @brief Declares InMemoryOffloadCoordinator, an OffloadCoordinator that records its calls.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>

#include <SushiRuntime/task/offload_coordinator.hpp>

namespace SushiRuntime
{
    class PlacementRules;

    namespace Internal::Graph
    {
        struct Node;
        class OffloadSink;

        /**
         * @brief Records every call an offload coordinator receives and accepts nodes as set.
         *
         * try_offload() answers with set_accepting(), which refuses until it is set. The lists
         * grow on every call, so this adapter is the test double of the port, not a runtime
         * implementation.
         * @see modules/engine/task/README.md
         */
        class InMemoryOffloadCoordinator final : public OffloadCoordinator
        {
            public:
                /** @brief Builds a coordinator that refuses every node until told otherwise. */
                InMemoryOffloadCoordinator() = default;

                /**
                 * @brief Keeps the address of the sink the scheduler binds.
                 * @param sink The sink the call was given.
                 */
                void bind(OffloadSink& sink) override
                {
                    bound_sink_ = &sink;
                }

                /**
                 * @brief Counts the call and keeps the rules it was given; the nodes are not recorded.
                 * @param nodes Not read.
                 * @param rules The rules the call was given; null for none.
                 */
                void prepare(span<const sushi_ptr<Node>>,
                             const std::shared_ptr<const PlacementRules>& rules) override
                {
                    ++prepare_count_;
                    last_rules_ = rules;
                }

                /**
                 * @brief Records the node and answers with the acceptance that was set.
                 * @param node The node the call was given; kept in accepted() when accepted.
                 * @return The acceptance set by set_accepting(), false by default.
                 */
                [[nodiscard]] bool try_offload(Node& node) override
                {
                    offered_.push_back(&node);
                    if (!accepting_)
                        return false;

                    accepted_.push_back(&node);
                    return true;
                }

                /**
                 * @brief Records the node the pool finished itself.
                 * @param node The node the call was given.
                 */
                void on_local_completion(Node& node) noexcept override
                {
                    local_completions_.push_back(&node);
                }

                /** @brief Records that the scheduler stopped the coordinator. */
                void stop() noexcept override
                {
                    stopped_ = true;
                }

                /**
                 * @brief Sets the answer try_offload() gives from now on.
                 * @param accepting True to accept every node offered after this call.
                 */
                void set_accepting(bool accepting) noexcept
                {
                    accepting_ = accepting;
                }

                /** @brief Returns the sink bind() was given, null before the call. */
                [[nodiscard]] OffloadSink* bound_sink() const noexcept
                {
                    return bound_sink_;
                }

                /** @brief Returns how many times prepare() ran. */
                [[nodiscard]] std::size_t prepare_count() const noexcept
                {
                    return prepare_count_;
                }

                /** @brief Returns the rules prepare() was given last, null for none. */
                [[nodiscard]] const std::shared_ptr<const PlacementRules>& last_rules() const noexcept
                {
                    return last_rules_;
                }

                /** @brief Returns every node try_offload() was given, in call order. */
                [[nodiscard]] const std::vector<Node*>& offered() const noexcept
                {
                    return offered_;
                }

                /** @brief Returns every node try_offload() accepted, in call order. */
                [[nodiscard]] const std::vector<Node*>& accepted() const noexcept
                {
                    return accepted_;
                }

                /** @brief Returns every node on_local_completion() was given, in call order. */
                [[nodiscard]] const std::vector<Node*>& local_completions() const noexcept
                {
                    return local_completions_;
                }

                /** @brief Returns whether stop() ran. */
                [[nodiscard]] bool stopped() const noexcept
                {
                    return stopped_;
                }

            private:
                /** @brief The sink bind() was given; null until then. */
                OffloadSink* bound_sink_ = nullptr;

                /** @brief The number of prepare() calls. */
                std::size_t prepare_count_ = 0;

                /** @brief The rules the last prepare() call was given. */
                std::shared_ptr<const PlacementRules> last_rules_;

                /** @brief Every node offered to try_offload(), in call order. */
                std::vector<Node*> offered_;

                /** @brief Every node try_offload() accepted, in call order. */
                std::vector<Node*> accepted_;

                /** @brief Every node reported to on_local_completion(), in call order. */
                std::vector<Node*> local_completions_;

                /** @brief The answer try_offload() gives. */
                bool accepting_ = false;

                /** @brief Whether stop() ran. */
                bool stopped_ = false;
        };

    } // namespace Internal::Graph
} // namespace SushiRuntime
