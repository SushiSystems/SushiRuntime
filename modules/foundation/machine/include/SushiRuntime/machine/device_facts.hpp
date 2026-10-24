/**************************************************************************/
/* device_facts.hpp                                                       */
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
 * @file device_facts.hpp
 * @brief Declares DeviceFacts, what a machine reports about one device to another machine.
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <string>

#include <SushiRuntime/machine/device_kind.hpp>

namespace SushiRuntime
{
    /**
     * @brief Describes one device in terms that hold on any machine.
     */
    struct DeviceFacts
    {
        DeviceKind kind = DeviceKind::Other; /**< Class of the device. */
        std::string name;                    /**< Name the device reports. */
        std::uint64_t memory_bytes = 0;      /**< Global memory of the device. */
    };
} // namespace SushiRuntime
