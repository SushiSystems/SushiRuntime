/**************************************************************************/
/* test_device_profile_file.cpp                                         */
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
 * @file test_device_profile_file.cpp
 * @brief Tests that the profile file round-trips, writes in a fixed order and refuses what it cannot read.
 * @see tests/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include <SushiRuntime/device_profile/cost_model.hpp>
#include <SushiRuntime/device_profile/device_profile_file.hpp>

#include "support/fake_profile_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    DeviceProfileSet two_devices()
    {
        DeviceProfileSet profiles;
        profiles.set(host_key(2), round_profile(2.0));
        profiles.set(host_key(1), round_profile(1.0));
        return profiles;
    }

    NodeCost sample_cost(OperationKind kind)
    {
        NodeCost cost;
        cost.operations = 4000;
        cost.read_bytes = 1000;
        cost.written_bytes = 1000;
        cost.kind = kind;
        return cost;
    }

    TEST(Small_DeviceProfileFile, AProfileWrittenOnceAndReadBackGivesTheSameCostModelTimes)
    {
        const DeviceProfileSet original = two_devices();
        const Result<std::string> text = DeviceProfileFile::serialise(original);
        ASSERT_TRUE(text.has_value());
        const Result<DeviceProfileSet> parsed = DeviceProfileFile::parse(text.value());
        ASSERT_TRUE(parsed.has_value());

        EXPECT_EQ(parsed.value(), original);
        const CostModel before(original);
        const CostModel after(parsed.value());
        for (const OperationKind kind : {OperationKinds::K_GENERIC, OperationKinds::K_COMPUTE_BOUND})
        {
            for (std::uint32_t ordinal : {1u, 2u})
            {
                EXPECT_EQ(before.node_time(host_key(ordinal), sample_cost(kind)),
                          after.node_time(host_key(ordinal), sample_cost(kind)));
            }
        }
        EXPECT_EQ(before.transfer_time(host_key(1), host_key(2), "pcie", 4096),
                  after.transfer_time(host_key(1), host_key(2), "pcie", 4096));
    }

    TEST(Small_DeviceProfileFile, AChangedFileChangesTheCostModelTimes)
    {
        const Result<std::string> text = DeviceProfileFile::serialise(two_devices());
        ASSERT_TRUE(text.has_value());
        std::string changed = text.value();
        const std::string::size_type at = changed.find("base_throughput 2000\n");
        ASSERT_NE(at, std::string::npos);
        changed.replace(at, 21, "base_throughput 4000\n");

        const Result<DeviceProfileSet> parsed = DeviceProfileFile::parse(changed);
        ASSERT_TRUE(parsed.has_value());
        const CostModel before(two_devices());
        const CostModel after(parsed.value());
        EXPECT_NE(before.node_time(host_key(1), sample_cost(OperationKinds::K_GENERIC)),
                  after.node_time(host_key(1), sample_cost(OperationKinds::K_GENERIC)));
    }

    TEST(Small_DeviceProfileFile, TheWriteOrderDoesNotDependOnTheInsertionOrder)
    {
        DeviceProfileSet forward;
        forward.set(host_key(1), round_profile(1.0));
        forward.set(host_key(2), round_profile(2.0));
        DeviceProfileSet backward;
        backward.set(host_key(2), round_profile(2.0));
        backward.set(host_key(1), round_profile(1.0));

        const Result<std::string> first = DeviceProfileFile::serialise(forward);
        const Result<std::string> second = DeviceProfileFile::serialise(backward);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(first.value(), second.value());
        EXPECT_LT(first.value().find(to_text(host_key(1))), first.value().find(to_text(host_key(2))));
    }

    TEST(Small_DeviceProfileFile, ADeviceMissingFromTheFileHasNoProfile)
    {
        const Result<DeviceProfileSet> parsed =
            DeviceProfileFile::parse(DeviceProfileFile::serialise(two_devices()).value());
        ASSERT_TRUE(parsed.has_value());
        const CostModel model(parsed.value());

        EXPECT_TRUE(model.has_profile(host_key(1)));
        EXPECT_FALSE(model.has_profile(host_key(3)));
        EXPECT_FALSE(model.node_time(host_key(3), sample_cost(OperationKinds::K_GENERIC)).has_value());
    }

    TEST(Small_DeviceProfileFile, ANewerVersionIsRefusedNamingBothVersions)
    {
        const Result<DeviceProfileSet> parsed = DeviceProfileFile::parse("format_version 3\n");

        ASSERT_FALSE(parsed.has_value());
        EXPECT_EQ(parsed.error().code, ErrorCode::Unsupported);
        EXPECT_NE(parsed.error().message.find('3'), std::string::npos);
        EXPECT_NE(parsed.error().message.find('2'), std::string::npos);
    }

    TEST(Small_DeviceProfileFile, HostLinksAreWrittenAndReadBack)
    {
        DeviceProfile profile = round_profile(1.0);
        profile.host_to_device = PathProfile{250.0, 0.125};
        profile.device_to_host = PathProfile{200.0, 0.0625};
        DeviceProfileSet profiles;
        profiles.set(host_key(1), profile);

        const Result<std::string> text = DeviceProfileFile::serialise(profiles);
        ASSERT_TRUE(text.has_value());
        EXPECT_EQ(text.value().rfind("format_version 2\n", 0), 0u);
        const Result<DeviceProfileSet> parsed = DeviceProfileFile::parse(text.value());
        ASSERT_TRUE(parsed.has_value()) << parsed.error().message;

        EXPECT_EQ(parsed.value(), profiles);
    }

    TEST(Small_DeviceProfileFile, AVersionOneFileIsUpgradedWithUnmeasuredHostLinks)
    {
        const std::string key = to_text(host_key(1));
        const Result<DeviceProfileSet> parsed = DeviceProfileFile::parse(
            "format_version 1\ndevice " + key +
            "\ncapacity_bytes 1\nreserve_bytes 0\nmemory_bandwidth 1\nbase_throughput 1\nend\n");

        ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
        const DeviceProfile* profile = parsed.value().find(host_key(1));
        ASSERT_NE(profile, nullptr);
        EXPECT_FALSE(profile->host_to_device.has_value());
        EXPECT_FALSE(profile->device_to_host.has_value());
    }

    TEST(Small_DeviceProfileFile, AHostLinkInAVersionOneFileIsRefused)
    {
        const std::string key = to_text(host_key(1));
        const Result<DeviceProfileSet> parsed = DeviceProfileFile::parse(
            "format_version 1\ndevice " + key +
            "\ncapacity_bytes 1\nreserve_bytes 0\nmemory_bandwidth 1\nbase_throughput 1\n"
            "host_to_device 1 0\nend\n");

        ASSERT_FALSE(parsed.has_value());
        EXPECT_EQ(parsed.error().code, ErrorCode::InvalidArgument);
    }

    TEST(Small_DeviceProfileFile, MalformedTextIsRefusedWithItsLine)
    {
        const std::string key = to_text(host_key(1));
        EXPECT_FALSE(DeviceProfileFile::parse("").has_value());
        EXPECT_FALSE(DeviceProfileFile::parse("device " + key + "\n").has_value());
        EXPECT_FALSE(DeviceProfileFile::parse("format_version 1\ndevice " + key + "\n").has_value());
        EXPECT_FALSE(DeviceProfileFile::parse("format_version 1\ncapacity_bytes 5\n").has_value());

        const Result<DeviceProfileSet> negative = DeviceProfileFile::parse(
            "format_version 1\ndevice " + key +
            "\ncapacity_bytes 1\nreserve_bytes 0\nmemory_bandwidth -1\nbase_throughput 1\nend\n");
        ASSERT_FALSE(negative.has_value());
        EXPECT_EQ(negative.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(negative.error().message.find("line 5"), std::string::npos);
    }

    TEST(Small_DeviceProfileFile, APathNameWithWhiteSpaceIsRefusedOnWrite)
    {
        DeviceProfileSet profiles;
        DeviceProfile profile = round_profile(1.0);
        profile.paths["two words"] = PathProfile{1.0, 0.0};
        profiles.set(host_key(1), profile);

        const Result<std::string> text = DeviceProfileFile::serialise(profiles);

        ASSERT_FALSE(text.has_value());
        EXPECT_EQ(text.error().code, ErrorCode::InvalidArgument);
    }

    TEST(Small_DeviceProfileFile, AFileOnDiskReadsBackEqualAndAMissingFileIsRefused)
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "sushiruntime_test_device_profile_file.txt";
        std::filesystem::remove(path);

        ASSERT_TRUE(DeviceProfileFile::write(path, two_devices()).has_value());
        const Result<DeviceProfileSet> read = DeviceProfileFile::read(path);
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read.value(), two_devices());

        std::filesystem::remove(path);
        EXPECT_FALSE(DeviceProfileFile::read(path).has_value());
    }
} // namespace
