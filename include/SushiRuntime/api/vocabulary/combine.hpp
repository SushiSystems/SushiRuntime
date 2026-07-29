/**************************************************************************/
/* combine.hpp                                                            */
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

#include <limits>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief The combining operators Graph::add_reduce() understands.
         *
         * A combiner is any binary functor that is trivially copyable, so it can be
         * captured into a device kernel. These three cover the common cases and each
         * supplies the identity its reduction needs for an empty input, which is why
         * they are types rather than lambdas: a lambda cannot carry identity(), so a
         * caller passing one uses the add_reduce overload that takes the identity
         * explicitly.
         *
         * The combiner does NOT determine the order values are combined in. That is
         * fixed by the reduction's element layout alone (see add_reduce), which is
         * what makes a floating-point sum reproducible.
         */

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
