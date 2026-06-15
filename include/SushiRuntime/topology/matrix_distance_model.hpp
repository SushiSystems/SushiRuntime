/**************************************************************************/
/* matrix_distance_model.hpp                                              */
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

#include <vector>
#include <stdexcept>
#include <SushiRuntime/topology/i_topology_provider.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /**
         * @brief In-memory, hardware-free NUMA distance model for testing. [LSP]
         *
         * Holds an explicit N x N SLIT-style matrix so multi-node distance and
         * placement logic can be validated deterministically on any machine —
         * including single-node desktops and WSL where no real SLIT exists.
         *
         * Example (2 sockets, 21-cost remote hop):
         * @code
         *   MatrixDistanceModel model{{{10, 21},
         *                              {21, 10}}};
         * @endcode
         */
        class MatrixDistanceModel : public INumaDistanceModel
        {
            public:
                explicit MatrixDistanceModel(std::vector<std::vector<int>> matrix)
                    : matrix_(std::move(matrix))
                {
                    for (const auto& row : matrix_)
                        if (row.size() != matrix_.size())
                            throw std::invalid_argument("MatrixDistanceModel: matrix must be square.");
                }

                int distance(int numa_a, int numa_b) const override
                {
                    if (numa_a < 0 || numa_b < 0 ||
                        static_cast<size_t>(numa_a) >= matrix_.size() ||
                        static_cast<size_t>(numa_b) >= matrix_.size())
                    {
                        return (numa_a == numa_b) ? 10 : 21; // out-of-range → ACPI convention
                    }
                    return matrix_[static_cast<size_t>(numa_a)][static_cast<size_t>(numa_b)];
                }

                int node_count() const override
                {
                    return static_cast<int>(matrix_.size());
                }

            private:
                std::vector<std::vector<int>> matrix_;
        };

    } // namespace Topology
} // namespace SushiRuntime
