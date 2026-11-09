/**************************************************************************/
/* profile.hpp                                                            */
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
 * @file profile.hpp
 * @brief Declares the SR_ZONE family, the runtime's front over SushiProfiler.
 * @author Mustafa Garip
 * @see modules/foundation/profile/README.md
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <SushiRuntime/profile/profile_gate.hpp>

#if SR_ENABLE_PROFILER

#include <SushiProfiler/collector/collector.hpp>
#include <SushiProfiler/zones/records.hpp>
#include <SushiProfiler/zones/zone.hpp>

#include <SushiRuntime/profile/collector_lock.hpp>

/** @brief The group every runtime zone is charged to. */
#define SR_PROFILE_GROUP "Runtime"

/** @brief Pastes two tokens after expanding them. */
#define SR_PROFILE_JOIN(left, right) SR_PROFILE_JOIN_INNER(left, right)

/** @brief Pastes two tokens. */
#define SR_PROFILE_JOIN_INNER(left, right) left##right

/** @brief Declares the site and handle of record @p id; the handle is empty when @p compiled_in is false. */
#define SR_PROFILE_SITE(id, compiled_in, name, group, level, role, op, unit) \
    static constexpr ::SushiProfiler::Site SR_PROFILE_JOIN(sr_site_, id){ \
        name, group, __FILE__, __LINE__, level, ::SushiProfiler::SiteRole::role, \
        ::SushiProfiler::CounterOp::op, unit}; \
    static ::SushiProfiler::SiteSlot<compiled_in> SR_PROFILE_JOIN(sr_handle_, id) \
    { \
        &SR_PROFILE_JOIN(sr_site_, id) \
    }

/** @brief Declares zone @p id of @p level into @p collector, compiled in when @p level is at most the compiled level. */
#define SR_PROFILE_ZONE(id, collector, level, name) \
    SR_PROFILE_SITE(id, ((level) <= ::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL), name, \
                    SR_PROFILE_GROUP, level, Zone, Add, nullptr); \
    [[maybe_unused]] const ::SushiProfiler::ScopedZone< \
        ((level) <= ::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL)> \
        SR_PROFILE_JOIN(sr_zone_, id)(collector, SR_PROFILE_JOIN(sr_handle_, id))

/** @brief Declares a level 1 value or counter site @p id and records @p value into @p collector through @p record. */
#define SR_PROFILE_SAMPLE(id, collector, role, op, record, name, unit, value) \
    do \
    { \
        if constexpr (1 <= ::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL) \
        { \
            SR_PROFILE_SITE(id, true, name, nullptr, 1, role, op, unit); \
            ::SushiProfiler::record((collector), SR_PROFILE_JOIN(sr_handle_, id), value); \
        } \
    } while (false)

/** @brief Records the rest of the scope as a stage zone, level 1, into @p collector. */
#define SR_ZONE(collector, name) SR_PROFILE_ZONE(__COUNTER__, collector, 1, name)

/** @brief Records the rest of the scope as a fine zone, level 2, into @p collector. */
#define SR_ZONE_FINE(collector, name) SR_PROFILE_ZONE(__COUNTER__, collector, 2, name)

/** @brief Records the rest of the scope as a per-element zone, level 3, into @p collector. */
#define SR_ZONE_DETAIL(collector, name) SR_PROFILE_ZONE(__COUNTER__, collector, 3, name)

/** @brief Records @p value as a sample of @p unit, level 1, into @p collector. */
#define SR_VALUE(collector, name, unit, value) \
    SR_PROFILE_SAMPLE(__COUNTER__, collector, Value, Add, record_value, name, unit, value)

/** @brief Records @p value at a counter folded per period by CounterOp @p op, level 1, into @p collector. */
#define SR_COUNTER(collector, name, unit, op, value) \
    SR_PROFILE_SAMPLE(__COUNTER__, collector, Counter, op, record_counter, name, unit, value)

/** @brief Declares a level 1 instant site @p id and records an occurrence now into @p collector. */
#define SR_PROFILE_INSTANT(id, collector, name) \
    do \
    { \
        if constexpr (1 <= ::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL) \
        { \
            SR_PROFILE_SITE(id, true, name, nullptr, 1, Instant, Add, nullptr); \
            ::SushiProfiler::record_instant((collector), SR_PROFILE_JOIN(sr_handle_, id)); \
        } \
    } while (false)

/** @brief Records an occurrence now, level 1, into @p collector. */
#define SR_INSTANT(collector, name) SR_PROFILE_INSTANT(__COUNTER__, collector, name)

/** @brief Declares lock wait site @p id and locks @p mutex for the rest of the scope, recording a contended wait into @p collector. */
#define SR_PROFILE_LOCK_WAIT(id, collector, name, mutex) \
    SR_PROFILE_SITE(id, (1 <= ::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL), name, "Lock wait", 1, \
                    LockWait, Add, nullptr); \
    const ::SushiRuntime::Internal::Profile::CollectorLock< \
        std::remove_reference_t<decltype(mutex)>, (1 <= ::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL)> \
        SR_PROFILE_JOIN(sr_lock_, id)((collector), (mutex), SR_PROFILE_JOIN(sr_handle_, id))

/** @brief Locks @p mutex for the rest of the scope and records its wait into @p collector when it was contended. */
#define SR_LOCK_WAIT(collector, name, mutex) SR_PROFILE_LOCK_WAIT(__COUNTER__, collector, name, mutex)

namespace SushiRuntime
{
    namespace Internal::Profile
    {
        /** @brief The collector the runtime records into. */
        using Collector = ::SushiProfiler::Collector;

        /** @brief Returns the process-wide collector, the one a runtime records into unless it is given another. */
        [[nodiscard]] inline Collector& process_collector()
        {
            return Collector::instance();
        }

        /**
         * @brief Returns the profiler's index of the calling thread, or 0 when it has none.
         *
         * A thread has an index once it has recorded while a session was open or was registered.
         */
        [[nodiscard]] inline std::uint32_t current_thread_index() noexcept
        {
            const std::uint32_t index = ::SushiProfiler::Collector::thread_index();
            return index == ::SushiProfiler::Collector::K_NO_THREAD ? 0U : index;
        }

        /** @brief Registers the calling thread with the profiler and names it, so its first zone allocates nothing. */
        inline void name_this_thread(std::string_view name)
        {
            static_cast<void>(::SushiProfiler::Collector::register_this_thread());
            ::SushiProfiler::Collector::set_thread_name(name);
        }
    } // namespace Internal::Profile
} // namespace SushiRuntime

#else // SR_ENABLE_PROFILER

#define SR_PROFILE_GROUP "Runtime"
/** @brief Evaluates nothing: the unevaluated operands keep a collector or a value only a record reads from being reported unused. */
#define SR_ZONE(collector, name) static_cast<void>(sizeof(collector))
#define SR_ZONE_FINE(collector, name) static_cast<void>(sizeof(collector))
#define SR_ZONE_DETAIL(collector, name) static_cast<void>(sizeof(collector))
#define SR_VALUE(collector, name, unit, value) static_cast<void>(sizeof(collector) + sizeof(value))
#define SR_COUNTER(collector, name, unit, op, value) static_cast<void>(sizeof(collector) + sizeof(value))
#define SR_INSTANT(collector, name) static_cast<void>(sizeof(collector))

/** @brief Pastes two tokens after expanding them. */
#define SR_PROFILE_JOIN(left, right) SR_PROFILE_JOIN_INNER(left, right)

/** @brief Pastes two tokens. */
#define SR_PROFILE_JOIN_INNER(left, right) left##right

/**
 * @brief Locks @p mutex for the rest of the scope; with no profiler the lock is a plain one and @p collector is not read.
 *
 * The file that uses it includes <mutex>; this header does not.
 */
#define SR_LOCK_WAIT(collector, name, mutex) \
    static_cast<void>(sizeof(collector)); \
    const std::lock_guard<std::remove_reference_t<decltype(mutex)>> \
        SR_PROFILE_JOIN(sr_lock_wait_guard_, __LINE__)(mutex)

namespace SushiRuntime
{
    namespace Internal::Profile
    {
        /** @brief Stands in for the collector: without the profiler nothing records. */
        struct Collector
        {
        };

        /** @brief Returns the one stand-in collector. */
        [[nodiscard]] inline Collector& process_collector() noexcept
        {
            static Collector collector;
            return collector;
        }

        /** @brief Returns 0: without the profiler no thread has an index. */
        [[nodiscard]] inline std::uint32_t current_thread_index() noexcept
        {
            return 0U;
        }

        /** @brief Does nothing: without the profiler no thread is registered. */
        inline void name_this_thread(std::string_view) noexcept
        {
        }
    } // namespace Internal::Profile
} // namespace SushiRuntime

#endif // SR_ENABLE_PROFILER
