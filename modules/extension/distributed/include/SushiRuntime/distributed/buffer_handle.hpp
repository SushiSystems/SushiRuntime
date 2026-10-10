/**************************************************************************/
/* buffer_handle.hpp                                                      */
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
 * @file buffer_handle.hpp
 * @brief Declares GlobalBufferID and BufferHandle, the cluster-wide name of a shared buffer.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstdint>

namespace SushiRuntime::Distributed
{
    /**
     * @brief Identifies one shared buffer across the master and its workers.
     */
    struct GlobalBufferID
    {
        /** @brief Holds the identifier; 0 names no buffer. */
        std::uint64_t value = 0;

        /** @brief Returns whether two identifiers name the same buffer. */
        [[nodiscard]] constexpr bool operator==(const GlobalBufferID& other) const noexcept
        {
            return value == other.value;
        }

        /** @brief Returns whether two identifiers name different buffers. */
        [[nodiscard]] constexpr bool operator!=(const GlobalBufferID& other) const noexcept
        {
            return value != other.value;
        }

        /** @brief Returns whether the identifier names a buffer. */
        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return value != 0;
        }
    };

    /**
     * @brief Names one version of a shared buffer and its size.
     */
    struct BufferHandle
    {
        /** @brief Holds the buffer's identifier. */
        GlobalBufferID id;

        /** @brief Holds the version, which the master raises on every write. */
        std::uint32_t version = 0;

        /** @brief Holds the buffer's size in bytes. */
        std::uint64_t size_bytes = 0;
    };

    /**
     * @brief Holds the private codec of protocol version 2; these names reach it unqualified.
     */
    namespace Wire
    {
        using Distributed::BufferHandle;
        using Distributed::GlobalBufferID;
    } // namespace Wire
} // namespace SushiRuntime::Distributed
