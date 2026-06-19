/**************************************************************************/
/* result.hpp                                                             */
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

#include <type_traits>
#include <utility>
#include <variant>

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Wraps a value on its way into a Result's error channel.
         *
         * Returned by make_error(); a Result constructs from it implicitly so call
         * sites read `return make_error(Error{...});`. Mirrors std::unexpected from
         * C++23 without requiring it.
         *
         * @tparam E The error type carried.
         */
        template <typename E>
        struct ErrorWrapper
        {
            E value; /**< The error payload. */
        };

        /**
         * @brief Builds an ErrorWrapper, deducing the error type.
         * @param e The error value to wrap.
         * @return An ErrorWrapper a Result can be constructed from.
         */
        template <typename E>
        ErrorWrapper<std::decay_t<E>> make_error(E&& e)
        {
            return ErrorWrapper<std::decay_t<E>>{std::forward<E>(e)};
        }

        /**
         * @brief Holds either a success value of type T or an error of type E.
         *
         * A small C++17 stand-in for std::expected covering exactly what the v1
         * API needs: construct from a value or from make_error(), test with
         * operator bool, and read the value (value()/operator*) or the error
         * (error()). T and E must be distinct types. Reading the wrong side is
         * undefined, as with std::expected; callers check first.
         *
         * @tparam T The success value type.
         * @tparam E The error type.
         */
        template <typename T, typename E>
        class Result
        {
            public:
                /**
                 * @brief Constructs a success result holding a value.
                 * @param value The success value.
                 */
                Result(T value)
                    : data_(std::in_place_index<0>, std::move(value))
                {
                }

                /**
                 * @brief Constructs an error result from make_error().
                 * @param wrapper The wrapped error value.
                 */
                Result(ErrorWrapper<E> wrapper)
                    : data_(std::in_place_index<1>, std::move(wrapper.value))
                {
                }

                /** @brief True when this holds a value rather than an error. */
                bool has_value() const noexcept { return data_.index() == 0; }

                /** @brief True when this holds a value rather than an error. */
                explicit operator bool() const noexcept { return has_value(); }

                /** @brief The success value; valid only when has_value() is true. */
                T& value() & { return std::get<0>(data_); }
                /** @brief The success value; valid only when has_value() is true. */
                const T& value() const & { return std::get<0>(data_); }

                /** @brief The success value; valid only when has_value() is true. */
                T& operator*() & { return std::get<0>(data_); }
                /** @brief The success value; valid only when has_value() is true. */
                const T& operator*() const & { return std::get<0>(data_); }

                /** @brief Member access on the success value. */
                T* operator->() { return &std::get<0>(data_); }
                /** @brief Member access on the success value. */
                const T* operator->() const { return &std::get<0>(data_); }

                /** @brief The error; valid only when has_value() is false. */
                E& error() & { return std::get<1>(data_); }
                /** @brief The error; valid only when has_value() is false. */
                const E& error() const & { return std::get<1>(data_); }

            private:
                std::variant<T, E> data_; /**< Success value at index 0, error at index 1. */
        };

    } // namespace Core
} // namespace SushiRuntime
