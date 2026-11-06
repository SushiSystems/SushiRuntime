/**************************************************************************/
/* context_handle_access.hpp                                              */
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
 * @file context_handle_access.hpp
 * @brief Declares ContextHandleAccess, which builds a ContextHandle and reads one back.
 * @author Mustafa Garip
 * @see modules/surface/context/README.md
 */

#pragma once

#include <memory>
#include <utility>

#include <SushiRuntime/context/context.hpp>
#include <SushiRuntime/context/context_handle.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Builds a ContextHandle from the opaque context and reads one back, for the
         *        library's own sources.
         * @see modules/surface/context/README.md
         */
        struct ContextHandleAccess
        {
            /**
             * @brief Returns the handle that shares a context.
             * @param context The context to share; null makes an empty handle.
             */
            [[nodiscard]] static ContextHandle wrap(std::shared_ptr<Context> context) noexcept
            {
                return ContextHandle(std::move(context));
            }

            /** @brief Returns the share a handle holds; null for an empty handle. */
            [[nodiscard]] static const std::shared_ptr<Context>&
            share(const ContextHandle& handle) noexcept
            {
                return handle.context_;
            }

            /**
             * @brief Returns the handle an owner keeps in its context_ member.
             * @tparam Owner A type that befriends this struct, such as Runtime.
             */
            template <class Owner>
            [[nodiscard]] static const ContextHandle& handle_of(const Owner& owner) noexcept
            {
                return owner.context_;
            }
        };
    } // namespace Detail
} // namespace SushiRuntime
