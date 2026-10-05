/****************************************************************/
/* span.hpp                                                     */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

/**
 * @file span.hpp
 * @brief Provides a non-owning view over a contiguous sequence of objects.
 * 
 * Implements a C++17 compatible subset of std::span to facilitate memory-safe array operations.
 */

#include <cstddef>

// Key the choice on __cplusplus (the language standard), NOT the library macro
// __cpp_lib_span: the latter is only defined after a header that provides it
// (<version>/<span>) has been included, so its visibility varies per translation
// unit — which silently splits SushiRuntime::span into std::span in one TU and
// the shim in another, producing link errors (mismatched mangled signatures).
// __cplusplus is defined uniformly for every TU in the build, so all TUs agree.
#if __cplusplus >= 202002L

    #include <span>

    namespace SushiRuntime
    {
        using std::span;
        inline constexpr std::size_t dynamic_extent = std::dynamic_extent;
    }

#else

    #include <array>
    #include <iterator>
    #include <type_traits>

    namespace SushiRuntime
    {
        inline constexpr std::size_t dynamic_extent = static_cast<std::size_t>(-1);

        /**
         * @brief A non-owning view over a contiguous sequence of objects.
         *
         * Provides safe access to an underlying array without copying or managing its lifetime.
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
                 * @brief Constructs a span from a contiguous container.
                 * 
                 * Resolves the underlying pointer and size via the container's data() and size() methods.
                 *
                 * @tparam Container The deduced container type.
                 * @tparam Decayed The stripped type of the container for SFINAE checks.
                 * @param c The contiguous container to view.
                 */
                template <typename Container,
                          typename Decayed = std::remove_cv_t<std::remove_reference_t<Container>>,
                          typename = std::enable_if_t<
                              !std::is_same_v<Decayed, span> &&
                              std::is_convertible_v<
                                  std::remove_pointer_t<decltype(std::declval<Container&>().data())> (*)[],
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
                constexpr pointer    data()  const noexcept { return data_; }
                /**
                 * @brief Retrieves the number of elements in the span.
                 * 
                 * Returns the explicit or deduced element count of the view.
                 *
                 * @return The number of elements.
                 */
                constexpr size_type  size()  const noexcept { return size_; }
                /**
                 * @brief Retrieves the size of the span in bytes.
                 * 
                 * Calculates the total memory footprint of the viewed sequence.
                 *
                 * @return The size in bytes.
                 */
                constexpr size_type  size_bytes() const noexcept { return size_ * sizeof(T); }
                /**
                 * @brief Checks if the span is empty.
                 * 
                 * Returns true if the size is zero.
                 *
                 * @return True if the span contains no elements, false otherwise.
                 */
                constexpr bool       empty() const noexcept { return size_ == 0; }

                constexpr reference operator[](size_type i) const noexcept { return data_[i]; }
                constexpr reference front() const noexcept { return data_[0]; }
                constexpr reference back()  const noexcept { return data_[size_ - 1]; }

                constexpr iterator begin() const noexcept { return data_; }
                constexpr iterator end()   const noexcept { return data_ + size_; }

                constexpr span subspan(size_type offset,
                                       size_type count = dynamic_extent) const noexcept
                {
                    size_type n = (count == dynamic_extent) ? (size_ - offset) : count;
                    return span(data_ + offset, n);
                }

                constexpr span first(size_type count) const noexcept { return span(data_, count); }
                constexpr span last(size_type count)  const noexcept { return span(data_ + (size_ - count), count); }

            private:
                pointer   data_ = nullptr;
                size_type size_ = 0;
        };

    } // namespace SushiRuntime

#endif
