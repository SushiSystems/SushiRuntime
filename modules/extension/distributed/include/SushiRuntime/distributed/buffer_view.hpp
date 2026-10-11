/**************************************************************************/
/* buffer_view.hpp                                                        */
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
 * @file buffer_view.hpp
 * @brief Declares BufferView, the address and the size a kernel receives for one buffer.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Names the memory of one buffer on the machine that runs a kernel.
         *
         * The view owns nothing: the memory belongs to the worker's cache, or to the master's
         * allocation when the kernel runs as a local fallback. A view a kernel receives is
         * valid until the event that kernel returns completes.
         */
        struct BufferView
        {
            /** @brief Holds the base address, a USM allocation; null when the size is 0. */
            void* data = nullptr;

            /** @brief Holds the number of bytes at the address. */
            std::size_t size_bytes = 0;

            /**
             * @brief Returns the address as a pointer to elements of @p Element.
             * @tparam Element The type the buffer was written as; size_bytes is not checked.
             */
            template <typename Element>
            [[nodiscard]] Element* as() const noexcept
            {
                return static_cast<Element*>(data);
            }
        };

    } // namespace Distributed
} // namespace SushiRuntime
