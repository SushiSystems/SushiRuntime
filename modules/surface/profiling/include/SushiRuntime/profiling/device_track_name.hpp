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
 * @file device_track_name.hpp
 * @brief Declares the functions that name the tracks of the device source.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/machine/device_description.hpp>

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
     * @brief Returns the track of device @p device_index among @p devices on node @p node_name.
     *
     * A device index @p devices does not hold names a device called "device". The Runtime overload
     * above forwards here with its own node and devices.
     * @see modules/surface/profiling/README.md
     */
    [[nodiscard]] SUSHIRUNTIME_API std::string device_track_name(std::string_view node_name,
                                                                 std::uint32_t device_index,
                                                                 const std::vector<DeviceDescription>& devices);

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
