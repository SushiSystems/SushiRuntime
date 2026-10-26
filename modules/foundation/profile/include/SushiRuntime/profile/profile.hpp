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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
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
#include <SushiProfiler/lock_wait/contended_lock.hpp>
#include <SushiProfiler/zones/macros.hpp>

/** @brief The group every runtime zone is charged to. */
#define SR_PROFILE_GROUP "Runtime"

/** @brief Records the rest of the scope as a stage zone, level 1. */
#define SR_ZONE(name) \
    SP_ZONE_AT(::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL, 1, name, SR_PROFILE_GROUP)

/** @brief Records the rest of the scope as a fine zone, level 2. */
#define SR_ZONE_FINE(name) \
    SP_ZONE_AT(::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL, 2, name, SR_PROFILE_GROUP)

/** @brief Records the rest of the scope as a per-element zone, level 3. */
#define SR_ZONE_DETAIL(name) \
    SP_ZONE_AT(::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL, 3, name, SR_PROFILE_GROUP)

/** @brief Records @p value as a sample of @p unit, level 1. */
#define SR_VALUE(name, unit, value) \
    SP_VALUE_AT(::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL, 1, name, unit, value)

/** @brief Records @p value at a counter folded per period by CounterOp @p op, level 1. */
#define SR_COUNTER(name, unit, op, value) \
    SP_COUNTER_AT(::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL, 1, name, unit, op, value)

/** @brief Records an occurrence now, level 1. */
#define SR_INSTANT(name) \
    SP_INSTANT_AT(::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL, 1, name)

/** @brief Locks @p mutex for the rest of the scope and records its wait when it was contended. */
#define SR_LOCK_WAIT(name, mutex) \
    SP_LOCK_WAIT_AT(::SushiRuntime::Internal::Profile::K_COMPILED_LEVEL, name, mutex)

namespace SushiRuntime
{
    namespace Internal::Profile
    {
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
#define SR_ZONE(name) static_cast<void>(0)
#define SR_ZONE_FINE(name) static_cast<void>(0)
#define SR_ZONE_DETAIL(name) static_cast<void>(0)
/** @brief Evaluates nothing: the unevaluated operand keeps a variable only a value reads from being reported unused. */
#define SR_VALUE(name, unit, value) static_cast<void>(sizeof(value))
#define SR_COUNTER(name, unit, op, value) static_cast<void>(sizeof(value))
#define SR_INSTANT(name) static_cast<void>(0)

/** @brief Pastes two tokens after expanding them. */
#define SR_PROFILE_JOIN(left, right) SR_PROFILE_JOIN_INNER(left, right)

/** @brief Pastes two tokens. */
#define SR_PROFILE_JOIN_INNER(left, right) left##right

/**
 * @brief Locks @p mutex for the rest of the scope; with no profiler the lock is a plain one.
 *
 * The file that uses it includes <mutex>; this header does not.
 */
#define SR_LOCK_WAIT(name, mutex) \
    const std::lock_guard<std::remove_reference_t<decltype(mutex)>> \
        SR_PROFILE_JOIN(sr_lock_wait_guard_, __LINE__)(mutex)

namespace SushiRuntime
{
    namespace Internal::Profile
    {
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
