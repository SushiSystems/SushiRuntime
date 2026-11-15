/**************************************************************************/
/* planned_host_access.hpp                                                */
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
 * @file planned_host_access.hpp
 * @brief Declares PlannedHostAccess, the host's way into a Residency::Planned buffer.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <SushiRuntime/coherence/instance_allocator.hpp>
#include <SushiRuntime/coherence/transfer_path.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>
#include <SushiRuntime/memory/memory_space.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Mints Residency::Planned buffers and reads and writes their bytes from the host.
         *
         * A host write lands in the host instance and invalidates every other space; a host read
         * first copies to the host what only another space holds. Calls run between runs.
         * @see modules/engine/coherence/README.md
         */
        class PlannedHostAccess final
        {
            public:
                /**
                 * @brief Builds the access over a runtime's directory, spaces, paths and allocator.
                 * @param host The host memory space; spaces holds it.
                 * @pre Every argument outlives this object.
                 */
                PlannedHostAccess(Memory::ResidencyDirectory& directory,
                                  const std::vector<const Memory::MemorySpace*>& spaces,
                                  const std::vector<TransferPath*>& paths,
                                  InstanceAllocator& allocator,
                                  MemorySpaceID host) noexcept;

                /**
                 * @brief Mints a planned handle: size and layout, no instance, held nowhere.
                 * @return The handle, the null handle for zero bytes, or the error the directory
                 *         refused the entry with.
                 */
                [[nodiscard]] Result<GlobalHandle> mint(HandleKind kind, std::size_t bytes,
                                                        Memory::ElementLayout layout);

                /**
                 * @brief Writes host bytes into [begin, begin + bytes) of the host instance and
                 *        records the write on the host space.
                 * @return Nothing, AllocationFailed when no host instance can be made, or the
                 *         directory's error for a handle that is not live.
                 * @pre The range lies inside the buffer and source holds bytes bytes.
                 */
                [[nodiscard]] Result<void> write(GlobalHandle handle, std::size_t begin,
                                                 const void* source, std::size_t bytes);

                /**
                 * @brief Reads [begin, begin + bytes) into destination, copying to the host space
                 *        first what only another space holds.
                 * @return Nothing, Unsupported when no path reaches the host from a space that
                 *         holds the bytes, AllocationFailed, or the directory's error.
                 * @pre The range lies inside the buffer and destination holds bytes bytes.
                 */
                [[nodiscard]] Result<void> read(GlobalHandle handle, std::size_t begin,
                                                void* destination, std::size_t bytes);

                /** @brief Frees every instance the directory records for handle, then retires it. */
                void release(GlobalHandle handle) noexcept;

            private:
                /** @brief Returns handle's host instance, allocating it when there is none. */
                [[nodiscard]] Result<unsigned char*> host_instance(GlobalHandle handle);

                /** @brief Copies [begin, end) from the first space in spaces_ that holds it to the host. */
                [[nodiscard]] Result<void> fetch(GlobalHandle handle, std::size_t begin, std::size_t end,
                                                 std::uint64_t valid_spaces, unsigned char* host_bytes);

                /** @brief Returns the space with id, or null when spaces_ holds none. */
                [[nodiscard]] const Memory::MemorySpace* space_with(MemorySpaceID id) const noexcept;

                /// @brief Entries, instances and coherence maps of the buffers.
                Memory::ResidencyDirectory* directory_;

                /// @brief Spaces in the order a source is chosen.
                const std::vector<const Memory::MemorySpace*>* spaces_;

                /// @brief Paths in the order a path is chosen.
                const std::vector<TransferPath*>* paths_;

                /// @brief Makes and frees the host instance and frees every instance at release.
                InstanceAllocator* allocator_;

                /// @brief The host memory space.
                MemorySpaceID host_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
