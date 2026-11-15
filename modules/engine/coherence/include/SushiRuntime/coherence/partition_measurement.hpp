/**************************************************************************/
/* partition_measurement.hpp                                              */
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
 * @file partition_measurement.hpp
 * @brief Declares PartitionMeasurement, what the runs of a plan measured per step.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <map>
#include <vector>

#include <SushiRuntime/handle/device_key.hpp>
#include <SushiRuntime/report/partition_report.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds what the runs of a plan measured, each figure per step; empty before a measured run. */
        struct PartitionMeasurement
        {
            double step_seconds = 0.0;                                  /**< Measured seconds of one step. */
            std::map<DeviceKey, double> device_seconds;                 /**< Device time of one step, per device. */
            std::map<DeviceKey, std::vector<PartitionCopyRow>> copies;  /**< Copies into each device per step, by path name. */
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
