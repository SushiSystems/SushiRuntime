/**************************************************************************/
/* span.hpp                                                               */
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
 * @file span.hpp
 * @brief Declares SushiRuntime::span, a non-owning view with std::span conversions from C++20.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#if __cplusplus >= 202002L
    #include <ranges>
    #include <span>
#endif

namespace SushiRuntime
{
    inline constexpr std::size_t dynamic_extent = static_cast<std::size_t>(-1);

    template <typename T>
    class span;

    namespace Detail
    {
        /** @brief Tells whether Range owns no element, so an rvalue of it may be viewed. */
        template <typename Range>
        inline constexpr bool is_borrowed_view = false;

#if __cplusplus >= 202002L
        /** @brief Marks std::span as a view that owns no element. */
        template <typename U, std::size_t E>
        inline constexpr bool is_borrowed_view<std::span<U, E>> = true;
#endif
    } // namespace Detail

    /**
     * @brief Views a contiguous sequence of objects without owning it.
     *
     * The span does not manage the lifetime of the elements it refers to.
     *
     * @tparam T The underlying element type of the sequence.
     */
    template <typename T>
    class span
    {
        public:
            using element_type    = T;
            using value_type      = std::remove_cv_t<T>;
            using size_type       = std::size_t;
            using pointer         = T*;
            using reference       = T&;
            using iterator        = T*;
            using const_iterator  = const T*;

            /**
             * @brief Constructs an empty span.
             *
             * Initializes the internal pointer to null and the size to zero.
             */
            constexpr span() noexcept = default;

            /**
             * @brief Constructs a span from a pointer and a size.
             *
             * Maps the specified number of elements starting at the given address.
             *
             * @param ptr The starting address of the contiguous memory block.
             * @param count The number of elements in the memory block.
             */
            constexpr span(pointer ptr, size_type count) noexcept
                : data_(ptr), size_(count) {}

            /**
             * @brief Constructs a span from a pair of pointers.
             *
             * Calculates the size based on the distance between the two pointers.
             *
             * @param first The pointer to the first element.
             * @param last The pointer one past the last element.
             */
            constexpr span(pointer first, pointer last) noexcept
                : data_(first), size_(static_cast<size_type>(last - first)) {}

            /**
             * @brief Constructs a span from a native C array.
             *
             * Automatically deduces the array's extent at compile time.
             *
             * @tparam N The automatically deduced size of the array.
             * @param arr The native array to wrap.
             */
            template <std::size_t N>
            constexpr span(element_type (&arr)[N]) noexcept
                : data_(arr), size_(N) {}

            /**
             * @brief Constructs a span over a contiguous container's data() and size().
             *
             * An rvalue binds only when T is const or the argument is itself a view.
             *
             * @param c The container to view; the span must not outlive it.
             */
            template <typename Container,
                      typename Decayed = std::remove_cv_t<std::remove_reference_t<Container>>,
                      typename = std::enable_if_t<
                          !std::is_same_v<Decayed, span> &&
                          (std::is_lvalue_reference_v<Container> || std::is_const_v<T> ||
                           Detail::is_borrowed_view<Decayed>) &&
                          std::is_convertible_v<
                              std::remove_pointer_t<
                                  decltype(std::declval<Container&>().data())> (*)[],
                              T (*)[]>>>
            constexpr span(Container&& c) noexcept
                : data_(c.data()), size_(c.size()) {}

            /**
             * @brief Retrieves the pointer to the underlying data.
             *
             * Exposes the raw memory address for direct operations.
             *
             * @return The starting pointer of the viewed sequence.
             */
            [[nodiscard]] constexpr pointer data() const noexcept
            {
                return data_;
            }

            /**
             * @brief Retrieves the number of elements in the span.
             *
             * Returns the explicit or deduced element count of the view.
             *
             * @return The number of elements.
             */
            [[nodiscard]] constexpr size_type size() const noexcept
            {
                return size_;
            }

            /**
             * @brief Retrieves the size of the span in bytes.
             *
             * Calculates the total memory footprint of the viewed sequence.
             *
             * @return The size in bytes.
             */
            [[nodiscard]] constexpr size_type size_bytes() const noexcept
            {
                return size_ * sizeof(T);
            }

            /**
             * @brief Checks if the span is empty.
             *
             * Returns true if the size is zero.
             *
             * @return True if the span contains no elements, false otherwise.
             */
            [[nodiscard]] constexpr bool empty() const noexcept
            {
                return size_ == 0;
            }

            /**
             * @brief Returns a reference to the element at index i, without a bounds check.
             * @pre i < size()
             */
            [[nodiscard]] constexpr reference operator[](size_type i) const noexcept
            {
                return data_[i];
            }

            /**
             * @brief Returns a reference to the first element.
             * @pre !empty()
             */
            [[nodiscard]] constexpr reference front() const noexcept
            {
                return data_[0];
            }

            /**
             * @brief Returns a reference to the last element.
             * @pre !empty()
             */
            [[nodiscard]] constexpr reference back() const noexcept
            {
                return data_[size_ - 1];
            }

            /** @brief Returns an iterator to the first element. */
            [[nodiscard]] constexpr iterator begin() const noexcept
            {
                return data_;
            }

            /** @brief Returns an iterator one past the last element. */
            [[nodiscard]] constexpr iterator end() const noexcept
            {
                return data_ + size_;
            }

            /**
             * @brief Returns a span over count elements starting at offset.
             * @pre offset <= size(); when count is given, offset + count <= size().
             */
            [[nodiscard]] constexpr span subspan(size_type offset,
                                                 size_type count = dynamic_extent) const noexcept
            {
                size_type n = (count == dynamic_extent) ? (size_ - offset) : count;
                return span(data_ + offset, n);
            }

            /**
             * @brief Returns a span over the first count elements.
             * @pre count <= size()
             */
            [[nodiscard]] constexpr span first(size_type count) const noexcept
            {
                return span(data_, count);
            }

            /**
             * @brief Returns a span over the last count elements.
             * @pre count <= size()
             */
            [[nodiscard]] constexpr span last(size_type count) const noexcept
            {
                return span(data_ + (size_ - count), count);
            }

        private:
            pointer   data_ = nullptr;
            size_type size_ = 0;
    };
} // namespace SushiRuntime

#if __cplusplus >= 202002L
/** @brief Lets std::span and the range algorithms take an rvalue SushiRuntime::span. */
template <typename T>
inline constexpr bool std::ranges::enable_borrowed_range<SushiRuntime::span<T>> = true;
#endif
