/**************************************************************************/
/* refusing_fill.hpp                                                      */
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
 * @file refusing_fill.hpp
 * @brief Declares RefusingFill, a host task whose copy fails the build of a plan on demand.
 * @see modules/surface/api/tests/graph/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

namespace SushiRuntime
{
    namespace Testing
    {
        /**
         * @brief Fills a range when it runs, and throws from its copy while a flag is set.
         * @see modules/surface/api/tests/graph/README.md
         */
        class RefusingFill final
        {
            public:
                /**
                 * @brief Builds a task that writes value to count elements of target.
                 * @param refuses Set while a copy of the task must throw; shared with the test.
                 * @param failure Message of the std::runtime_error a refused copy throws.
                 */
                RefusingFill(std::shared_ptr<bool> refuses, const char* failure, int* target,
                             std::size_t count, int value) noexcept
                    : refuses_(std::move(refuses)), failure_(failure), target_(target),
                      count_(count), value_(value)
                {
                }

                /** @brief Copies other, or throws std::runtime_error while the flag is set. */
                RefusingFill(const RefusingFill& other)
                    : refuses_(other.refuses_), failure_(other.failure_),
                      target_(other.target_), count_(other.count_), value_(other.value_)
                {
                    if (*refuses_)
                        throw std::runtime_error(failure_);
                }

                RefusingFill(RefusingFill&&) noexcept = default;
                RefusingFill& operator=(const RefusingFill&) = delete;
                RefusingFill& operator=(RefusingFill&&) = delete;
                ~RefusingFill() = default;

                /** @brief Writes the value to every element of the range. */
                void operator()() const
                {
                    for (std::size_t i = 0; i < count_; ++i)
                        target_[i] = value_;
                }

            private:
                /** @brief Set while a copy must throw. */
                std::shared_ptr<bool> refuses_;

                /** @brief Message of the exception a refused copy throws. */
                const char* failure_;

                /** @brief First element the task writes. */
                int* target_;

                /** @brief How many elements the task writes. */
                std::size_t count_;

                /** @brief What the task writes. */
                int value_;
        };
    } // namespace Testing
} // namespace SushiRuntime
