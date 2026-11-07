/**************************************************************************/
/* in_memory_device_enumerator.hpp                                        */
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
 * @file in_memory_device_enumerator.hpp
 * @brief Declares InMemoryDeviceEnumerator, a DeviceEnumerator that returns the list it was built with.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/topology/device_enumerator.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Returns the devices it was built with, or a fixed Unsupported error.
         * @see modules/platform/topology/README.md
         */
        class InMemoryDeviceEnumerator final : public DeviceEnumerator
        {
            public:
                /** @param devices The list every enumerate() call returns. */
                explicit InMemoryDeviceEnumerator(std::vector<DeviceDescriptor> devices)
                    : devices_(std::move(devices))
                {
                }

                /**
                 * @brief Builds an enumerator whose enumerate() always fails.
                 * @param runtime_text The message of the Unsupported error it returns.
                 */
                [[nodiscard]] static InMemoryDeviceEnumerator failing(std::string runtime_text)
                {
                    InMemoryDeviceEnumerator enumerator{std::vector<DeviceDescriptor>{}};
                    enumerator.failure_ = std::move(runtime_text);
                    return enumerator;
                }

                [[nodiscard]] Result<std::vector<DeviceDescriptor>> enumerate() override
                {
                    if (failure_.has_value())
                        return make_error(Error{ErrorCode::Unsupported, *failure_});
                    return devices_;
                }

            private:
                /// @brief The list enumerate() returns.
                std::vector<DeviceDescriptor> devices_;
                /// @brief The error text of a failing enumerator; empty for one that lists.
                std::optional<std::string> failure_;
        };
    } // namespace Internal::Topology
} // namespace SushiRuntime
