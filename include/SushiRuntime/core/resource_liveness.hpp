/****************************************************************/
/* resource_liveness.hpp                                        */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

/**
 * @file resource_liveness.hpp
 * @brief A liveness token a data handle owns and a graph weakly observes.
 *
 * A fluent graph records the base pointers of the Buffers and States its tasks
 * touch, but it does not own them — if a handle is destroyed before the graph runs,
 * those pointers dangle and a run is silent undefined behaviour. A handle holds one
 * shared LivenessToken; the graph keeps a weak observer of it per recorded
 * resource. When the handle dies the token expires, so the graph can detect a stale
 * resource at run time and fail cleanly instead of dispatching against freed memory.
 * The token carries no state: its existence is the signal, so observing it costs a
 * weak_ptr lock and never touches the dependency hot path.
 */

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
