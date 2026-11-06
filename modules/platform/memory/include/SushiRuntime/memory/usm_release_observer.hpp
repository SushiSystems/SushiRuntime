/**************************************************************************/
/* usm_release_observer.hpp                                               */
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
 * @file usm_release_observer.hpp
 * @brief Declares USMReleaseObserver, which USMAllocator tells about every release.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Receives the address of every allocation USMAllocator::release() is given.
         * @see modules/platform/memory/README.md
         */
        class USMReleaseObserver
        {
            public:
                /**
                 * @brief Hears that an address is about to be released.
                 * @param address Never null. The memory is still allocated during the call and
                 *        may be freed as soon as it returns. Any thread may call, several at once.
                 */
                virtual void on_release(void* address) noexcept = 0;

            protected:
                /** @brief Keeps an observer from being destroyed through this interface. */
                ~USMReleaseObserver() = default;
        };

    } // namespace Memory
} // namespace SushiRuntime
