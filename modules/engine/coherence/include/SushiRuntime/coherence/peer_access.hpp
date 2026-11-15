/**************************************************************************/
/* peer_access.hpp                                                        */
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
 * @file peer_access.hpp
 * @brief Declares PeerAccess, the port that says whether one device may read another's memory.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/handle/memory_space_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Says whether the device of one space may read the memory of another space's device.
         *
         * The runtime answers it where the backend offers peer access; where it does not, the answer
         * is false and no peer copy is planned.
         */
        class PeerAccess
        {
            public:
                virtual ~PeerAccess() = default;

                /**
                 * @brief Returns whether the device of reader reads the memory of owner, and turns the
                 *        access on when it is not on yet.
                 */
                [[nodiscard]] virtual bool grant(MemorySpaceID reader, MemorySpaceID owner) = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
