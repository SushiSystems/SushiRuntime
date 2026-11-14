/**************************************************************************/
/* submit_form.hpp                                                        */
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
 * @file submit_form.hpp
 * @brief Declares SubmitForm, what a recorded command graph may keep of a node's submission.
 * @author Mustafa Garip
 * @see docs/design/GRAPH_REPLAY/README.md
 */

#pragma once

#include <cstdint>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /**
         * @brief Says whether a node's command group submits the same work at every step.
         * @see docs/design/GRAPH_REPLAY/README.md
         */
        enum class SubmitForm : std::uint8_t
        {
            StepBound,   /**< It may read host state that changes between steps; a recording keeps one step's. */
            Fixed,       /**< It submits the same work at every step. */
            LateWindow,  /**< A late-bound window: recorded at capacity, it reads its controls from host words. */
            DeviceSized  /**< It launches at capacity and reads its width from a word a predecessor wrote. */
        };

    } // namespace Graph
} // namespace SushiRuntime
