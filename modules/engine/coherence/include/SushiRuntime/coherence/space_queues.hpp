/**************************************************************************/
/* space_queues.hpp                                                       */
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
 * @file space_queues.hpp
 * @brief Declares SpaceQueues, the port that names the queue a memory space copies on.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <sycl/sycl.hpp>

#include <SushiRuntime/handle/memory_space_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Names the queue that runs the copies into a memory space.
         *
         * The runtime answers it over its device set, so a transfer path never names a device
         * type.
         */
        class SpaceQueues
        {
            public:
                virtual ~SpaceQueues() = default;

                /**
                 * @brief Returns the queue that copies into space, or null when the space has none.
                 *
                 * The queue stays valid as long as the object that answers lives.
                 */
                [[nodiscard]] virtual sycl::queue* queue_of(MemorySpaceID space) const = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
