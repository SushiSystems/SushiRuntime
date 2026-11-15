/**************************************************************************/
/* uuid_device_key_source.hpp                                             */
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
 * @file uuid_device_key_source.hpp
 * @brief Declares the DeviceKeySource of each backend that reports a device UUID.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <optional>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/topology/device_key_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Names a device by its UUID when the device is on one backend.
         *
         * The three backends that report a UUID derive from it.
         */
        class SUSHIRUNTIME_API UUIDDeviceKeySource : public DeviceKeySource
        {
            public:
                [[nodiscard]] std::optional<DeviceKey>
                key_of(const DeviceIdentity& identity) const override;

            protected:
                /** @param backend The only backend whose devices this source names. */
                explicit UUIDDeviceKeySource(DeviceBackend backend) noexcept;

            private:
                /// @brief The backend whose devices this source names.
                DeviceBackend backend_;
        };

        /** @brief Names a CUDA device by its UUID. */
        class SUSHIRUNTIME_API CUDAUUIDDeviceKeySource final : public UUIDDeviceKeySource
        {
            public:
                CUDAUUIDDeviceKeySource() noexcept;
        };

        /** @brief Names a HIP device by its UUID. */
        class SUSHIRUNTIME_API HIPUUIDDeviceKeySource final : public UUIDDeviceKeySource
        {
            public:
                HIPUUIDDeviceKeySource() noexcept;
        };

        /** @brief Names a Level Zero device by its UUID. */
        class SUSHIRUNTIME_API LevelZeroUUIDDeviceKeySource final : public UUIDDeviceKeySource
        {
            public:
                LevelZeroUUIDDeviceKeySource() noexcept;
        };
    } // namespace Internal::Topology
} // namespace SushiRuntime
