/**************************************************************************/
/* node_widths.cpp                                                        */
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
 * @file node_widths.cpp
 * @brief Reads the launched and live widths of a node at submit.
 * @author Mustafa Garip
 */

#include <SushiRuntime/task/node_widths.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <SushiRuntime/report/dispatch_width.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        namespace
        {
            /** @brief Narrows @p value to uint32_t, or K_WIDTH_UNKNOWN when it does not fit. */
            std::uint32_t narrow_width(std::size_t value) noexcept
            {
                constexpr std::size_t max_u32 =
                    static_cast<std::size_t>(K_WIDTH_UNKNOWN);
                return value <= max_u32 ? static_cast<std::uint32_t>(value)
                                        : K_WIDTH_UNKNOWN;
            }
        } // namespace

        /**
         * @brief Reads @p node's widths as they stand at submit.
         *
         * The baked width stands for both until the bindings hold a live count
         * other than K_USE_BAKED, which then narrows the live side only. Either
         * width that overflows uint32_t is reported unknown rather than truncated.
         */
        NodeWidths snapshot_widths(const Node& node) noexcept
        {
            NodeWidths widths;
            widths.launched = narrow_width(node.baked_width);
            widths.live = widths.launched;

            if (node.bindings_)
            {
                const std::size_t count = node.bindings_->count.load(std::memory_order_relaxed);
                if (count != LateBindings::K_USE_BAKED)
                    widths.live = narrow_width(count);
            }
            return widths;
        }

    } // namespace Graph
} // namespace SushiRuntime
