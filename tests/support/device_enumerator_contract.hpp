/**************************************************************************/
/* device_enumerator_contract.hpp                                         */
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
 * @file device_enumerator_contract.hpp
 * @brief Declares DeviceEnumeratorContract, the typed suite every DeviceEnumerator adapter passes.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/machine/device_kind.hpp>
#include <SushiRuntime/topology/device_enumerator.hpp>

namespace TestSupport
{
    /**
     * @brief Fixture of the enumerator contract; Maker::make() returns the enumerator under test.
     * @tparam Maker A type with a static make() returning std::unique_ptr<DeviceEnumerator>.
     */
    template <class Maker>
    class DeviceEnumeratorContract : public ::testing::Test
    {
        protected:
            /** @brief Makes the enumerator each case runs on. */
            void SetUp() override { enumerator_ = Maker::make(); }

            /** @brief Returns what the enumerator lists; fails the case on an error. */
            [[nodiscard]] std::vector<SushiRuntime::Internal::Topology::DeviceDescriptor> listed()
            {
                SushiRuntime::Result<std::vector<SushiRuntime::Internal::Topology::DeviceDescriptor>>
                    devices = enumerator_->enumerate();
                EXPECT_TRUE(devices.has_value()) << devices.error().message;
                if (!devices.has_value())
                    return {};
                return *devices;
            }

        private:
            /** @brief The enumerator under test, one per case. */
            std::unique_ptr<SushiRuntime::Internal::Topology::DeviceEnumerator> enumerator_;
    };

    TYPED_TEST_SUITE_P(DeviceEnumeratorContract);

    /** @brief Tests that an enumeration lists a device, each with a name. */
    TYPED_TEST_P(DeviceEnumeratorContract, ListsNamedDevices)
    {
        const auto devices = this->listed();

        ASSERT_FALSE(devices.empty());
        for (const auto& device : devices)
            EXPECT_FALSE(device.name.empty());
    }

    /** @brief Tests that a CPU or an Other device reports no PCI address text. */
    TYPED_TEST_P(DeviceEnumeratorContract, OnlyAGPUOrAnAcceleratorReportsAPCIAddress)
    {
        for (const auto& device : this->listed())
            if (device.kind == SushiRuntime::DeviceKind::CPU || device.kind == SushiRuntime::DeviceKind::Other)
                EXPECT_TRUE(device.pci_text.empty()) << device.name;
    }

    /** @brief Tests that two enumerations list the same devices in the same order. */
    TYPED_TEST_P(DeviceEnumeratorContract, TwoEnumerationsAgree)
    {
        const auto first = this->listed();
        const auto second = this->listed();

        ASSERT_EQ(first.size(), second.size());
        for (std::size_t index = 0; index < first.size(); ++index)
            EXPECT_EQ(first[index].name, second[index].name);
    }

    REGISTER_TYPED_TEST_SUITE_P(DeviceEnumeratorContract, ListsNamedDevices,
                                OnlyAGPUOrAnAcceleratorReportsAPCIAddress, TwoEnumerationsAgree);

} // namespace TestSupport
