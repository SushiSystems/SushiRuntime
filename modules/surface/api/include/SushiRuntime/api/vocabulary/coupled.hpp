/**************************************************************************/
/* coupled.hpp                                                            */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file coupled.hpp
 * @brief Declares Coupled, the States one update rule reads to write one of them.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
 */

#pragma once

#include <tuple>

#include <SushiRuntime/api/data/state.hpp>

namespace SushiRuntime
{
    /**
     * @brief Names the State a rule writes and the further States it reads.
     * @tparam Out    Element type of the State written.
     * @tparam Inputs Element type of each further State read; at least one.
     * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
     */
    template <typename Out, typename... Inputs>
    struct Coupled
    {
        static_assert(sizeof...(Inputs) >= 1,
                      "Coupled(out, in0, ...) names at least one State to read besides the "
                      "one written; a rule over one State is add_rule(state, kernel).");

        State<Out>& out;                      /**< The State whose next field is written. */
        std::tuple<State<Inputs>&...> inputs; /**< The States read, in argument order. */

        /**
         * @brief Names the States of one coupled rule.
         * @param written The State written; alive at every run, free to move after
         *                add_coupled().
         * @param read    The further States read; each alive at every run, free to move
         *                after add_coupled().
         */
        explicit Coupled(State<Out>& written, State<Inputs>&... read) noexcept
            : out(written), inputs(read...)
        {

        }
    };

    // The guide lets Coupled(u, v) omit the element types.
    template <typename Out, typename... Inputs>
    Coupled(State<Out>&, State<Inputs>&...) -> Coupled<Out, Inputs...>;



} // namespace SushiRuntime
