/**************************************************************************/
/* host_write_watch.hpp                                                   */
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
 * @file host_write_watch.hpp
 * @brief Declares HostWriteWatch, which tells a stored copy plan that the host wrote one of its buffers.
 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <vector>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Holds the buffers a copy plan was made over and the host-write generation it
         *        was made at, and says whether the host wrote one of them since.
         *
         * A check reads one atomic while no host write happened anywhere since it was armed.
         */
        class HostWriteWatch final
        {
            public:
                /** @brief Watches the buffers nodes read or write, from the directory's current generation. */
                HostWriteWatch(const std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                               const Internal::Memory::ResidencyDirectory& directory);

                /**
                 * @brief Returns whether the host wrote a watched buffer since the watch was armed.
                 *
                 * A host write that touched no watched buffer moves the armed generation up, so the
                 * next check reads one atomic again.
                 */
                [[nodiscard]] bool written(const Internal::Memory::ResidencyDirectory& directory) noexcept;

            private:
                /// @brief The watched buffers, sorted and unique.
                std::vector<GlobalHandle> handles_;

                /// @brief Host-write generation no watched buffer was written after.
                std::uint64_t armed_;
        };
    } // namespace Detail
} // namespace SushiRuntime
