/**************************************************************************/
/* coherence_history.hpp                                                  */
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
 * @file coherence_history.hpp
 * @brief Declares CoherenceHistory, the port that drops the coherence history of retired steps.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Drops the coherence history the live handles keep.
         */
        class CoherenceHistory
        {
            public:
                /**
                 * @brief Retires the history of every live handle.
                 * Each map is retired under its own lock, so a plan or a run may be in flight.
                 * @return The intervals left across the live handles.
                 */
                [[nodiscard]] virtual std::size_t retire_coherence_history() = 0;

            protected:
                /** @brief Keeps a history from being destroyed through this interface. */
                ~CoherenceHistory() = default;
        };

    } // namespace Internal::Memory
} // namespace SushiRuntime
