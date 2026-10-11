/**************************************************************************/
/* before_step_hooks.hpp                                                  */
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
 * @file before_step_hooks.hpp
 * @brief Declares BeforeStepHooks, the caller's hooks a graph runs before each step.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <SushiRuntime/core/delegate.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Owns the hooks a caller added to a graph and runs them in the order added.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        class BeforeStepHooks final
        {
            public:
                /** @brief Starts with no hook. */
                BeforeStepHooks() noexcept = default;

                /** @brief Takes over other's hooks and leaves it with none. */
                BeforeStepHooks(BeforeStepHooks&& other) noexcept
                    : hooks_(std::exchange(other.hooks_, {}))
                {
                }

                /** @brief Replaces these hooks with other's and leaves it with none. */
                BeforeStepHooks& operator=(BeforeStepHooks&& other) noexcept
                {
                    if (this != &other)
                        hooks_ = std::exchange(other.hooks_, {});
                    return *this;
                }

                BeforeStepHooks(const BeforeStepHooks&) = delete;
                BeforeStepHooks& operator=(const BeforeStepHooks&) = delete;
                ~BeforeStepHooks() = default;

                /**
                 * @brief Appends a hook; it runs after every hook added before it.
                 * @param hook Callable invoked as hook(std::size_t finished_steps).
                 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
                 */
                template <class Hook>
                void add(Hook&& hook)
                {
                    using Held = std::decay_t<Hook>;
                    if constexpr (fits_inline<Held>())
                        hooks_.emplace_back(std::forward<Hook>(hook));
                    else
                        hooks_.emplace_back(
                            [held = std::make_unique<Held>(std::forward<Hook>(hook))]
                            (std::size_t finished_steps)
                            {
                                (*held)(finished_steps);
                            });
                }

                /** @brief Returns whether no hook was added. */
                [[nodiscard]] bool empty() const noexcept { return hooks_.empty(); }

                /**
                 * @brief Calls every hook once, in the order they were added.
                 * @param finished_steps Steps of the run in progress replayed before this one.
                 */
                void run(std::size_t finished_steps) const
                {
                    for (const StoredHook& hook : hooks_)
                        hook(finished_steps);
                }

            private:
                /** @brief Bytes of a hook that Delegate stores with no allocation. */
                static constexpr std::size_t K_INLINE_BYTES = 64;

                /** @brief Stores one hook; a callable past K_INLINE_BYTES is held by pointer. */
                using StoredHook = Delegate<void(std::size_t), K_INLINE_BYTES>;

                /** @brief Tells whether Delegate takes a callable of type Held as it is. */
                template <class Held>
                static constexpr bool fits_inline() noexcept
                {
                    return sizeof(Held) <= K_INLINE_BYTES &&
                           alignof(Held) <= alignof(std::max_align_t) &&
                           std::is_nothrow_move_constructible<Held>::value;
                }

                /** @brief The hooks, in the order they were added. */
                std::vector<StoredHook> hooks_;
        };
    } // namespace Detail
} // namespace SushiRuntime
