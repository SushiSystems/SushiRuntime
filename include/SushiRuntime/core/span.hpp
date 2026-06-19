/**************************************************************************/
/* span.hpp                                                               */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

// SushiRuntime::span — a non-owning view over a contiguous sequence.
//
// On a C++20 (or newer) standard library this is exactly std::span, aliased in.
// On C++17 it is a minimal hand-rolled equivalent covering the subset the
// distributed layer uses (dynamic extent only). Spelling every use as
// SushiRuntime::span means the switch back to std::span is automatic the day the
// project raises CMAKE_CXX_STANDARD — no call sites change. See
// docs/slop/cpp_standard_migration.md.

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
         * @brief A non-owning view over a contiguous range (C++17 std::span subset).
         *
         * Dynamic-extent only. Provides the members the codebase relies on:
         * construction from a pointer+size, from a contiguous container, and from a
         * C array; data()/size()/empty(); indexing and iteration; and
         * subspan/first/last. It does not model static extents or the full span
         * interface — just enough to stand in for std::span until the standard moves
         * to C++20.
         *
         * @tparam T The element type (use `const T` for a read-only view).
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

                constexpr span() noexcept = default;

                constexpr span(pointer ptr, size_type count) noexcept
                    : data_(ptr), size_(count) {}

                constexpr span(pointer first, pointer last) noexcept
                    : data_(first), size_(static_cast<size_type>(last - first)) {}

                template <std::size_t N>
                constexpr span(element_type (&arr)[N]) noexcept
                    : data_(arr), size_(N) {}

                // From any contiguous container exposing data()/size() whose element
                // pointer converts to T* (e.g. std::vector<U> -> span<const U>). A
                // forwarding reference so a temporary container binds too — valid for
                // the full expression, which is how std::span<const T> behaves and how
                // the codebase passes msg.serialize() straight into send(). The
                // enable_if strips cv + ref before comparing to span so this never
                // hijacks span's own copy/move.
                template <typename Container,
                          typename Decayed = std::remove_cv_t<std::remove_reference_t<Container>>,
                          typename = std::enable_if_t<
                              !std::is_same_v<Decayed, span> &&
                              std::is_convertible_v<
                                  std::remove_pointer_t<decltype(std::declval<Container&>().data())> (*)[],
                                  T (*)[]>>>
                constexpr span(Container&& c) noexcept
                    : data_(c.data()), size_(c.size()) {}

                constexpr pointer    data()  const noexcept { return data_; }
                constexpr size_type  size()  const noexcept { return size_; }
                constexpr size_type  size_bytes() const noexcept { return size_ * sizeof(T); }
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
