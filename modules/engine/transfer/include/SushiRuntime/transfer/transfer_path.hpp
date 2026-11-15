/**************************************************************************/
/* transfer_path.hpp                                                      */
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
 * @file transfer_path.hpp
 * @brief Declares TransferPath, one way of copying bytes from one memory space to another.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>
#include <SushiRuntime/handle/transfer_path_id.hpp>
#include <SushiRuntime/memory/memory_space.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds one copy a path is asked to make: the spaces, the addresses and the length. */
        struct TransferRequest
        {
            MemorySpaceID source;          /**< Space the bytes are read from. */
            MemorySpaceID destination;     /**< Space the bytes are written to. */
            const void* from = nullptr;    /**< First byte to read, on the source space. */
            void* to = nullptr;            /**< First byte to write, on the destination space. */
            std::size_t bytes = 0;         /**< Bytes to copy. */
        };

        /**
         * @brief One way of copying bytes between two memory spaces.
         *
         * Each way is its own class behind this interface, so a new interconnect adds a file.
         */
        class TransferPath
        {
            public:
                virtual ~TransferPath() = default;

                /** @brief Returns the name of the path, for logs, reports and the profile file only. */
                [[nodiscard]] virtual std::string_view name() const noexcept = 0;

                /**
                 * @brief Returns the identity of the path.
                 *
                 * It is fixed per path class and the same on every run. Selecting, pricing and recording
                 * a path key on it, never on name().
                 */
                [[nodiscard]] virtual TransferPathID id() const noexcept = 0;

                /** @brief Returns whether this path copies from source to destination. */
                [[nodiscard]] virtual bool connects(const Memory::MemorySpace& source,
                                                    const Memory::MemorySpace& destination) const = 0;

                /**
                 * @brief Starts the copy after dependencies and returns the event that ends it.
                 * @pre connects() holds for the two spaces of request.
                 * @return The event of the copy, or the error that stopped it; a path never throws.
                 */
                [[nodiscard]] virtual Result<sycl::event> copy(const TransferRequest& request,
                                                       const std::vector<sycl::event>& dependencies) = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
