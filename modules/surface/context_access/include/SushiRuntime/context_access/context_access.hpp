/**************************************************************************/
/* context_access.hpp                                                     */
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
 * @file context_access.hpp
 * @brief Declares the way from a ContextHandle to the engine's context and back.
 * @author Mustafa Garip
 * @see modules/surface/context_access/README.md
 */

#pragma once

#include <memory>

#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/context/context_handle_access.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Internal::MemoryExchange
    {
        /// @brief Names the per-runtime cache of device capabilities, in capability_cache.hpp.
        class CapabilityCache;
    } // namespace Internal::MemoryExchange

    namespace Internal::ContextAccess
    {
        /**
         * @brief Returns the context a handle shares.
         * @pre handle is not empty.
         */
        [[nodiscard]] SUSHIRUNTIME_API Execution::RuntimeContext&
        context(const ContextHandle& handle) noexcept;

        /** @brief Returns a share of a handle's context; null for an empty handle. */
        [[nodiscard]] SUSHIRUNTIME_API std::shared_ptr<Execution::RuntimeContext>
        shared(const ContextHandle& handle) noexcept;

        /**
         * @brief Returns the cache of device capabilities the handle's runtime owns.
         * @pre handle is not empty.
         */
        [[nodiscard]] SUSHIRUNTIME_API MemoryExchange::CapabilityCache&
        capability_cache(const ContextHandle& handle) noexcept;

        /**
         * @brief Returns the handle that shares a context the library built.
         * @param context The context to share; null makes an empty handle.
         * @throws std::bad_alloc when the surface's share of the context cannot be built.
         */
        [[nodiscard]] SUSHIRUNTIME_API ContextHandle
        handle(std::shared_ptr<Execution::RuntimeContext> context);

        /**
         * @brief Returns the context an owner of a handle shares.
         * @tparam Owner A type that befriends Detail::ContextHandleAccess, such as Runtime.
         * @pre owner holds a handle that is not empty.
         */
        template <class Owner>
        [[nodiscard]] Execution::RuntimeContext& context(const Owner& owner) noexcept
        {
            return context(Detail::ContextHandleAccess::handle_of(owner));
        }

        /**
         * @brief Returns a share of the context an owner of a handle shares.
         * @tparam Owner A type that befriends Detail::ContextHandleAccess, such as Runtime.
         */
        template <class Owner>
        [[nodiscard]] std::shared_ptr<Execution::RuntimeContext>
        shared(const Owner& owner) noexcept
        {
            return shared(Detail::ContextHandleAccess::handle_of(owner));
        }

        /**
         * @brief Returns the cache of device capabilities an owner of a handle shares.
         * @tparam Owner A type that befriends Detail::ContextHandleAccess, such as Runtime.
         * @pre owner holds a handle that is not empty.
         */
        template <class Owner>
        [[nodiscard]] MemoryExchange::CapabilityCache& capability_cache(const Owner& owner) noexcept
        {
            return capability_cache(Detail::ContextHandleAccess::handle_of(owner));
        }
    } // namespace Internal::ContextAccess
} // namespace SushiRuntime
