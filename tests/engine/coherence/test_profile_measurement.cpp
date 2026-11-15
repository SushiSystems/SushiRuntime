/**************************************************************************/
/* test_profile_measurement.cpp                                         */
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
 * @file test_profile_measurement.cpp
 * @brief Tests that ProfileMeasurement turns probe samples into a profile and refuses a plan or a sample it cannot use.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/profile_measurement.hpp>

#include "fake_profile_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    MeasurementPlan plan_with_kinds()
    {
        MeasurementPlan plan;
        plan.latency_probe_bytes = 1;
        plan.bandwidth_probe_bytes = 1001;
        plan.kinds = {OperationKinds::K_GENERIC, "gemm"_kind};
        plan.paths = {PathTarget{host_key(2), "pcie"}};
        return plan;
    }

    TEST(Small_ProfileMeasurement, TheProfileHoldsTheRatesTheProbeReported)
    {
        FakeProfileProbe probe;
        ProfileMeasurement measurement(probe, plan_with_kinds());

        const Result<DeviceProfile> profile = measurement.measure(host_key(1));

        ASSERT_TRUE(profile.has_value());
        EXPECT_EQ(profile.value().capacity_bytes, 1000u);
        EXPECT_EQ(profile.value().reserve_bytes, 100u);
        EXPECT_DOUBLE_EQ(profile.value().memory_bandwidth, 2.0 * 1001.0 / (1001.0 / 500.0));
        EXPECT_DOUBLE_EQ(profile.value().base_throughput, 2000.0);
        EXPECT_DOUBLE_EQ(profile.value().throughputs.at("gemm"_kind), 2000.0);
        const PathProfile* path = profile.value().find_path("pcie");
        ASSERT_NE(path, nullptr);
        EXPECT_NEAR(path->latency, 0.5 + 1.0 / 100.0, 1e-12);
        EXPECT_NEAR(path->bandwidth, 100.1, 1e-9);
    }

    TEST(Small_ProfileMeasurement, HostLinksAreMeasuredOnlyWhenThePlanAsks)
    {
        FakeProfileProbe probe;
        MeasurementPlan plan = plan_with_kinds();
        ProfileMeasurement without(probe, plan);
        const Result<DeviceProfile> bare = without.measure(host_key(1));
        ASSERT_TRUE(bare.has_value());
        EXPECT_FALSE(bare.value().host_to_device.has_value());

        plan.host_links = true;
        ProfileMeasurement with(probe, plan);
        const Result<DeviceProfile> linked = with.measure(host_key(1));

        ASSERT_TRUE(linked.has_value());
        ASSERT_TRUE(linked.value().host_to_device.has_value());
        ASSERT_TRUE(linked.value().device_to_host.has_value());
        EXPECT_NEAR(linked.value().host_to_device->latency, 0.5 + 1.0 / 200.0, 1e-12);
        EXPECT_NEAR(linked.value().host_to_device->bandwidth, 200.2, 1e-9);
        EXPECT_NEAR(linked.value().device_to_host->bandwidth, 100.1, 1e-9);
    }

    TEST(Small_ProfileMeasurement, AFasterDeviceMeasuresFaster)
    {
        FakeProfileProbe probe;
        probe.speeds[host_key(2)] = 2.0;
        ProfileMeasurement measurement(probe, plan_with_kinds());

        const Result<DeviceProfileSet> profiles = measurement.measure_all({host_key(2), host_key(1)});

        ASSERT_TRUE(profiles.has_value());
        EXPECT_GT(profiles.value().find(host_key(2))->base_throughput,
                  profiles.value().find(host_key(1))->base_throughput);
        EXPECT_TRUE(profiles.value().has_profile(host_key(1)));
        EXPECT_FALSE(profiles.value().has_profile(host_key(3)));
    }

    TEST(Small_ProfileMeasurement, OnlyTheKindsThePlanNamesAreMeasured)
    {
        FakeProfileProbe probe;
        MeasurementPlan plan = plan_with_kinds();
        plan.kinds = {OperationKinds::K_GENERIC};
        plan.paths.clear();
        ProfileMeasurement measurement(probe, plan);

        const Result<DeviceProfile> profile = measurement.measure(host_key(1));

        ASSERT_TRUE(profile.has_value());
        EXPECT_TRUE(profile.value().throughputs.empty());
        EXPECT_TRUE(profile.value().paths.empty());
        EXPECT_EQ(probe.calls, 3);
    }

    TEST(Small_ProfileMeasurement, APlanWithoutTheGenericKindIsRefused)
    {
        FakeProfileProbe probe;
        MeasurementPlan plan = plan_with_kinds();
        plan.kinds = {"gemm"_kind};
        ProfileMeasurement measurement(probe, plan);

        const Result<DeviceProfile> profile = measurement.measure(host_key(1));

        ASSERT_FALSE(profile.has_value());
        EXPECT_EQ(profile.error().code, ErrorCode::InvalidArgument);
        EXPECT_EQ(probe.calls, 0);
    }

    TEST(Small_ProfileMeasurement, ALargeCopyNoBiggerThanTheSmallOneIsRefused)
    {
        FakeProfileProbe probe;
        MeasurementPlan plan = plan_with_kinds();
        plan.bandwidth_probe_bytes = plan.latency_probe_bytes;
        ProfileMeasurement measurement(probe, plan);

        EXPECT_FALSE(measurement.measure(host_key(1)).has_value());
    }

    TEST(Small_ProfileMeasurement, AProbeThatReportsNoTimeIsRefused)
    {
        FakeProfileProbe probe;
        probe.report_no_time = true;
        ProfileMeasurement measurement(probe, plan_with_kinds());

        const Result<DeviceProfile> profile = measurement.measure(host_key(1));

        ASSERT_FALSE(profile.has_value());
        EXPECT_EQ(profile.error().code, ErrorCode::InvalidState);
    }
} // namespace
