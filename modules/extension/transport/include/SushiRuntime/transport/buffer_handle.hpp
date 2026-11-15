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
 * @file buffer_handle.hpp
 * @brief Declares BufferHandle, the version and size of one shared buffer named by its GlobalHandle.
 * @author Mustafa Garip
 * @see modules/extension/transport/README.md
 */

#pragma once

#include <cstdint>

#include <SushiRuntime/handle/global_handle.hpp>

namespace SushiRuntime::Distributed
{
    /**
     * @brief Names one version of a shared buffer and its size.
     */
    struct BufferHandle
    {
        /** @brief Holds the buffer's global handle. */
        GlobalHandle id;

        /** @brief Holds the version, which the master raises on every write. */
        std::uint32_t version = 0;

        /** @brief Holds the buffer's size in bytes. */
        std::uint64_t size_bytes = 0;
    };

    /**
     * @brief Holds the private codec of protocol version 5; these names reach it unqualified.
     */
    namespace Wire
    {
        using Distributed::BufferHandle;
    } // namespace Wire
} // namespace SushiRuntime::Distributed
