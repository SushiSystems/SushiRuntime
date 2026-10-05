/****************************************************************/
/* resource_liveness.hpp                                        */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file resource_liveness.hpp
 * @brief Declares the liveness token a data handle owns and a graph weakly observes.
 * @author Mustafa Garip
 * @see include/SushiRuntime/core/README.md
 */

#pragma once

#include <memory>

namespace SushiRuntime
{
    namespace Core
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

    } // namespace Core
} // namespace SushiRuntime
