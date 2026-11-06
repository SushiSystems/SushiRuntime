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
 * @file context_handle.hpp
 * @brief Declares ContextHandle, the opaque share of a runtime every handle holds.
 * @author Mustafa Garip
 * @see modules/surface/context/README.md
 */

#pragma once

#include <memory>
#include <utility>

#include <SushiRuntime/context/context.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        struct ContextHandleAccess;
    } // namespace Detail

    /**
     * @brief Shares ownership of one runtime's context without naming what it holds.
     *
     * A Runtime and every graph, buffer and view it hands out hold one; the context is
     * destroyed with the last of them. A copy shares the same context.
     * @see modules/surface/context/README.md
     */
    class ContextHandle final
    {
        public:
            /** @brief Holds no context. */
            ContextHandle() noexcept = default;

            /** @brief Returns whether this handle shares a context. */
            [[nodiscard]] explicit operator bool() const noexcept { return context_ != nullptr; }

        private:
            // The library's sources build a handle and read it back through this one seam.
            friend struct Detail::ContextHandleAccess;

            /**
             * @brief Shares a context the library built.
             * @param context The context to share; null makes an empty handle.
             */
            explicit ContextHandle(std::shared_ptr<Detail::Context> context) noexcept
                : context_(std::move(context))
            {
            }

            /** @brief The shared context; null for an empty handle. */
            std::shared_ptr<Detail::Context> context_;
    };
} // namespace SushiRuntime
