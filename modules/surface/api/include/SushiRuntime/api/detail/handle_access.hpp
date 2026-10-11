/**************************************************************************/
/* handle_access.hpp                                                      */
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
 * @file handle_access.hpp
 * @brief Declares HandleAccess, the graph layer's access to a data handle's tracking state.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <memory>
#include <utility>

#include <SushiRuntime/region/liveness.hpp>

namespace SushiRuntime
{
    template <typename T>
    class Buffer;

    template <typename T>
    class State;

    namespace Detail
    {
        template <typename T>
        struct StateView;

        /**
         * @brief Reads and steps the tracking state of a Buffer or a State for the graph
         *        layer.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        struct HandleAccess
        {
            /**
             * @brief Constructs a Buffer or a State through one of its private constructors.
             * @tparam Handle Buffer<T> or State<T>.
             * @pre A null context is passed only for a handle on which no range call is made.
             */
            template <typename Handle, typename... Arguments>
            static Handle construct(Arguments&&... arguments)
            {
                return Handle(std::forward<Arguments>(arguments)...);
            }

            /**
             * @brief Returns the address the dependency tracker keys a Buffer on.
             * @return The allocation's base address, or nullptr for an empty handle.
             */
            template <typename T>
            static void* key(const Buffer<T>& buffer) noexcept
            {
                return static_cast<void*>(buffer.data());
            }

            /** @brief Returns the key of the field a task reading the State depends on. */
            template <typename T>
            static void* read_key(const State<T>& state) noexcept
            {
                return key(state.current_);
            }

            /** @brief Returns the key of the field a task writing the State depends on. */
            template <typename T>
            static void* write_key(const State<T>& state) noexcept
            {
                return key(state.next_);
            }

            /**
             * @brief Returns an observer that expires when the Buffer is destroyed or assigned
             *        over by move assignment.
             * @return An empty observer for a handle that owns no allocation.
             */
            template <typename T>
            static LivenessObserver liveness(const Buffer<T>& buffer) noexcept
            {
                return buffer.liveness_;
            }

            /** @brief Returns the observer of the State's current field. */
            template <typename T>
            static LivenessObserver liveness(const State<T>& state) noexcept
            {
                return liveness(state.current_);
            }

            /**
             * @brief Returns the view a State shares with the graphs that record it.
             * @return The State's own view, or a new empty one for a State that owns no memory.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            template <typename T>
            static std::shared_ptr<StateView<T>> view(const State<T>& state)
            {
                if (state.view_)
                    return state.view_;
                return std::make_shared<StateView<T>>();
            }

            /**
             * @brief Swaps the State's two fields, so the one just written becomes current,
             *        and writes the swap into its view.
             */
            template <typename T>
            static void advance(State<T>& state) noexcept
            {
                using std::swap;
                swap(state.current_, state.next_);
                if (state.view_)
                    state.publish_view();
            }

            /** @brief Advances the State that owns a view; does nothing once it is gone. */
            template <typename T>
            static void advance(StateView<T>& view) noexcept
            {
                if (view.owner != nullptr)
                    advance(*view.owner);
            }

            /** @brief Rewrites a view from the State that owns it; does nothing once gone. */
            template <typename T>
            static void resync(StateView<T>& view) noexcept
            {
                if (view.owner != nullptr)
                    view.owner->publish_view();
            }
        };
    } // namespace Detail
} // namespace SushiRuntime
