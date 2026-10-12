/**************************************************************************/
/* context_handle.hpp                                                     */
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
 * @file context_handle.hpp
 * @brief Declares ContextHandle, the opaque share of a runtime every handle holds.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#pragma once

#include <memory>
#include <utility>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        class RuntimeContext;
    } // namespace Internal::Execution

    namespace Detail
    {
        struct ContextAccess;
    } // namespace Detail

    class Runtime;

    /**
     * @brief Shares ownership of one runtime's context without naming what it holds.
     *
     * A Runtime and every graph, buffer and view it hands out hold one; the context is
     * destroyed with the last of them. A copy shares the same context.
     * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
     */
    class ContextHandle final
    {
        public:
            /** @brief Holds no context. */
            ContextHandle() noexcept = default;

            /** @brief Returns whether this handle shares a context. */
            [[nodiscard]] explicit operator bool() const noexcept { return context_ != nullptr; }

        private:
            // Runtime::create() builds the context; the library's sources read it back.
            friend class Runtime;
            friend struct Detail::ContextAccess;

            /**
             * @brief Shares a context the library built.
             * @param context The context to share; null makes an empty handle.
             */
            explicit ContextHandle(
                std::shared_ptr<Internal::Execution::RuntimeContext> context) noexcept
                : context_(std::move(context))
            {
            }

            /** @brief The shared context; null for an empty handle. */
            std::shared_ptr<Internal::Execution::RuntimeContext> context_;
    };
} // namespace SushiRuntime
