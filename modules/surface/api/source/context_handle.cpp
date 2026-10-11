/**************************************************************************/
/* context_handle.cpp                                                     */
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
 * @file context_handle.cpp
 * @brief Implements ContextAccess, which returns the context a surface handle shares.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#include <SushiRuntime/execution/context_access.hpp>

#include <memory>

#include <SushiRuntime/api/runtime/context_handle.hpp>
#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        Internal::Execution::RuntimeContext&
        ContextAccess::context(const ContextHandle& handle) noexcept
        {
            return *handle.context_;
        }

        const std::shared_ptr<Internal::Execution::RuntimeContext>&
        ContextAccess::shared(const ContextHandle& handle) noexcept
        {
            return handle.context_;
        }

        Internal::Execution::RuntimeContext&
        ContextAccess::context(const Runtime& runtime) noexcept
        {
            return context(runtime.context_);
        }
    } // namespace Detail
} // namespace SushiRuntime
