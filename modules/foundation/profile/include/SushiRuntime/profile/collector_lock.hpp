/**************************************************************************/
/* collector_lock.hpp                                                     */
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
 * @file collector_lock.hpp
 * @brief Declares CollectorLock, the scope guard behind SR_LOCK_WAIT.
 * @author Mustafa Garip
 * @see modules/foundation/profile/README.md
 */

#pragma once

#include <SushiProfiler/collector/collector.hpp>
#include <SushiProfiler/sites/site.hpp>
#include <SushiProfiler/zones/zone.hpp>

namespace SushiRuntime
{
    namespace Internal::Profile
    {
        /**
         * @brief Holds a mutex for its scope and records a zone into a collector only when it had to wait.
         * @tparam Mutex A type with try_lock, lock and unlock.
         * @tparam Compiled False compiles the wait zone out and leaves a plain lock.
         *
         * An uncontended lock costs one try_lock and writes nothing.
         */
        template <class Mutex, bool Compiled>
        class CollectorLock final
        {
            public:
                /** @brief Locks @p mutex, recording the wait at @p handle's site of @p collector when it is contended. */
                CollectorLock(::SushiProfiler::Collector& collector, Mutex& mutex,
                              ::SushiProfiler::SiteSlot<Compiled>& handle)
                    : mutex_(mutex)
                {
                    if (!mutex_.try_lock())
                    {
                        [[maybe_unused]] const ::SushiProfiler::ScopedZone<Compiled> wait(collector, handle);
                        mutex_.lock();
                    }
                }

                CollectorLock(const CollectorLock&) = delete;
                CollectorLock& operator=(const CollectorLock&) = delete;
                CollectorLock(CollectorLock&&) = delete;
                CollectorLock& operator=(CollectorLock&&) = delete;

                /** @brief Unlocks the mutex. */
                ~CollectorLock()
                {
                    mutex_.unlock();
                }

            private:
                Mutex& mutex_; /**< The guarded mutex. */
        };
    } // namespace Internal::Profile
} // namespace SushiRuntime
