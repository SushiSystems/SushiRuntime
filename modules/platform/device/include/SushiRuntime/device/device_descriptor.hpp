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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
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
