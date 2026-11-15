/**************************************************************************/
/* retire_source.hpp                                                      */
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
 * @file retire_source.hpp
 * @brief Declares RetireSource, the port that tells one observer about every retired handle.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <SushiRuntime/memory/retire_observer.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Holds the one RetireObserver that hears each handle retired.
         */
        class RetireSource
        {
            public:
                /**
                 * @brief Installs the observer retire() tells and waits out the calls in flight.
                 * @param observer Null removes it. It must stay alive until a later call that
                 *        replaces or removes it has returned.
                 * @pre The caller is not inside on_retire() of an observer of this source.
                 */
                virtual void set_retire_observer(RetireObserver* observer) noexcept = 0;

            protected:
                /** @brief Keeps a source from being destroyed through this interface. */
                ~RetireSource() = default;
        };

    } // namespace Internal::Memory
} // namespace SushiRuntime
