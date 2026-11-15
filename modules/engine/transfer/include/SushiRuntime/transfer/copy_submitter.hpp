/**************************************************************************/
/* copy_submitter.hpp                                                     */
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
 * @file copy_submitter.hpp
 * @brief Declares CopySubmitter, the port that submits one copy on a queue.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <vector>

#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Submits one copy on a queue and returns its event without waiting for it. */
        class CopySubmitter
        {
            public:
                virtual ~CopySubmitter() = default;

                /**
                 * @brief Submits a copy of bytes from from to to on queue, after dependencies.
                 *
                 * May throw what the SYCL call throws; the caller translates it.
                 * @param dependencies Events of any context the copy waits for on the device side.
                 */
                [[nodiscard]] virtual sycl::event submit(sycl::queue& queue, void* to, const void* from,
                                                         std::size_t bytes,
                                                         const std::vector<sycl::event>& dependencies) = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
