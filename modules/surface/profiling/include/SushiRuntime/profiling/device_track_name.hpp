/**************************************************************************/
/* device_track_name.hpp                                                  */
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
 * @file device_track_name.hpp
 * @brief Declares the functions that name the tracks of the device source.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    /**
     * @brief Returns the track the device source puts the spans of @p device_index of @p runtime on.
     *
     * The one place that builds the string, so a library that states a fact keyed by the track
     * calls this and never rebuilds the name. A device index the runtime does not have names a
     * device called "device".
     * @see modules/surface/profiling/README.md
     */
    [[nodiscard]] SUSHIRUNTIME_API std::string device_track_name(const Runtime& runtime,
                                                                 std::uint32_t device_index);

    /**
     * @brief Returns the track of a device from the three parts it is built of.
     *
     * The source and the overload above both use it; a test names a track without a runtime.
     */
    [[nodiscard]] SUSHIRUNTIME_API std::string device_track_name(std::string_view node_name,
                                                                 std::uint32_t device_index,
                                                                 std::string_view device_name);

    /** @brief Returns the track the device source puts the spans of host-native nodes of a runtime on. */
    [[nodiscard]] SUSHIRUNTIME_API std::string host_track_name(std::string_view node_name);
} // namespace SushiRuntime
