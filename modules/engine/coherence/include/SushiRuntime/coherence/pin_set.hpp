/**************************************************************************/
/* pin_set.hpp                                                            */
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
 * @file pin_set.hpp
 * @brief Declares PinSet, the instances eviction never touches.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Holds the buffers pinned to a space.
         *
         * A job admitted as fitting pins its weights to the spaces the partitioner chose. The
         * planner never evicts a pinned instance. The set is built before planning and is not
         * changed while a plan runs.
         */
        class PinSet final
        {
            public:
                /** @brief Pins handle to space. Pinning twice changes nothing. */
                void pin(GlobalHandle handle, MemorySpaceID space)
                {
                    pins_.insert({handle, space.value()});
                }

                /** @brief Removes the pin of handle on space; an unpinned pair changes nothing. */
                void unpin(GlobalHandle handle, MemorySpaceID space)
                {
                    pins_.erase({handle, space.value()});
                }

                /** @brief Returns whether handle is pinned to space. */
                [[nodiscard]] bool is_pinned(GlobalHandle handle, MemorySpaceID space) const
                {
                    return pins_.count({handle, space.value()}) != 0;
                }

                /** @brief Returns how many pairs are pinned. */
                [[nodiscard]] std::size_t size() const noexcept
                {
                    return pins_.size();
                }

            private:
                /// @brief Pinned pairs, in handle order then space order.
                std::set<std::pair<GlobalHandle, std::uint32_t>> pins_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
