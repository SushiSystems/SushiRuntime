/**************************************************************************/
/* test_device_profile_probe.cpp                                          */
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
 * @file test_device_profile_probe.cpp
 * @brief Tests that ProfileMeasurement measures a SYCL CPU device through DeviceProfileProbe.
 * @see tests/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/device_profile/device_profile_probe.hpp>
#include <SushiRuntime/device_profile/profile_measurement.hpp>
#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/transfer/peer_path.hpp>
#include <SushiRuntime/transfer/same_context_path.hpp>

#include "support/fake_transfer_path.hpp"
#include "support/transfer_path_doubles.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Memory::DeviceMemorySpace;
using SushiRuntime::Internal::Memory::DeviceMemorySpaceDescription;

namespace
{
    const MemorySpaceID K_DEVICE_0(1);
    const MemorySpaceID K_DEVICE_1(2);

    /** @brief Returns the key of the host device with ordinal. */
    DeviceKey key_of(std::uint32_t ordinal)
    {
        DeviceKey key;
        key.ordinal = ordinal;
        return key;
    }

    /** @brief Answers two keys with one CPU queue and a space each. */
    class TwoDevices final : public ProbeDevices
    {
        public:
            TwoDevices(sycl::queue* queue, const Internal::Memory::MemorySpace& first,
                       const Internal::Memory::MemorySpace& second) noexcept
                : queue_(queue), first_(&first), second_(&second)
            {

            }

            [[nodiscard]] std::optional<ProbeDevice> find(const DeviceKey& key) const override
            {
                if (key == key_of(0))
                    return ProbeDevice{queue_, first_};
                if (key == key_of(1))
                    return ProbeDevice{queue_, second_};
                return std::nullopt;
            }

        private:
            sycl::queue* queue_;                          /**< The queue of both devices. */
            const Internal::Memory::MemorySpace* first_;  /**< Space of ordinal 0. */
            const Internal::Memory::MemorySpace* second_; /**< Space of ordinal 1. */
    };

    /** @brief Holds two devices of one context on a SYCL CPU queue. */
    class Medium_DeviceProfileProbe : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                if (!queues_.available())
                    GTEST_SKIP() << "SYCL offers no CPU device.";
            }

            /** @brief Returns a plan that times the generic kind and the same-context path. */
            [[nodiscard]] static MeasurementPlan plan()
            {
                MeasurementPlan measured;
                measured.latency_probe_bytes = 64;
                measured.bandwidth_probe_bytes = 1U << 20;
                measured.kinds = {OperationKinds::K_GENERIC};
                measured.paths = {PathTarget{key_of(1), SameContextPath::K_ID, "same_context"}};
                return measured;
            }

            /** @brief Returns a kernel of n work items, each one operation. */
            [[nodiscard]] static ProbeKernel counting_kernel()
            {
                return [](sycl::queue& queue) -> Result<std::uint64_t>
                {
                    constexpr std::size_t K_ITEMS = 1U << 16;
                    queue.parallel_for(sycl::range<1>(K_ITEMS), [](sycl::id<1>) {}).wait();
                    return static_cast<std::uint64_t>(K_ITEMS);
                };
            }

            FixedMemoryProbe memory_probe_;
            DeviceMemorySpace device_0_{DeviceMemorySpaceDescription{K_DEVICE_0, 1, {K_DEVICE_1}}, memory_probe_};
            DeviceMemorySpace device_1_{DeviceMemorySpaceDescription{K_DEVICE_1, 2, {K_DEVICE_0}}, memory_probe_};
            CPUSpaceQueues queues_;
    };

    TEST_F(Medium_DeviceProfileProbe, MeasuresADeviceThroughProfileMeasurement)
    {
        const TwoDevices devices(queues_.queue_of(K_DEVICE_0), device_0_, device_1_);
        SameContextPath path(queues_);
        DeviceProfileProbe probe(devices, {&path}, {{OperationKinds::K_GENERIC, counting_kernel()}});
        ProfileMeasurement measurement(probe, plan());

        const Result<DeviceProfile> profile = measurement.measure(key_of(0));

        ASSERT_TRUE(profile) << profile.error().message;
        EXPECT_EQ(profile.value().capacity_bytes, device_0_.capacity_bytes());
        EXPECT_EQ(profile.value().reserve_bytes, device_0_.reserve_bytes());
        EXPECT_GT(profile.value().memory_bandwidth, 0.0);
        EXPECT_GT(profile.value().base_throughput, 0.0);
        ASSERT_EQ(profile.value().paths.count(SameContextPath::K_ID), 1U);
        EXPECT_GT(profile.value().paths.at(SameContextPath::K_ID).bandwidth, 0.0);
    }

    TEST_F(Medium_DeviceProfileProbe, RefusesAPathNoneOfItsPathsNames)
    {
        const TwoDevices devices(queues_.queue_of(K_DEVICE_0), device_0_, device_1_);
        SameContextPath path(queues_);
        DeviceProfileProbe probe(devices, {&path}, {});

        const Result<CopySample> sample = probe.measure_path_copy(key_of(0), key_of(1), PeerPath::K_ID, 64);

        ASSERT_FALSE(sample);
        EXPECT_EQ(sample.error().code, ErrorCode::Unsupported);
    }

    TEST_F(Medium_DeviceProfileProbe, RefusesAKindWithNoRegisteredKernel)
    {
        const TwoDevices devices(queues_.queue_of(K_DEVICE_0), device_0_, device_1_);
        DeviceProfileProbe probe(devices, {}, {});

        const Result<KernelSample> sample = probe.measure_kernel(key_of(0), OperationKinds::K_GENERIC);

        ASSERT_FALSE(sample);
        EXPECT_EQ(sample.error().code, ErrorCode::InvalidArgument);
    }

    TEST_F(Medium_DeviceProfileProbe, RefusesAnUnknownDevice)
    {
        const TwoDevices devices(queues_.queue_of(K_DEVICE_0), device_0_, device_1_);
        DeviceProfileProbe probe(devices, {}, {});

        EXPECT_FALSE(probe.measure_capacity(key_of(7)));
    }
} // namespace
