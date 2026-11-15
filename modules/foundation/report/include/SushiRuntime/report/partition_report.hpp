/**************************************************************************/
/* partition_report.hpp                                                   */
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
 * @file partition_report.hpp
 * @brief Declares PartitionDeviceRow and PartitionReport, what a partition gave each device.
 * @author Mustafa Garip
 * @see modules/foundation/report/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <SushiRuntime/handle/device_key.hpp>

namespace SushiRuntime
{
    /** @brief Holds the copies into one device that took one path, per step. */
    struct PartitionCopyRow
    {
        std::string path;              /**< Name of the path, as the transfer path names itself. */
        double copies = 0.0;           /**< Copies per step. */
        double bytes = 0.0;            /**< Bytes copied per step. */
    };

    /** @brief Holds what one device received in a partition. */
    struct PartitionDeviceRow
    {
        DeviceKey device;                      /**< The device. */
        std::uint32_t pool = 0;                /**< Memory pool the device draws from. */
        std::size_t nodes = 0;                 /**< Nodes assigned to the device. */
        std::uint64_t bytes = 0;               /**< Bytes of the buffers homed on the device. */
        double modelled_seconds = 0.0;         /**< Modelled time of the nodes assigned to the device. */
        std::uint64_t shortfall_bytes = 0;     /**< Bytes the device's pool lacked when the job was refused; zero otherwise. */
        double measured_seconds = 0.0;         /**< Measured device time per step; zero before a measured run. */
        std::vector<PartitionCopyRow> copies;  /**< Copies into the device per path, by path name. */
    };

    /**
     * @brief Holds one row per device of a partition, in DeviceKey order, and what the plan as a whole costs.
     *
     * A device that received no node has a row of zeros. A refused job has rows with no nodes and the
     * shortfall of each pool.
     * @see modules/foundation/report/README.md
     */
    struct PartitionReport
    {
        std::vector<PartitionDeviceRow> rows;  /**< One row per device. */
        double step_seconds = 0.0;             /**< Modelled seconds of one step of the plan. */
        double measured_step_seconds = 0.0;    /**< Measured seconds of one step; zero before a measured run. */
        double floor_step_seconds = 0.0;       /**< Modelled seconds of one step of the floor; zero when no floor fits. */
        std::vector<DeviceKey> floor_devices;  /**< Devices the floor uses, in DeviceKey order. */
        bool floor_taken = false;              /**< Whether the floor plan replaced the scheduled one. */
        bool refused = false;                  /**< Whether the job was refused before loading. */
        std::string refusal;                   /**< The sentence of the refusal; empty when none. */

        /** @brief Returns the report as lines of text for a log: one header line, one line per device. */
        [[nodiscard]] std::string to_text() const
        {
            std::ostringstream text;
            text << std::setprecision(6);
            if (refused)
                text << "partition refused: " << refusal << '\n';
            else
                text << "partition: step " << step_seconds << " s, measured " << measured_step_seconds << " s, floor "
                     << floor_step_seconds << " s " << (floor_taken ? "taken" : "not taken") << '\n';
            for (const PartitionDeviceRow& row : rows)
            {
                text << "  " << SushiRuntime::to_text(row.device) << " pool " << row.pool << " nodes " << row.nodes << " bytes "
                     << row.bytes << " modelled " << row.modelled_seconds << " s measured " << row.measured_seconds << " s";
                for (const PartitionCopyRow& copy : row.copies)
                    text << " copies " << copy.path << ' ' << copy.copies << ' ' << copy.bytes << " bytes";
                if (row.shortfall_bytes != 0)
                    text << " short " << row.shortfall_bytes << " bytes";
                text << '\n';
            }
            return text.str();
        }
    };
} // namespace SushiRuntime
