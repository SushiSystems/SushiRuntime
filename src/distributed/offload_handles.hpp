/**************************************************************************/
/* offload_handles.hpp                                                    */
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
 * @file offload_handles.hpp
 * @brief Declares OffloadHandles, the buffer handles the coordinator keeps on a node.
 * @author Mustafa Garip
 * @see src/distributed/README.md
 */

#pragma once

#include <vector>
#include <SushiRuntime/distributed/protocol.hpp>
#include <SushiRuntime/task/offload_state.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Holds the handles of the buffers a fixed-buffer node reads and writes.
         *
         * DistributedCoordinator::prepare() stores one in Graph::OffloadState::payload;
         * a double-buffered node has none.
         */
        struct OffloadHandles final : public Graph::OffloadPayload
        {
            /** @brief Handles of the buffers the node reads, in io_reads_ order. */
            std::vector<BufferHandle> in_handles;

            /** @brief Handles of the buffers the node writes, in io_writes_ order. */
            std::vector<BufferHandle> out_handles;
        };

    } // namespace Distributed
} // namespace SushiRuntime
