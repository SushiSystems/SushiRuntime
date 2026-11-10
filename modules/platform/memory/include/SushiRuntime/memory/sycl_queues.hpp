/**************************************************************************/
/* sycl_queues.hpp                                                       */
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
 * @file sycl_queues.hpp
 * @brief Declares make_sycl_queues(), which wraps SYCL device queues for make_sycl_usm_source().
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/sycl_usm_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Wraps SYCL queues, one device per queue, so a header that names no SYCL type can
         *        pass them on.
         * @param device_queues A device's index is its position.
         * @see modules/platform/memory/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API SYCLQueues
        make_sycl_queues(std::vector<sycl::queue> device_queues);
    } // namespace Internal::Memory
} // namespace SushiRuntime
