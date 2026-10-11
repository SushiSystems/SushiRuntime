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

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>
#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /**
         * @brief Stores a node's SYCL event dependencies, inline up to STATIC_CAP events.
         *
         * Events past STATIC_CAP go to dynamic_data in order. Every inline slot at or past
         * static_count holds a default-constructed event.
         */
        struct SmallEventVector
        {
            static constexpr size_t STATIC_CAP = 8;

            size_t static_count = 0;                /**< Number of events in the inline buffer. */
            sycl::event static_data[STATIC_CAP];   /**< Inline event storage. */
            std::vector<sycl::event> dynamic_data; /**< Overflow storage when count > STATIC_CAP. */

            /** @brief Constructs an empty vector. */
            SmallEventVector() = default;

            /**
             * @brief Copies every event, inline and overflow, from @p other.
             * @param other Source vector.
             */
            SmallEventVector(const SmallEventVector& other) = default;

            /**
             * @brief Moves the events of @p other into this vector.
             * @param other Source vector; left with size() equal to zero.
             */
            SmallEventVector(SmallEventVector&& other)
                : SmallEventVector(other, std::make_index_sequence<STATIC_CAP>{})
            {
                other.clear();
            }

            /** @brief Releases every stored event. */
            ~SmallEventVector() = default;

            /**
             * @brief Replaces the stored events with a copy of those in @p other.
             * @param other Source vector; may be this vector.
             * @return *this.
             */
            inline SmallEventVector& operator=(const SmallEventVector& other)
            {
                if (this == &other)
                    return *this;

                for (size_t i = 0; i < other.static_count; ++i)
                    static_data[i] = other.static_data[i];
                set_inline_count(other.static_count);
                dynamic_data = other.dynamic_data;
                return *this;
            }

            /**
             * @brief Moves the events of @p other into this vector.
             * @param other Source vector; left with size() equal to zero.
             * @return *this.
             */
            inline SmallEventVector& operator=(SmallEventVector&& other)
            {
                if (this == &other)
                    return *this;

                for (size_t i = 0; i < other.static_count; ++i)
                    static_data[i] = std::move(other.static_data[i]);
                set_inline_count(other.static_count);
                dynamic_data = std::move(other.dynamic_data);
                other.clear();
                return *this;
            }

            /**
             * @brief Replaces the stored events with those from @p vec.
             * @param vec Source event list; may be this vector's own dynamic_data.
             * @return *this.
             */
            inline SmallEventVector& operator=(const std::vector<sycl::event>& vec)
            {
                const size_t inline_count = std::min(vec.size(), STATIC_CAP);

                for (size_t i = 0; i < inline_count; ++i)
                    static_data[i] = vec[i];
                set_inline_count(inline_count);

                // A list that is this vector's dynamic_data is erased from the front, not cleared.
                if (&vec == &dynamic_data)
                    dynamic_data.erase(dynamic_data.begin(),
                                       dynamic_data.begin() + static_cast<std::ptrdiff_t>(inline_count));
                else
                    dynamic_data.assign(vec.begin() + static_cast<std::ptrdiff_t>(inline_count),
                                        vec.end());
                return *this;
            }

            /** @brief Removes every event, inline and overflow. */
            inline void clear()
            {
                set_inline_count(0);
                dynamic_data.clear();
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

            private:
                /**
                 * @brief Builds a vector that takes the inline slots and overflow of @p source.
                 * @param source Vector whose slots are moved from; the caller empties it.
                 */
                template <std::size_t... Index>
                SmallEventVector(SmallEventVector& source, std::index_sequence<Index...>)
                    : static_count(source.static_count)
                    , static_data{std::move(source.static_data[Index])...}
                    , dynamic_data(std::move(source.dynamic_data))
                {

                }

                /**
                 * @brief Sets the inline count, resetting the slots that held events past it.
                 * @param count Number of inline slots holding events after the call; the caller has
                 *        already written them.
                 */
                inline void set_inline_count(size_t count)
                {
                    for (size_t i = count; i < static_count; ++i)
                        static_data[i] = sycl::event{};
                    static_count = count;
                }
        };

    } // namespace Graph
} // namespace SushiRuntime
