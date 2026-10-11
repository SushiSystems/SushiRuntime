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
 */

#pragma once

#include <type_traits>
#include <utility>
#include <variant>

#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Wraps a value on its way into a Result's error channel.
         *
         * Forces the target Result constructor to treat the payload as an error rather than a success value.
         *
         * @tparam E The exact type of the error payload carried.
         */
        template <typename E>
        struct ErrorWrapper
        {
            E value; /**< The error payload. */
        };

        /** @brief Builds an ErrorWrapper, deducing the error type. */
        template <typename E>
        [[nodiscard]] ErrorWrapper<std::decay_t<E>> make_error(E&& e)
        {
            return ErrorWrapper<std::decay_t<E>>{std::forward<E>(e)};
        }

        /**
         * @brief Holds either a successful result of type T or an error payload of type E.
         *
         * @tparam T The type of the success value.
         * @tparam E The type of the error value.
         */
        template <typename T, typename E>
        class [[nodiscard]] Result
        {
            public:
                /**
                 * @brief Constructs a success result holding a value.
                 *
                 * Initializes the internal variant with the success state.
                 *
                 * @param value The successful result to store.
                 */
                Result(T value)
                    : data_(std::in_place_index<0>, std::move(value))
                {
                }

                /**
                 * @brief Constructs an error result from an ErrorWrapper.
                 *
                 * Initializes the internal variant with the error state.
                 *
                 * @param wrapper The wrapped error instance to store.
                 */
                Result(ErrorWrapper<E> wrapper)
                    : data_(std::in_place_index<1>, std::move(wrapper.value))
                {
                }

                /**
                 * @brief Checks if this object holds a success value.
                 *
                 * Returns the active state of the underlying variant.
                 *
                 * @return True if a success value is present, false if an error is present.
                 */
                [[nodiscard]] bool has_value() const noexcept { return data_.index() == 0; }

                /**
                 * @brief Checks if this object holds a success value.
                 *
                 * Provides boolean conversion for if-statement checking.
                 *
                 * @return True if a success value is present, false if an error is present.
                 */
                explicit operator bool() const noexcept { return has_value(); }

                /**
                 * @brief Returns a reference to the stored success value.
                 *
                 * @return A reference to the underlying success value.
                 * @throws Core::SushiException If the result holds an error. The exception
                 *                              carries the stored Error when E is Core::Error.
                 */
                [[nodiscard]] T& value() &
                {
                    if (!has_value())
                        throw_value_of_error();
                    return *std::get_if<0>(&data_);
                }

                /**
                 * @brief Returns a reference to the stored success value.
                 *
                 * @return A constant reference to the underlying success value.
                 * @throws Core::SushiException If the result holds an error. The exception
                 *                              carries the stored Error when E is Core::Error.
                 */
                [[nodiscard]] const T& value() const &
                {
                    if (!has_value())
                        throw_value_of_error();
                    return *std::get_if<0>(&data_);
                }

                /**
                 * @brief Returns a reference to the stored success value.
                 *
                 * @return A reference to the underlying success value.
                 * @throws Core::SushiException If the result holds an error. The exception
                 *                              carries the stored Error when E is Core::Error.
                 */
                [[nodiscard]] T& operator*() &
                {
                    if (!has_value())
                        throw_value_of_error();
                    return *std::get_if<0>(&data_);
                }

                /**
                 * @brief Returns a reference to the stored success value.
                 *
                 * @return A constant reference to the underlying success value.
                 * @throws Core::SushiException If the result holds an error. The exception
                 *                              carries the stored Error when E is Core::Error.
                 */
                [[nodiscard]] const T& operator*() const &
                {
                    if (!has_value())
                        throw_value_of_error();
                    return *std::get_if<0>(&data_);
                }

                /**
                 * @brief Returns a pointer to the stored success value.
                 *
                 * @return A pointer to the underlying success value.
                 * @throws Core::SushiException If the result holds an error. The exception
                 *                              carries the stored Error when E is Core::Error.
                 */
                [[nodiscard]] T* operator->()
                {
                    if (!has_value())
                        throw_value_of_error();
                    return std::get_if<0>(&data_);
                }

                /**
                 * @brief Returns a pointer to the stored success value.
                 *
                 * @return A constant pointer to the underlying success value.
                 * @throws Core::SushiException If the result holds an error. The exception
                 *                              carries the stored Error when E is Core::Error.
                 */
                [[nodiscard]] const T* operator->() const
                {
                    if (!has_value())
                        throw_value_of_error();
                    return std::get_if<0>(&data_);
                }

                /**
                 * @brief Returns a reference to the stored error payload.
                 *
                 * @return A reference to the underlying error value.
                 * @throws Core::SushiException If the result holds a value.
                 */
                [[nodiscard]] E& error() &
                {
                    if (has_value())
                        throw_error_of_value();
                    return *std::get_if<1>(&data_);
                }

                /**
                 * @brief Returns a reference to the stored error payload.
                 *
                 * @return A constant reference to the underlying error value.
                 * @throws Core::SushiException If the result holds a value.
                 */
                [[nodiscard]] const E& error() const &
                {
                    if (has_value())
                        throw_error_of_value();
                    return *std::get_if<1>(&data_);
                }

            private:
                /**
                 * @brief Throws the exception for a value read on a result that holds an error.
                 *
                 * @throws Core::SushiException Always; carries the stored Error when E is Core::Error.
                 */
                [[noreturn]] void throw_value_of_error() const
                {
                    if constexpr (std::is_same_v<E, Error>)
                    {
                        throw SushiException(*std::get_if<1>(&data_));
                    }
                    else
                    {
                        throw SushiException(Error{
                            Errc::invalid_state, "Result::value(): the result holds an error."});
                    }
                }

                /**
                 * @brief Throws the exception for an error read on a result that holds a value.
                 *
                 * @throws Core::SushiException Always.
                 */
                [[noreturn]] void throw_error_of_value() const
                {
                    throw SushiException(Error{
                        Errc::invalid_state, "Result::error(): the result holds a value."});
                }

                std::variant<T, E> data_; /**< Success value at index 0, error at index 1. */
        };

    } // namespace Core
} // namespace SushiRuntime
