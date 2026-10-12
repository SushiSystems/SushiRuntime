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
 * @file context_access.hpp
 * @brief Declares ContextAccess, which returns the context a surface handle shares.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <memory>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    class ContextHandle;
    class Runtime;

    namespace Internal::Execution
    {
        class RuntimeContext;
    } // namespace Internal::Execution

    namespace Detail
    {
        /**
         * @brief Returns the context behind a ContextHandle or a Runtime, for the library's
         *        own sources and its tests.
         * @see modules/engine/execution/README.md
         */
        struct ContextAccess
        {
            /**
             * @brief Returns the context a handle shares.
             * @pre handle is not empty.
             */
            [[nodiscard]] SUSHIRUNTIME_API static Internal::Execution::RuntimeContext&
            context(const ContextHandle& handle) noexcept;

            /** @brief Returns a share of a handle's context; null for an empty one. */
            [[nodiscard]] SUSHIRUNTIME_API static
            std::shared_ptr<Internal::Execution::RuntimeContext>
            shared(const ContextHandle& handle) noexcept;

            /**
             * @brief Returns the handle that shares a context the library built.
             * @param context The context to share; null makes an empty handle.
             */
            [[nodiscard]] SUSHIRUNTIME_API static ContextHandle
            handle(std::shared_ptr<Internal::Execution::RuntimeContext> context) noexcept;

            /**
             * @brief Returns the context a runtime shares with its handles.
             * @pre runtime was not moved from.
             */
            [[nodiscard]] SUSHIRUNTIME_API static Internal::Execution::RuntimeContext&
            context(const Runtime& runtime) noexcept;

            /**
             * @brief Returns a share of the context a runtime shares with its handles.
             * @pre runtime was not moved from.
             */
            [[nodiscard]] SUSHIRUNTIME_API static
            std::shared_ptr<Internal::Execution::RuntimeContext>
            shared(const Runtime& runtime) noexcept;
        };
    } // namespace Detail
} // namespace SushiRuntime
