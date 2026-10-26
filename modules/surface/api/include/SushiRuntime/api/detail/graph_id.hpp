/**************************************************************************/
/* graph_id.hpp                                                           */
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
 * @file graph_id.hpp
 * @brief Declares GraphId, the process-wide identity of one owner of plans.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstdint>
#include <utility>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Takes the next identity from the process-wide counter, which starts at 1.
         * @return A value no earlier call returned and no later call will.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::uint64_t take_graph_id() noexcept;

        /**
         * @brief Holds the identity a graph, a dynamic graph or a plan owner reports.
         *
         * Taken at construction and kept across a move; a moved-from value reports 0, which no
         * graph ever has.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        class GraphId final
        {
            public:
                /** @brief Takes a new identity. */
                GraphId() noexcept : value_(take_graph_id())
                {
                }

                GraphId(const GraphId&) = delete;
                GraphId& operator=(const GraphId&) = delete;

                /** @brief Takes over @p other's identity and leaves it 0. */
                GraphId(GraphId&& other) noexcept : value_(std::exchange(other.value_, 0U))
                {
                }

                /** @brief Takes over @p other's identity and leaves it 0. */
                GraphId& operator=(GraphId&& other) noexcept
                {
                    if (this != &other)
                        value_ = std::exchange(other.value_, 0U);
                    return *this;
                }

                ~GraphId() = default;

                /** @brief Returns the identity; 0 after a move from this value. */
                [[nodiscard]] std::uint64_t value() const noexcept
                {
                    return value_;
                }

            private:
                /** @brief The identity, or 0. */
                std::uint64_t value_;
        };
    } // namespace Detail
} // namespace SushiRuntime
