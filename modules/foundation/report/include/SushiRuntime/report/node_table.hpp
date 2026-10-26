/**************************************************************************/
/* node_table.hpp                                                         */
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
 * @file node_table.hpp
 * @brief Declares NodeTable, the names and operations of a plan's nodes by registration index.
 * @author Mustafa Garip
 * @see modules/foundation/report/README.md
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <SushiRuntime/core/node_cost.hpp>
#include <SushiRuntime/core/operation_id.hpp>

namespace SushiRuntime
{
    /** @brief Describes the node or nodes one registration index stands for. */
    struct NodeTableRow
    {
        std::uint32_t index = 0;        /**< Registration index, the value of NodeEventRecord::node_index. */
        std::string name;               /**< Label of the first node registered under the index. */
        std::string origin;             /**< The caller's own operation that node belongs to; empty for none. */
        OperationID operation;          /**< Operation that node stated; unnamed for none. */
        std::optional<NodeCost> cost;   /**< Work one dispatch of that node does, as its caller counted it; absent for none. */
        bool host_native = false;       /**< Whether the node runs on a worker thread with no device queue. */
        std::uint32_t multiplicity = 0; /**< Internal nodes that share the index; a record may repeat that often per step. */
    };

    /**
     * @brief Holds one row per registration index of a plan, built once and never changed.
     *
     * A plan shares one table with every observation of its runs, so a run copies no string.
     * @see modules/foundation/report/README.md
     */
    class NodeTable
    {
        public:
            /** @brief Builds an empty table. */
            NodeTable() = default;

            /**
             * @brief Takes @p rows and orders them by index.
             * @pre No two rows share an index.
             */
            explicit NodeTable(std::vector<NodeTableRow> rows, std::string label = {},
                               std::uint64_t graph_id = 0)
                : rows_(std::move(rows)), label_(std::move(label)), graph_id_(graph_id)
            {
                std::sort(rows_.begin(), rows_.end(),
                          [](const NodeTableRow& left, const NodeTableRow& right)
                          {
                              return left.index < right.index;
                          });
            }

            /** @brief Returns the row of registration index @p index, or null when it has none. */
            [[nodiscard]] const NodeTableRow* find(std::uint32_t index) const noexcept
            {
                const auto at = std::lower_bound(rows_.begin(), rows_.end(), index,
                                                 [](const NodeTableRow& row, std::uint32_t wanted)
                                                 {
                                                     return row.index < wanted;
                                                 });
                return at != rows_.end() && at->index == index ? &*at : nullptr;
            }

            /** @brief Returns the rows in index order. */
            [[nodiscard]] const std::vector<NodeTableRow>& rows() const noexcept { return rows_; }

            /** @brief Returns the label of the graph the plan was built for; empty when it has none. */
            [[nodiscard]] const std::string& label() const noexcept { return label_; }

            /** @brief Returns the id of the graph the plan was built for; 0 when none is known. */
            [[nodiscard]] std::uint64_t graph_id() const noexcept { return graph_id_; }

            /** @brief Returns how many rows the table holds. */
            [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }

        private:
            /** @brief The rows, ordered by index. */
            std::vector<NodeTableRow> rows_;

            /** @brief The graph's label when the plan was built. */
            std::string label_;

            /** @brief The id of the graph the plan was built for. */
            std::uint64_t graph_id_ = 0;
    };
} // namespace SushiRuntime
