/**************************************************************************/
/* context_access.cpp                                                     */
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
 * @file context_access.cpp
 * @brief Implements the way from a ContextHandle to the engine's context and back.
 * @author Mustafa Garip
 * @see modules/surface/context_access/README.md
 */

#include <SushiRuntime/context_access/context_access.hpp>

#include <memory>
#include <utility>

#include <SushiRuntime/context/context.hpp>
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/context/context_handle_access.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory_exchange/capability_cache.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Holds what one runtime owns at the surface: its engine context and its caches.
         *
         * The cache is declared after the engine context, so it is destroyed first.
         */
        struct Context
        {
            /// @brief Holds the engine's context of the runtime.
            std::shared_ptr<Internal::Execution::RuntimeContext> runtime;
            /// @brief Holds each device's interop capabilities once asked.
            Internal::MemoryExchange::CapabilityCache capabilities;
        };
    } // namespace Detail

    namespace Internal::ContextAccess
    {
        Execution::RuntimeContext& context(const ContextHandle& handle) noexcept
        {
            return *Detail::ContextHandleAccess::share(handle)->runtime;
        }

        std::shared_ptr<Execution::RuntimeContext> shared(const ContextHandle& handle) noexcept
        {
            const std::shared_ptr<Detail::Context>& share =
                Detail::ContextHandleAccess::share(handle);
            if (share == nullptr)
                return nullptr;
            return std::shared_ptr<Execution::RuntimeContext>(share, share->runtime.get());
        }

        MemoryExchange::CapabilityCache& capability_cache(const ContextHandle& handle) noexcept
        {
            return Detail::ContextHandleAccess::share(handle)->capabilities;
        }

        ContextHandle handle(std::shared_ptr<Execution::RuntimeContext> context)
        {
            if (context == nullptr)
                return Detail::ContextHandleAccess::wrap(nullptr);
            auto surface = std::make_shared<Detail::Context>();
            surface->runtime = std::move(context);
            return Detail::ContextHandleAccess::wrap(std::move(surface));
        }
    } // namespace Internal::ContextAccess
} // namespace SushiRuntime
