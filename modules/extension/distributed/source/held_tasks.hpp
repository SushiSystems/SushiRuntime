/**************************************************************************/
/* held_tasks.hpp                                                         */
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
 * @file held_tasks.hpp
 * @brief Declares HeldTasks, the task ids a worker holds until their results leave.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstdint>
#include <mutex>
#include <set>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Keeps the task ids the worker holds for one connection, for HEARTBEAT.
         *
         * An output stream may outlive the executor, so the two share this object. Its mutex
         * is a leaf: nothing is called while it is held.
         */
        class HeldTasks final
        {
            public:
                /**
                 * @brief Starts holding a task.
                 * @return The generation let_go() must be given for the same task.
                 */
                [[nodiscard]] std::uint64_t hold(std::uint64_t task_id);

                /** @brief Stops holding a task whose result left or cannot leave. */
                void let_go(std::uint64_t task_id, std::uint64_t generation) noexcept;

                /** @brief Forgets every task and the idle value; the connection is gone. */
                void reset() noexcept;

                /** @brief Returns the smallest held id, or the idle value when none is. */
                [[nodiscard]] std::uint64_t oldest() const;

            private:
                /// @brief Guards every member below.
                mutable std::mutex mutex_;

                /// @brief The ids held, smallest first.
                std::set<std::uint64_t> held_;

                /// @brief The largest id let go plus one; 0 before the first.
                std::uint64_t idle_value_ = 0;

                /// @brief Counts the resets, so a task of an earlier connection is ignored.
                std::uint64_t generation_ = 0;
        };

    } // namespace Distributed
} // namespace SushiRuntime
