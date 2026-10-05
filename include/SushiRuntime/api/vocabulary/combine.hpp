/****************************************************************/
/* combine.hpp                                                  */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file combine.hpp
 * @brief Declares the Sum, Minimum and Maximum combiners of Graph::add_reduce().
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/vocabulary/README.md
 */

#pragma once

#include <limits>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Adds two values; identity is the value-initialised T.
         * @tparam T Element type being reduced.
         */
        template <typename T>
        struct Sum
        {
            /**
             * @brief Combines two values by addition.
             * @param a Left operand — the accumulator in a fold.
             * @param b Right operand — the next element.
             * @return Their sum.
             */
            T operator()(const T& a, const T& b) const { return a + b; }

            /**
             * @brief The value a sum of no elements produces.
             * @return The value-initialised T (zero for arithmetic types).
             */
            static constexpr T identity() { return T{}; }
        };

        /**
         * @brief Keeps the smaller of two values; identity is the largest T.
         * @tparam T Element type being reduced; must have a numeric_limits.
         */
        template <typename T>
        struct Minimum
        {
            /**
             * @brief Combines two values by keeping the smaller.
             * @param a Left operand — the accumulator in a fold.
             * @param b Right operand — the next element.
             * @return @p b when it compares less than @p a, else @p a.
             */
            T operator()(const T& a, const T& b) const { return b < a ? b : a; }

            /**
             * @brief The value a minimum over no elements produces.
             * @return The largest representable T, so any element wins against it.
             */
            static constexpr T identity() { return (std::numeric_limits<T>::max)(); }
        };

        /**
         * @brief Keeps the larger of two values; identity is the lowest T.
         * @tparam T Element type being reduced; must have a numeric_limits.
         */
        template <typename T>
        struct Maximum
        {
            /**
             * @brief Combines two values by keeping the larger.
             * @param a Left operand — the accumulator in a fold.
             * @param b Right operand — the next element.
             * @return @p b when @p a compares less than it, else @p a.
             */
            T operator()(const T& a, const T& b) const { return a < b ? b : a; }

            /**
             * @brief The value a maximum over no elements produces.
             * @return The lowest representable T, so any element wins against it.
             */
            static constexpr T identity() { return (std::numeric_limits<T>::lowest)(); }
        };

    } // namespace API

    using API::Sum;
    using API::Minimum;
    using API::Maximum;

} // namespace SushiRuntime
