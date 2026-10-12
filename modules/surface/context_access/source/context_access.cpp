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

#include <SushiRuntime/context/context.hpp>
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/context/context_handle_access.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Internal::ContextAccess
    {
        namespace
        {
            /** @brief Returns the engine's context an opaque context names; null for null. */
            [[nodiscard]] Execution::RuntimeContext*
            engine_context(Detail::Context* context) noexcept
            {
                return reinterpret_cast<Execution::RuntimeContext*>(context);
            }

            /** @brief Returns the surface's name for a context of the engine; null for null. */
            [[nodiscard]] Detail::Context*
            surface_context(Execution::RuntimeContext* context) noexcept
            {
                return reinterpret_cast<Detail::Context*>(context);
            }
        } // namespace

        Execution::RuntimeContext& context(const ContextHandle& handle) noexcept
        {
            return *engine_context(Detail::ContextHandleAccess::share(handle).get());
        }

        std::shared_ptr<Execution::RuntimeContext> shared(const ContextHandle& handle) noexcept
        {
            const std::shared_ptr<Detail::Context>& share =
                Detail::ContextHandleAccess::share(handle);
            return std::shared_ptr<Execution::RuntimeContext>(share, engine_context(share.get()));
        }

        ContextHandle handle(std::shared_ptr<Execution::RuntimeContext> context) noexcept
        {
            return Detail::ContextHandleAccess::wrap(
                std::shared_ptr<Detail::Context>(context, surface_context(context.get())));
        }
    } // namespace Internal::ContextAccess
} // namespace SushiRuntime
