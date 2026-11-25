/**************************************************************************/
/* guarded_usm_source.hpp                                                 */
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
 * @file guarded_usm_source.hpp
 * @brief Declares make_guarded_usm_source(), a USMSource whose device memory the host cannot touch outside runtime work.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <memory>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/guarded_device_pages.hpp>
#include <SushiRuntime/memory/usm_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Wraps a source so its Device requests come from guarded pages and copy() runs with
         *        them open; every other call goes to the wrapped source.
         * @param inner Not null; the source the result owns and forwards to.
         * @param pages Not null; the pages Device requests come from, shared with whoever opens them.
         * @see modules/platform/memory/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API std::unique_ptr<USMSource>
        make_guarded_usm_source(std::unique_ptr<USMSource> inner, std::shared_ptr<GuardedDevicePages> pages);
    } // namespace Internal::Memory
} // namespace SushiRuntime
