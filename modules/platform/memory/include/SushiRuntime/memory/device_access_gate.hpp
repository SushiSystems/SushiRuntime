/**************************************************************************/
/* device_access_gate.hpp                                                 */
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
 * @file device_access_gate.hpp
 * @brief Declares DeviceAccessGate, the port that opens guarded device memory while runtime work runs.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Opens device memory to every thread while a runtime kernel or copy runs, and closes
         *        it when none runs.
         *
         * Opens are counted: the memory stays open until each open() has its close().
         * @see modules/platform/memory/README.md
         */
        class DeviceAccessGate
        {
            public:
                /** @brief Closes nothing; the owner of the gate has closed every open first. */
                virtual ~DeviceAccessGate() = default;

                /** @brief Counts one more holder and opens the memory when it is the first. */
                virtual void open() noexcept = 0;

                /**
                 * @brief Counts one holder less and closes the memory when it was the last.
                 * @pre An open() of this gate has not been closed yet.
                 */
                virtual void close() noexcept = 0;

            protected:
                DeviceAccessGate() = default;
                DeviceAccessGate(const DeviceAccessGate&) = default;
                DeviceAccessGate& operator=(const DeviceAccessGate&) = default;
                DeviceAccessGate(DeviceAccessGate&&) = default;
                DeviceAccessGate& operator=(DeviceAccessGate&&) = default;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
