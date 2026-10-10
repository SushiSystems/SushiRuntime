/**************************************************************************/
/* late_count.hpp                                                         */
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
 * @file late_count.hpp
 * @brief Declares LateCount, a run count resolved from one live source.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include <SushiRuntime/api/data/buffer.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Holds a run count resolved once per run from exactly one live source.
         * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
         */
        struct LateCount
        {
            std::size_t capacity = 0; /**< Plan size; the live count must not exceed it. */

            /** @brief Polled once per run when set; overrides capacity as the count. */
            std::function<std::size_t()> host;

            /**
             * @brief Residency::Host slot a predecessor wrote the live count into.
             *
             * Borrowed, not owned: this LateCount only observes it, so the buffer it
             * points to must outlive every graph this LateCount is passed to.
             */
            const Buffer<std::uint32_t>* device = nullptr;

            /** @brief Element index of the live count within @c device. */
            std::size_t device_index = 0;
        };

    } // namespace API
} // namespace SushiRuntime
