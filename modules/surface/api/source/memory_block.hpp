/**************************************************************************/
/* memory_block.hpp                                                       */
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
 * @file memory_block.hpp
 * @brief Defines block_of, which turns an engine allocation into the block a Buffer holds.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
 */

#pragma once

#include <memory>
#include <utility>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/memory/allocation.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Moves an engine allocation behind the owner of a MemoryBlock.
         * @param allocation Released when the last copy of the owner goes.
         * @return A block with the address and the byte count of the allocation.
         */
        [[nodiscard]] inline MemoryBlock block_of(Internal::Memory::Allocation allocation)
        {
            MemoryBlock block;
            block.address = allocation.address();
            block.bytes = allocation.bytes();
            block.owner =
                std::make_shared<Internal::Memory::Allocation>(std::move(allocation));
            return block;
        }
    } // namespace Detail
} // namespace SushiRuntime
