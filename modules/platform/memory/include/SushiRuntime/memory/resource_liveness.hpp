/**************************************************************************/
/* resource_liveness.hpp                                                  */
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
 * @file resource_liveness.hpp
 * @brief Declares the liveness token a data handle owns and a graph weakly observes.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <memory>

namespace SushiRuntime
{
    namespace Memory
    {
        /**
         * @brief An empty object whose lifetime mirrors a data handle's allocation.
         *
         * Owned through a shared_ptr by the handle and observed through a weak_ptr by
         * any graph that recorded the handle. It holds no data because only its
         * liveness matters; a non-expired weak observer means the allocation behind
         * the recorded pointer is still valid.
         */
        struct Liveness
        {
        };

        /** @brief The owning side of a liveness token, held by a data handle. */
        using LivenessToken = std::shared_ptr<Liveness>;

        /** @brief The observing side of a liveness token, held by a graph. */
        using LivenessObserver = std::weak_ptr<Liveness>;

        /** @brief Creates a fresh live token for a newly allocated handle. */
        inline LivenessToken make_liveness()
        {
            return std::make_shared<Liveness>();
        }

    } // namespace Memory

    namespace Core
    {
        /** @brief Names Memory::Liveness by its spelling before v0.4.0. */
        using Liveness [[deprecated("use Memory::Liveness")]] = Memory::Liveness;

        /** @brief Names Memory::LivenessToken by its spelling before v0.4.0. */
        using LivenessToken [[deprecated("use Memory::LivenessToken")]] = Memory::LivenessToken;

        /** @brief Names Memory::LivenessObserver by its spelling before v0.4.0. */
        using LivenessObserver [[deprecated("use Memory::LivenessObserver")]] =
            Memory::LivenessObserver;

        /** @brief Forwards to Memory::make_liveness(). */
        [[deprecated("use Memory::make_liveness")]]
        inline Memory::LivenessToken make_liveness()
        {
            return Memory::make_liveness();
        }

    } // namespace Core
} // namespace SushiRuntime
