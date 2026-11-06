/**************************************************************************/
/* profile_keys.hpp                                                       */
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
 * @file profile_keys.hpp
 * @brief Declares the track and layer names the runtime writes into a record.
 * @author Mustafa Garip
 * @see modules/foundation/profile/README.md
 */

#pragma once

#include <string_view>

namespace SushiRuntime
{
    namespace Internal::Profile
    {
        /** @brief Names the track that holds the spans of nodes that ran on a worker thread. */
        inline constexpr std::string_view K_HOST_TRACK_NAME = "host";

        /** @brief Names the attribute that holds the execution layer of a span's run, as a name. */
        inline constexpr std::string_view K_KEY_LAYER = "layer";

        /** @brief Names the layer value of a run the worker pool dispatched. */
        inline constexpr std::string_view K_LAYER_SCHEDULER = "scheduler";

        /** @brief Names the layer value of a run submitted in order to one queue. */
        inline constexpr std::string_view K_LAYER_CHAIN = "chain";
    } // namespace Internal::Profile
} // namespace SushiRuntime
