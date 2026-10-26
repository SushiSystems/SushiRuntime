/**************************************************************************/
/* device_track_name.cpp                                                  */
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
 * @file device_track_name.cpp
 * @brief Implements the functions that name the tracks of the device source.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#include <SushiRuntime/profiling/device_track_name.hpp>

#include <cstddef>
#include <vector>

#include <SushiRuntime/machine/device_description.hpp>
#include <SushiRuntime/profile/profile_keys.hpp>

namespace SushiRuntime
{
    std::string device_track_name(const Runtime& runtime, std::uint32_t device_index)
    {
        const std::vector<DeviceDescription>& devices = runtime.advanced().devices();
        const std::string_view device_name = device_index < devices.size()
            ? std::string_view(devices[device_index].name) : std::string_view("device");
        return device_track_name(runtime.topology().name, device_index, device_name);
    }

    std::string device_track_name(std::string_view node_name, std::uint32_t device_index,
                                  std::string_view device_name)
    {
        std::string track;
        track.reserve(node_name.size() + device_name.size() + 12);
        track.append(node_name);
        track.push_back('/');
        track.append(std::to_string(device_index));
        track.push_back('/');
        track.append(device_name);
        return track;
    }

    std::string host_track_name(std::string_view node_name)
    {
        std::string track;
        track.reserve(node_name.size() + 6);
        track.append(node_name);
        track.push_back('/');
        track.append(Internal::Profile::K_HOST_TRACK_NAME);
        return track;
    }
} // namespace SushiRuntime
