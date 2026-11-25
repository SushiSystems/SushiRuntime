/**************************************************************************/
/* device_access_scope.hpp                                                */
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
 * @file device_access_scope.hpp
 * @brief Declares DeviceAccessScope, which holds a DeviceAccessGate open for its lifetime.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <SushiRuntime/memory/device_access_gate.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Opens a gate when it is built and closes it when it is destroyed; a null gate does
         *        nothing.
         * @see modules/platform/memory/README.md
         */
        class DeviceAccessScope final
        {
            public:
                /**
                 * @brief Opens a gate.
                 * @param gate Null for a runtime that guards nothing; otherwise it outlives the scope.
                 */
                explicit DeviceAccessScope(DeviceAccessGate* gate) noexcept : gate_(gate)
                {
                    if (gate_ != nullptr)
                        gate_->open();
                }

                DeviceAccessScope(const DeviceAccessScope&) = delete;
                DeviceAccessScope& operator=(const DeviceAccessScope&) = delete;
                DeviceAccessScope(DeviceAccessScope&&) = delete;
                DeviceAccessScope& operator=(DeviceAccessScope&&) = delete;

                /** @brief Closes the gate the constructor opened. */
                ~DeviceAccessScope()
                {
                    if (gate_ != nullptr)
                        gate_->close();
                }

            private:
                /** @brief The gate held open; null when there is none. */
                DeviceAccessGate* gate_;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
