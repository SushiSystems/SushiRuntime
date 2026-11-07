/**************************************************************************/
/* sycl_usm_source.hpp                                                    */
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
 * @file sycl_usm_source.hpp
 * @brief Declares make_sycl_usm_source(), which makes the USMSource of a set of SYCL device queues.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <memory>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/usm_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Makes a source that takes USM from the queues, one device per queue.
         * @param device_queues A device's index is its position.
         * @see modules/platform/memory/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API std::unique_ptr<USMSource>
        make_sycl_usm_source(std::vector<sycl::queue> device_queues);
    } // namespace Internal::Memory
} // namespace SushiRuntime
