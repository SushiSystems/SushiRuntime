/**************************************************************************/
/* host_usm_source.hpp                                                    */
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
 * @file host_usm_source.hpp
 * @brief Declares make_host_usm_source(), which makes the in-memory USMSource of the host heap.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstddef>
#include <memory>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/usm_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Makes a source that allocates every kind of USM from the host heap.
         *
         * Every device shares one context, and owners resolve as on a device. It makes no device
         * and starts no thread.
         * @param device_count The devices the source stands for; 0 refuses every request.
         * @see modules/platform/memory/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API std::unique_ptr<USMSource>
        make_host_usm_source(std::size_t device_count = 1);
    } // namespace Internal::Memory
} // namespace SushiRuntime
