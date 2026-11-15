/**************************************************************************/
/* retire_observer.hpp                                                    */
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
 * @file retire_observer.hpp
 * @brief Declares RetireObserver, which ResidencyDirectory tells about every handle it retires.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <SushiRuntime/handle/global_handle.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Receives each handle ResidencyDirectory::retire() retires.
         * @see modules/platform/memory/README.md
         */
        class RetireObserver
        {
            public:
                /**
                 * @brief Hears that a handle was retired.
                 * @param handle The retired handle; it no longer resolves. Called on the thread
                 *        that retired it, after the directory's lock is released, so the call
                 *        may write to the directory. Several threads may call at once.
                 */
                virtual void on_retire(GlobalHandle handle) noexcept = 0;

            protected:
                /** @brief Keeps an observer from being destroyed through this interface. */
                ~RetireObserver() = default;
        };

    } // namespace Internal::Memory
} // namespace SushiRuntime
