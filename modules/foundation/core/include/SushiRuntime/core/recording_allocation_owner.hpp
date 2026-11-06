/**************************************************************************/
/* recording_allocation_owner.hpp                                         */
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
 * @file recording_allocation_owner.hpp
 * @brief Declares RecordingAllocationOwner, which appends each address it releases to a vector.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <vector>

#include <SushiRuntime/core/allocation.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Appends each address it is asked to release to a vector, in call order.
         *
         * The vector is not owned and must outlive every owner built on it.
         * The owner frees nothing; the addresses it records are compared, never dereferenced.
         * @see modules/foundation/core/README.md
         */
        class RecordingAllocationOwner final : public AllocationOwner
        {
            public:
                /**
                 * @brief Builds an owner that records into @p releases.
                 * @param releases Receives each released address; outlives the owner.
                 */
                explicit RecordingAllocationOwner(std::vector<void*>& releases) noexcept
                    : releases_(releases)
                {

                }

                /**
                 * @brief Records @p address as released.
                 * @param address The allocation being released.
                 */
                void release(void* address) noexcept override
                {
                    releases_.push_back(address);
                }

            private:
                /** @brief Receives each released address. */
                std::vector<void*>& releases_;
        };
    } // namespace Detail
} // namespace SushiRuntime
