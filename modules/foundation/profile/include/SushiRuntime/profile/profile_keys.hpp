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
 * @file profile_keys.hpp
 * @brief Declares the track and layer names the runtime writes into a record.
 * @author Mustafa Garip
 * @see modules/foundation/profile/README.md
 */

#pragma once

#include <array>
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

        /** @brief Lists every name above once, for the check that none repeats. */
        inline constexpr std::array<std::string_view, 4> K_ALL_KEYS{
            K_HOST_TRACK_NAME, K_KEY_LAYER, K_LAYER_SCHEDULER, K_LAYER_CHAIN};
    } // namespace Internal::Profile
} // namespace SushiRuntime
