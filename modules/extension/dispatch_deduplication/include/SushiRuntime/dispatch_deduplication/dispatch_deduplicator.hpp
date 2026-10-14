/**************************************************************************/
/* dispatch_deduplicator.hpp                                              */
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
 * @file dispatch_deduplicator.hpp
 * @brief Declares DispatchDeduplicator, the worker's memory of the task ids it has seen.
 * @author Mustafa Garip
 * @see modules/extension/dispatch_deduplication/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_set>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Remembers the last task ids dispatched, so a repeated frame runs nothing.
         *
         * The oldest id is forgotten once the capacity is reached. The deduplicator owns one
         * mutex and calls nothing while it holds it; any thread may call any member.
         */
        class SUSHIRUNTIME_API DispatchDeduplicator final
        {
            public:
                /** @brief Holds the ids remembered unless the constructor is told otherwise. */
                static constexpr std::size_t K_DEFAULT_CAPACITY = 4096;

                /**
                 * @brief Builds an empty deduplicator.
                 * @param capacity Most ids remembered; at least 1.
                 */
                explicit DispatchDeduplicator(std::size_t capacity = K_DEFAULT_CAPACITY);

                /**
                 * @brief Records a task id.
                 * @return True the first time an id is given, false while it is remembered.
                 */
                [[nodiscard]] bool mark_seen(std::uint64_t task_id);

                /** @brief Forgets every id; called when WELCOME opens a new master session. */
                void clear() noexcept;

                /** @brief Returns the number of ids remembered. */
                [[nodiscard]] std::size_t size() const;

            private:
                /// @brief Guards every member below.
                mutable std::mutex mutex_;

                /// @brief The ids remembered.
                std::unordered_set<std::uint64_t> seen_;

                /// @brief The same ids, oldest first.
                std::deque<std::uint64_t> order_;

                /// @brief Most ids remembered.
                std::size_t capacity_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
