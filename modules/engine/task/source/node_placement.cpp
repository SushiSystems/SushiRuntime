/**************************************************************************/
/* node_placement.cpp                                                     */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file node_placement.cpp
 * @brief Names the device a graph node runs on from its regions and its stated placement.
 * @author Mustafa Garip
 */

#include <SushiRuntime/task/node_placement.hpp>

#include <cstddef>
#include <optional>

#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        namespace
        {
            /** @brief Returns the base of the first write region, else of the first read region. */
            void* anchor_of(const Node& node) noexcept
            {
                if (!node.io_writes_.empty())
                    return node.io_writes_.front().base;
                if (!node.io_reads_.empty())
                    return node.io_reads_.front().base;
                return nullptr;
            }
        } // namespace

        /**
         * @brief Returns the device @p node runs on.
         */
        std::size_t placement_of(const Node& node, const Internal::Memory::USMAllocator& memory)
        {
            if (void* anchor = anchor_of(node))
                if (const std::optional<std::size_t> owner = memory.owning_device(anchor))
                    return *owner;

            return node.stated_placement().value_or(K_DEFAULT_PLACEMENT);
        }

    } // namespace Graph
} // namespace SushiRuntime
