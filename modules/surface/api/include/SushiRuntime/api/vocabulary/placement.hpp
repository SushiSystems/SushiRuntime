/**************************************************************************/
/* placement.hpp                                                          */
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
 * @file placement.hpp
 * @brief Declares On, the device an untracked kernel is placed on.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
 */

#pragma once

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/memory/resource_liveness.hpp>

namespace SushiRuntime
{
    /**
     * @brief States the device a kernel that names no region runs on.
     * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
     */
    struct On
    {
        DeviceIndex device;          /**< Device the kernel runs on. */
        Internal::Memory::LivenessObserver life; /**< Observer of the handle named, or empty. */

        /**
         * @brief Places the kernel on a device by index.
         * @param index Position in the runtime's device list.
         */
        explicit On(DeviceIndex index) noexcept : device(index)
        {

        }

        /**
         * @brief Places the kernel on the device that owns a buffer, and watches the buffer.
         * @param buffer Handle whose device is taken. Alive when the graph runs;
         *          free to move after add().
         */
        template <typename T>
        explicit On(const Buffer<T>& buffer) noexcept
            : device(buffer.device_index()), life(detail::HandleAccess::liveness(buffer))
        {

        }

        /**
         * @brief Places the kernel on the device that owns a State, and watches the State.
         * @param state Handle whose device is taken. Alive when the graph runs;
         *          free to move after add().
         */
        template <typename T>
        explicit On(const State<T>& state) noexcept
            : device(state.device_index()), life(detail::HandleAccess::liveness(state))
        {

        }
    };



} // namespace SushiRuntime
