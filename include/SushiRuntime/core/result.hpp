/****************************************************************/
/* result.hpp                                                   */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file result.hpp
 * @brief Declares Result and ErrorWrapper, the value-or-error return type.
 * @author Mustafa Garip
 */

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
        ErrorWrapper<std::decay_t<E>> make_error(E&& e)
        {
            return ErrorWrapper<std::decay_t<E>>{std::forward<E>(e)};
        }

        /**
         * @brief Holds either a successful result of type T or an error payload of type E.
         *
         * Manages a disjoint union of a success value and an error value, requiring explicit state checking before access.
         *
         * @tparam T The type of the success value.
         * @tparam E The type of the error value.
         */
        template <typename T, typename E>
        class Result
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
                bool has_value() const noexcept { return data_.index() == 0; }

                /** 
                 * @brief Checks if this object holds a success value.
                 * 
                 * Provides boolean conversion for if-statement checking.
                 *
                 * @return True if a success value is present, false if an error is present.
                 */
                explicit operator bool() const noexcept { return has_value(); }

                /** 
                 * @brief Retrieves the stored success value.
                 * 
                 * Extracts the value from the variant without bounds checking.
                 *
                 * @return A reference to the underlying success value.
                 * @warning Accessing the value when an error is stored yields undefined behavior.
                 */
                T& value() & { return std::get<0>(data_); }
                /** 
                 * @brief Retrieves the stored success value.
                 * 
                 * Extracts the value from the variant without bounds checking.
                 *
                 * @return A constant reference to the underlying success value.
                 * @warning Accessing the value when an error is stored yields undefined behavior.
                 */
                const T& value() const & { return std::get<0>(data_); }

                /** 
                 * @brief Retrieves the stored success value.
                 * 
                 * Extracts the value from the variant without bounds checking.
                 *
                 * @return A reference to the underlying success value.
                 * @warning Accessing the value when an error is stored yields undefined behavior.
                 */
                T& operator*() & { return std::get<0>(data_); }
                /** 
                 * @brief Retrieves the stored success value.
                 * 
                 * Extracts the value from the variant without bounds checking.
                 *
                 * @return A constant reference to the underlying success value.
                 * @warning Accessing the value when an error is stored yields undefined behavior.
                 */
                const T& operator*() const & { return std::get<0>(data_); }

                /** 
                 * @brief Accesses a member of the stored success value.
                 * 
                 * Evaluates the underlying value pointer.
                 *
                 * @return A pointer to the underlying success value.
                 * @warning Accessing the value when an error is stored yields undefined behavior.
                 */
                T* operator->() { return &std::get<0>(data_); }
                /** 
                 * @brief Accesses a member of the stored success value.
                 * 
                 * Evaluates the underlying value pointer.
                 *
                 * @return A constant pointer to the underlying success value.
                 * @warning Accessing the value when an error is stored yields undefined behavior.
                 */
                const T* operator->() const { return &std::get<0>(data_); }

                /** 
                 * @brief Retrieves the stored error payload.
                 * 
                 * Extracts the error from the variant without bounds checking.
                 *
                 * @return A reference to the underlying error value.
                 * @warning Accessing the error when a success value is stored yields undefined behavior.
                 */
                E& error() & { return std::get<1>(data_); }
                /** 
                 * @brief Retrieves the stored error payload.
                 * 
                 * Extracts the error from the variant without bounds checking.
                 *
                 * @return A constant reference to the underlying error value.
                 * @warning Accessing the error when a success value is stored yields undefined behavior.
                 */
                const E& error() const & { return std::get<1>(data_); }

            private:
                std::variant<T, E> data_; /**< Success value at index 0, error at index 1. */
        };

    } // namespace Core
} // namespace SushiRuntime
