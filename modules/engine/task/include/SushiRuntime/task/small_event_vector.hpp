/**************************************************************************/
/* small_event_vector.hpp                                                 */
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
 * @file small_event_vector.hpp
 * @brief Declares SmallEventVector, inline storage for a node's event dependencies.
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <vector>
#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Graph
    {
        /**
         * @brief Inline-optimised storage for a node's SYCL event dependencies.
         *
         * Holds up to STATIC_CAP events without a heap allocation; additional
         * events spill into a dynamic_data vector. Avoids per-node vector
         * allocation in the common case (≤ 8 deps).
         */
        struct SmallEventVector
        {
            static constexpr size_t STATIC_CAP = 8;

            size_t static_count = 0;                /**< Number of events in the inline buffer. */
            sycl::event static_data[STATIC_CAP];   /**< Inline event storage. */
            std::vector<sycl::event> dynamic_data; /**< Overflow storage when count > STATIC_CAP. */

            /**
             * @brief Replaces the stored events with those from @p vec.
             * @param vec Source event list.
             * @return *this.
             */
            inline SmallEventVector& operator=(const std::vector<sycl::event>& vec)
            {
                static_count = 0;
                dynamic_data.clear();

                for (const auto& e : vec) 
                {
                    if (static_count < STATIC_CAP) 
                        static_data[static_count++] = e;
                    else 
                        dynamic_data.push_back(e);
                }
                return *this;
            }

            /**
             * @brief Returns all stored events as a single vector.
             * @return Vector containing inline events followed by any overflow events.
             */
            inline std::vector<sycl::event> to_vector() const
            {
                std::vector<sycl::event> res;
                res.reserve(static_count + dynamic_data.size());

                for (size_t i = 0; i < static_count; ++i)
                    res.push_back(static_data[i]);

                for (const auto& e : dynamic_data)
                    res.push_back(e);

                return res;
            }

            /** @brief Read-only forward iterator over the inline buffer then the overflow vector. */
            struct const_iterator
            {
                const SmallEventVector* owner; /**< Vector being iterated. */
                size_t index;                  /**< Position in the combined inline-then-overflow sequence. */

                /**
                 * @brief Dereferences the event at the current position.
                 * @return The event, from inline storage or the overflow vector depending on @p index.
                 */
                inline const sycl::event& operator*() const
                {
                    return index < owner->static_count
                        ? owner->static_data[index]
                        : owner->dynamic_data[index - owner->static_count];
                }

                inline const_iterator& operator++() { ++index; return *this; }
                inline bool operator!=(const const_iterator& other) const { return index != other.index; }
                inline bool operator==(const const_iterator& other) const { return index == other.index; }
            };

            /**
             * @brief Number of events currently stored, inline plus overflow.
             * @return The event count.
             */
            inline size_t size() const { return static_count + dynamic_data.size(); }

            /** @brief Iterator to the first stored event, or end() when empty. */
            inline const_iterator begin() const { return {this, 0}; }

            /** @brief Iterator past the last stored event. */
            inline const_iterator end() const { return {this, static_count + dynamic_data.size()}; }
        };

    } // namespace Graph
} // namespace SushiRuntime
