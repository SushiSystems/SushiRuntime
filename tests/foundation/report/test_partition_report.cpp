/**************************************************************************/
/* test_partition_report.cpp                                              */
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
 * @file test_partition_report.cpp
 * @brief Tests the text form of PartitionReport.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <string>

#include <SushiRuntime/report/partition_report.hpp>

using namespace SushiRuntime;

namespace
{
    DeviceKey host_device(std::uint32_t ordinal)
    {
        DeviceKey key;
        key.ordinal = ordinal;
        return key;
    }

    TEST(Small_PartitionReport, APlanWritesAHeaderAndOneLinePerDeviceInRowOrder)
    {
        PartitionReport report;
        report.step_seconds = 0.5;
        report.floor_taken = true;
        report.measured_step_seconds = 0.75;
        report.floor_step_seconds = 0.5;
        report.rows.push_back(PartitionDeviceRow{host_device(1), 1, 3, 4096, 0.25, 0, 0.375, {}});
        report.rows.push_back(PartitionDeviceRow{host_device(2), 2, 0, 0, 0.0, 0, 0.0, {}});

        const std::string text = report.to_text();

        EXPECT_EQ(text,
                  "partition: step 0.5 s, measured 0.75 s, floor 0.5 s taken\n"
                  "  " + to_text(host_device(1)) + " pool 1 nodes 3 bytes 4096 modelled 0.25 s measured 0.375 s\n"
                  "  " + to_text(host_device(2)) + " pool 2 nodes 0 bytes 0 modelled 0 s measured 0 s\n");
    }

    TEST(Small_PartitionReport, ADeviceLineNamesEachPathItsCopiesTook)
    {
        PartitionReport report;
        PartitionDeviceRow row{host_device(1), 1, 1, 0, 0.0, 0, 0.0, {}};
        row.copies.push_back(PartitionCopyRow{"host_staged", 2.0, 512.0});
        report.rows.push_back(row);

        const std::string text = report.to_text();

        EXPECT_EQ(text,
                  "partition: step 0 s, measured 0 s, floor 0 s not taken\n"
                  "  " + to_text(host_device(1)) +
                      " pool 1 nodes 1 bytes 0 modelled 0 s measured 0 s copies host_staged 2 512 bytes\n");
    }

    TEST(Small_PartitionReport, ARefusalWritesItsSentenceAndTheBytesEachPoolLacks)
    {
        PartitionReport report;
        report.refused = true;
        report.refusal = "no plan fits";
        report.rows.push_back(PartitionDeviceRow{host_device(1), 1, 0, 0, 0.0, 512, 0.0, {}});

        const std::string text = report.to_text();

        EXPECT_EQ(text,
                  "partition refused: no plan fits\n"
                  "  " + to_text(host_device(1)) + " pool 1 nodes 0 bytes 0 modelled 0 s measured 0 s short 512 bytes\n");
    }

    TEST(Small_PartitionReport, AnEmptyReportWritesOnlyTheHeader)
    {
        EXPECT_EQ(PartitionReport{}.to_text(), "partition: step 0 s, measured 0 s, floor 0 s not taken\n");
    }
} // namespace
