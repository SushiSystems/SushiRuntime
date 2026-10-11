/**************************************************************************/
/* matrix_distance_model.hpp                                              */
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
 * @file matrix_distance_model.hpp
 * @brief Declares MatrixDistanceModel, a NUMA distance model held in memory.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <string>
#include <vector>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/topology/i_topology_provider.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Models NUMA distances from an explicit in-memory matrix, for tests.
         * @see modules/platform/topology/README.md
         */
        class MatrixDistanceModel : public INUMADistanceModel
        {
            public:
                /**
                 * @brief Constructs the model with a predefined distance matrix.
                 *
                 * Initializes the internal distance matrix and validates its dimensions.
                 * @param matrix The square distance matrix representing NUMA node latencies (takes ownership).
                 * @throws SushiException With Errc::invalid_access for a ragged matrix.
                 */
                explicit MatrixDistanceModel(std::vector<std::vector<int>> matrix)
                    : matrix_(std::move(matrix))
                {
                    for (std::size_t row = 0; row < matrix_.size(); ++row)
                        if (matrix_[row].size() != matrix_.size())
                            throw SushiException(Error{
                                Errc::invalid_access,
                                "MatrixDistanceModel: row " + std::to_string(row) + " holds " +
                                    std::to_string(matrix_[row].size()) +
                                    " entries and the matrix has " +
                                    std::to_string(matrix_.size()) +
                                    " rows; every row must be as long as the matrix."});
                }

                /**
                 * @brief Retrieves the memory access distance between two NUMA nodes.
                 *
                 * Looks up the latency cost from the internal matrix, falling back to ACPI convention if either index is out of bounds.
                 * @param numa_a The index of the source NUMA node.
                 * @param numa_b The index of the destination NUMA node.
                 * @return The latency cost from the matrix, or 10/21 if out of range.
                 */
                int distance(int numa_a, int numa_b) const override
                {
                    if (numa_a < 0 || numa_b < 0 ||
                        static_cast<size_t>(numa_a) >= matrix_.size() ||
                        static_cast<size_t>(numa_b) >= matrix_.size())
                    {
                        return (numa_a == numa_b) ? 10 : 21; // out-of-range -> ACPI convention
                    }
                    return matrix_[static_cast<size_t>(numa_a)][static_cast<size_t>(numa_b)];
                }

                /**
                 * @brief Retrieves the total number of simulated NUMA nodes.
                 *
                 * Returns the dimension of the internal distance matrix.
                 * @return The number of NUMA nodes represented by the matrix.
                 */
                int node_count() const override
                {
                    return static_cast<int>(matrix_.size());
                }

            private:
                std::vector<std::vector<int>> matrix_;
        };

    } // namespace Topology
} // namespace SushiRuntime
