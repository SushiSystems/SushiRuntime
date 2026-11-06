/**************************************************************************/
/* device_descriptor.hpp                                                  */
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
 * @file device_descriptor.hpp
 * @brief Declares DeviceDescriptor, a SYCL device with its inventory record.
 * @author Mustafa Garip
 */

#pragma once

#include <sycl/sycl.hpp>

#include <SushiRuntime/machine/device_description.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        /**
         * @brief Pairs a SYCL device with its inventory record.
         */
        struct DeviceDescriptor
        {
            sycl::device device; /**< The SYCL device a queue is built on. */
            DeviceDescription info;     /**< What the inventory reports about it. */
        };
    } // namespace Internal::Device
} // namespace SushiRuntime
