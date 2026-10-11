/**************************************************************************/
/* result.hpp                                                             */
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
 * @file result.hpp
 * @brief Declares Result and ErrorWrapper, the value-or-error return type.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    /**
     * @brief Wraps a payload so that a Result stores it as its error.
     */
    template <typename E>
    struct ErrorWrapper
    {
        E value; /**< The error payload. */
    };

    /**
     * @brief Returns the payload wrapped for the error side of a Result.
     */
    template <typename E>
    [[nodiscard]] ErrorWrapper<std::decay_t<E>> make_error(E&& error)
    {
        return ErrorWrapper<std::decay_t<E>>{std::forward<E>(error)};
    }

    /**
     * @brief Holds what the accessors of Result share.
     */
    namespace Detail
    {
        /**
         * @brief Returns the text the assertion handler prints for a value read on this error.
         */
        template <typename E>
        [[nodiscard]] const char* describe_stored_error(const E& stored) noexcept
        {
            if constexpr (std::is_same_v<E, Error>)
                return stored.message.c_str();
            else
                return "the Result holds an error";
        }
    } // namespace Detail

    /**
     * @brief Holds a value of type T or an error of type E.
     * @pre A value accessor is called on a value, and error() on an error.
     */
    template <typename T, typename E = Error>
    class [[nodiscard]] Result
    {
        public:
            /**
             * @brief Stores a value.
             */
            Result(T value)
                : data_(std::in_place_index<0>, std::move(value))
            {
            }

            /**
             * @brief Stores an error.
             */
            Result(ErrorWrapper<E> wrapper)
                : data_(std::in_place_index<1>, std::move(wrapper.value))
            {
            }

            /**
             * @brief Reports whether a value is stored.
             */
            [[nodiscard]] bool has_value() const noexcept { return data_.index() == 0; }

            /**
             * @brief Reports whether a value is stored.
             */
            explicit operator bool() const noexcept { return has_value(); }

            /**
             * @brief Returns the stored value.
             * @pre has_value() is true.
             */
            [[nodiscard]] T& value() &
            {
                verify_value();
                return *std::get_if<0>(&data_);
            }

            /**
             * @brief Returns the stored value.
             * @pre has_value() is true.
             */
            [[nodiscard]] const T& value() const&
            {
                verify_value();
                return *std::get_if<0>(&data_);
            }

            /**
             * @brief Returns the stored value of a Result that is about to be destroyed.
             * @pre has_value() is true.
             */
            [[nodiscard]] T&& value() &&
            {
                verify_value();
                return std::move(*std::get_if<0>(&data_));
            }

            /**
             * @brief Returns the stored value.
             * @pre has_value() is true.
             */
            [[nodiscard]] T& operator*() &
            {
                verify_value();
                return *std::get_if<0>(&data_);
            }

            /**
             * @brief Returns the stored value.
             * @pre has_value() is true.
             */
            [[nodiscard]] const T& operator*() const&
            {
                verify_value();
                return *std::get_if<0>(&data_);
            }

            /**
             * @brief Returns the stored value of a Result that is about to be destroyed.
             * @pre has_value() is true.
             */
            [[nodiscard]] T&& operator*() &&
            {
                verify_value();
                return std::move(*std::get_if<0>(&data_));
            }

            /**
             * @brief Returns the address of the stored value.
             * @pre has_value() is true.
             */
            [[nodiscard]] T* operator->()
            {
                verify_value();
                return std::get_if<0>(&data_);
            }

            /**
             * @brief Returns the address of the stored value.
             * @pre has_value() is true.
             */
            [[nodiscard]] const T* operator->() const
            {
                verify_value();
                return std::get_if<0>(&data_);
            }

            /**
             * @brief Returns the stored error.
             * @pre has_value() is false.
             */
            [[nodiscard]] E& error() &
            {
                SR_VERIFY(!has_value(), "Result::error() needs a Result that holds an error");
                return *std::get_if<1>(&data_);
            }

            /**
             * @brief Returns the stored error.
             * @pre has_value() is false.
             */
            [[nodiscard]] const E& error() const&
            {
                SR_VERIFY(!has_value(), "Result::error() needs a Result that holds an error");
                return *std::get_if<1>(&data_);
            }

        private:
            /**
             * @brief Stops the process with the stored error's text when no value is stored.
             */
            void verify_value() const noexcept
            {
                SR_VERIFY(has_value(), Detail::describe_stored_error(*std::get_if<1>(&data_)));
            }

            /// @brief The value at index 0, the error at index 1.
            std::variant<T, E> data_;
    };

    /**
     * @brief Holds success with no value, or an error of type E.
     * @pre value() is called on a success, and error() on an error.
     */
    template <typename E>
    class [[nodiscard]] Result<void, E>
    {
        public:
            /**
             * @brief Stores success.
             */
            Result() noexcept = default;

            /**
             * @brief Stores an error.
             */
            Result(ErrorWrapper<E> wrapper)
                : error_(std::move(wrapper.value))
            {
            }

            /**
             * @brief Reports whether success is stored.
             */
            [[nodiscard]] bool has_value() const noexcept { return !error_.has_value(); }

            /**
             * @brief Reports whether success is stored.
             */
            explicit operator bool() const noexcept { return has_value(); }

            /**
             * @brief Stops the process with the stored error's text when an error is stored.
             * @pre has_value() is true.
             */
            void value() const noexcept
            {
                SR_VERIFY(has_value(), Detail::describe_stored_error(*error_));
            }

            /**
             * @brief Returns the stored error.
             * @pre has_value() is false.
             */
            [[nodiscard]] E& error() &
            {
                SR_VERIFY(!has_value(), "Result::error() needs a Result that holds an error");
                return *error_;
            }

            /**
             * @brief Returns the stored error.
             * @pre has_value() is false.
             */
            [[nodiscard]] const E& error() const&
            {
                SR_VERIFY(!has_value(), "Result::error() needs a Result that holds an error");
                return *error_;
            }

        private:
            /// @brief The error; empty on success.
            std::optional<E> error_;
    };
} // namespace SushiRuntime
