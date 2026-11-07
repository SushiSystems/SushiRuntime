/**************************************************************************/
/* in_memory_offload_policy.hpp                                           */
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
 * @file in_memory_offload_policy.hpp
 * @brief Declares InMemoryOffloadPolicy, an OffloadPolicy that picks the first worker and counts its calls.
 * @author Mustafa Garip
 * @see modules/extension/cluster_load/README.md
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>

#include <SushiRuntime/cluster_load/offload_policy.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Picks the first worker it is offered and counts how often it was asked.
         *
         * The choice depends on nothing but the order of the offer, so a test decides the pick
         * by the order in which it registers workers. This adapter is the test double of the
         * port, not a runtime policy.
         * @see modules/extension/cluster_load/README.md
         */
        class InMemoryOffloadPolicy final : public OffloadPolicy
        {
            public:
                /** @brief Builds a policy that was never asked. */
                InMemoryOffloadPolicy() = default;

                /**
                 * @brief Counts the call and picks the first offered worker.
                 * @return The id of the first entry of @p workers, or nothing when none was offered.
                 */
                std::optional<int> pick_worker(const NodeOptions& /*task*/,
                                               SushiRuntime::span<const WorkerLoad> workers,
                                               std::uint64_t /*est_bytes*/) override
                {
                    calls_.fetch_add(1, std::memory_order_relaxed);
                    if (workers.empty())
                        return std::nullopt;
                    return workers.begin()->worker_id;
                }

                /** @brief Returns how often pick_worker() was called. */
                [[nodiscard]] int calls() const noexcept
                {
                    return calls_.load(std::memory_order_relaxed);
                }

            private:
                /** @brief Counts the calls to pick_worker(). */
                std::atomic<int> calls_{0};
        };

    } // namespace Distributed
} // namespace SushiRuntime
