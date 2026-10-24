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
 * @brief Declares OffloadHandles, the buffer handles the dispatcher keeps on a node.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /** @brief Names why a node whose rule names another machine cannot go there. */
        enum class UnreachableCause : std::uint8_t
        {
            Untagged,      /**< The node names no operation, so no worker can run it. */
            NotShared,     /**< A buffer the node touches is not shared. */
            WrittenTwice,  /**< Two of the node's writes name one shared buffer. */
            OutOfAttempts, /**< The node used every dispatch it may have in one run. */
        };

        /**
         * @brief Holds the handles of the shared buffers a fixed-buffer node touches.
         *
         * OffloadDispatcher::prepare() stores one in OffloadState::payload: every buffer read,
         * then every buffer written, on an offloadable node; only the shared writes, for a
         * local completion to bump, on a node that stays local. A double-buffered node has one
         * only to carry an UnreachableCause.
         */
        struct OffloadHandles final : public Internal::Graph::OffloadPayload
        {
            /** @brief Returns the handles of the buffers read, in io_reads_ order. */
            [[nodiscard]] SushiRuntime::span<const BufferHandle> inputs() const noexcept
            {
                return SushiRuntime::span<const BufferHandle>(handles.data(), input_count);
            }

            /** @brief Returns the handles of the buffers written, in io_writes_ order. */
            [[nodiscard]] SushiRuntime::span<BufferHandle> outputs() noexcept
            {
                return SushiRuntime::span<BufferHandle>(handles.data() + input_count,
                                                        handles.size() - input_count);
            }

            /** @brief Holds the input handles, then the output handles, in one block. */
            std::vector<BufferHandle> handles;

            /** @brief Counts the leading entries of handles that are inputs. */
            std::size_t input_count = 0;

            /** @brief Names why the node's rule cannot send it away; none when it can travel. */
            std::optional<UnreachableCause> unreachable;
        };

    } // namespace Distributed
} // namespace SushiRuntime
