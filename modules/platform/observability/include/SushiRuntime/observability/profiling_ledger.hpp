/**************************************************************************/
/* profiling_ledger.hpp                                                   */
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
 * @file profiling_ledger.hpp
 * @brief Declares ProfilingLedger, the per-plan store of node event records.
 * @author Mustafa Garip
 * @see modules/platform/observability/README.md
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include <SushiRuntime/observability/run_report.hpp>

namespace SushiRuntime
{
    namespace Observability
    {
        /**
         * @brief Stores one run's node event records per plan, at a high-water-mark size.
         * @see modules/platform/observability/README.md
         */
        class ProfilingLedger
        {
        public:
            /** @brief Sentinel returned by claim_slot() when a node's row is full. */
            static constexpr std::uint32_t npos = static_cast<std::uint32_t>(-1);

            ProfilingLedger() = default;

            /** @brief Move-only; hand-written since std::atomic has no move constructor. */
            ProfilingLedger(ProfilingLedger&& other) noexcept
                : node_count_(other.node_count_)
                , row_capacity_(std::move(other.row_capacity_))
                , row_offset_(std::move(other.row_offset_))
                , row_high_water_(std::move(other.row_high_water_))
                , records_(std::move(other.records_))
                , cursors_(std::move(other.cursors_))
                , pairing_owner_(other.pairing_owner_.load(std::memory_order_relaxed))
                , pairing_steady_ns_(other.pairing_steady_ns_.load(std::memory_order_relaxed))
                , pairing_uncertainty_ns_(other.pairing_uncertainty_ns_.load(std::memory_order_relaxed))
                , pairing_event_ns_(other.pairing_event_ns_.load(std::memory_order_relaxed))
                , run_start_steady_ns_(other.run_start_steady_ns_)
            {
            }

            /** @brief See the move constructor; every atomic is transferred by value. */
            ProfilingLedger& operator=(ProfilingLedger&& other) noexcept
            {
                if (this == &other) return *this;
                node_count_ = other.node_count_;
                row_capacity_ = std::move(other.row_capacity_);
                row_offset_ = std::move(other.row_offset_);
                row_high_water_ = std::move(other.row_high_water_);
                records_ = std::move(other.records_);
                cursors_ = std::move(other.cursors_);
                pairing_owner_.store(other.pairing_owner_.load(std::memory_order_relaxed), std::memory_order_relaxed);
                pairing_steady_ns_.store(other.pairing_steady_ns_.load(std::memory_order_relaxed), std::memory_order_relaxed);
                pairing_uncertainty_ns_.store(other.pairing_uncertainty_ns_.load(std::memory_order_relaxed), std::memory_order_relaxed);
                pairing_event_ns_.store(other.pairing_event_ns_.load(std::memory_order_relaxed), std::memory_order_relaxed);
                run_start_steady_ns_ = other.run_start_steady_ns_;
                return *this;
            }

            ProfilingLedger(const ProfilingLedger&) = delete;
            ProfilingLedger& operator=(const ProfilingLedger&) = delete;

            /**
             * @brief Grows the ledger for a new run and resets its per-run state.
             * @param multiplicities Count of internal nodes per registration index; its size is the
             *        row count.
             * @see modules/platform/observability/README.md
             */
            void reserve(const std::vector<std::uint32_t>& multiplicities,
                        std::uint32_t expected_invocations) noexcept
            {
                const std::size_t node_count = multiplicities.size();
                bool must_rebuild = node_count != node_count_;
                if (!must_rebuild)
                {
                    for (std::size_t i = 0; i < node_count; ++i)
                    {
                        const std::uint32_t needed =
                            std::max<std::uint32_t>(expected_invocations * multiplicities[i],
                                                    row_high_water_[i]);
                        if (needed > row_capacity_[i]) { must_rebuild = true; break; }
                    }
                }

                if (must_rebuild)
                {
                    node_count_ = node_count;
                    row_capacity_.assign(node_count_, 0);
                    row_offset_.assign(node_count_, 0);
                    if (row_high_water_.size() != node_count_)
                        row_high_water_.assign(node_count_, 0);

                    std::size_t total = 0;
                    for (std::size_t i = 0; i < node_count_; ++i)
                    {
                        const std::uint32_t needed =
                            std::max<std::uint32_t>(expected_invocations * multiplicities[i],
                                                    row_high_water_[i]);
                        row_capacity_[i] = needed;
                        row_offset_[i] = total;
                        total += needed;
                    }
                    records_.assign(total, NodeEventRecord{});
                    cursors_ = std::make_unique<std::atomic<std::uint32_t>[]>(
                        node_count_ ? node_count_ : 1);
                }

                reset_cursors();
                pairing_owner_.store(nullptr, std::memory_order_relaxed);
                pairing_event_ns_.store(0, std::memory_order_relaxed);
                pairing_steady_ns_.store(0, std::memory_order_relaxed);
                pairing_uncertainty_ns_.store(0, std::memory_order_relaxed);
            }

            /** @brief Records the steady-clock reading taken as this run starts. */
            void set_run_start(std::uint64_t steady_ns) noexcept { run_start_steady_ns_ = steady_ns; }

            /** @brief Zeroes every node's write cursor without touching capacity. */
            void reset_cursors() noexcept
            {
                for (std::size_t i = 0; i < node_count_; ++i)
                    cursors_[i].store(0, std::memory_order_relaxed);
            }

            /** @brief Claims the next write slot for a node's dispatch, or npos when its row is full. */
            std::uint32_t claim_slot(std::uint32_t node_index) noexcept
            {
                if (node_index >= node_count_) return npos;
                const std::uint32_t slot = cursors_[node_index].fetch_add(1, std::memory_order_relaxed);
                return slot < row_capacity_[node_index] ? slot : npos;
            }

            /** @brief Writes one record into a slot claim_slot() returned for the same node. */
            void write(std::uint32_t node_index, std::uint32_t slot, const NodeEventRecord& record) noexcept
            {
                records_[row_offset_[node_index] + slot] = record;
            }

            /**
             * @brief Appends every slot a node actually used, in node order, into the report.
             *
             * A row's true demand (its cursor, uncapped) becomes next reserve()'s
             * floor even when it exceeded this run's capacity, so an overflow
             * self-heals on the next run instead of silently repeating (item 4).
             */
            void collect(RunReport& report) noexcept
            {
                for (std::size_t n = 0; n < node_count_; ++n)
                {
                    const std::uint32_t demand = cursors_[n].load(std::memory_order_relaxed);
                    assert(demand <= row_capacity_[n] &&
                          "ProfilingLedger row overflow: some dispatches were not recorded this run");
                    row_high_water_[n] = demand;
                    const std::uint32_t used = std::min(demand, row_capacity_[n]);
                    const std::size_t base = row_offset_[n];
                    for (std::uint32_t i = 0; i < used; ++i)
                        report.node_events.push_back(records_[base + i]);
                }
            }

            /**
             * @brief Claims this run's clock pairing with a compare-exchange; true for the winner.
             *
             * The owner is the synchronizing atomic: a winning compare_exchange
             * publishes task with release, and is_pairing_owner() reads it with
             * acquire, so a reaping worker on another thread never observes a
             * half-written owner.
             */
            bool claim_pairing_owner(const void* task) noexcept
            {
                const void* expected = nullptr;
                return pairing_owner_.compare_exchange_strong(
                    expected, task, std::memory_order_acq_rel, std::memory_order_relaxed);
            }

            /** @brief Whether task is the run's pairing-owning submission. */
            bool is_pairing_owner(const void* task) const noexcept
            {
                return pairing_owner_.load(std::memory_order_acquire) == task;
            }

            /** @brief Records the steady-clock window the pairing owner's submit call spanned. */
            void set_pairing_submit_window(std::uint64_t steady_ns, std::uint64_t uncertainty_ns) noexcept
            {
                pairing_steady_ns_.store(steady_ns, std::memory_order_release);
                pairing_uncertainty_ns_.store(uncertainty_ns, std::memory_order_release);
            }

            /** @brief Records the pairing owner's event-clock submit reading, read at reap. */
            void set_pairing_event_ns(std::uint64_t event_ns) noexcept
            {
                pairing_event_ns_.store(event_ns, std::memory_order_release);
            }

            /** @brief Returns this run's clock pairing, or the identity pairing if nothing claimed it. */
            ClockPairing pairing() const noexcept
            {
                if (pairing_owner_.load(std::memory_order_acquire) != nullptr)
                {
                    return ClockPairing{pairing_event_ns_.load(std::memory_order_acquire),
                                        pairing_steady_ns_.load(std::memory_order_acquire),
                                        pairing_uncertainty_ns_.load(std::memory_order_acquire)};
                }
                return ClockPairing{run_start_steady_ns_, run_start_steady_ns_, 0};
            }

        private:
            std::size_t node_count_ = 0;                   /**< Rows the ledger currently indexes. */
            std::vector<std::uint32_t> row_capacity_;       /**< Slots guaranteed for each row. */
            std::vector<std::size_t> row_offset_;           /**< Each row's base offset into records_. */
            std::vector<std::uint32_t> row_high_water_;     /**< True demand (uncapped) seen per row at the last collect(). */
            std::vector<NodeEventRecord> records_;          /**< Flat, ragged node rows back to back. */
            std::unique_ptr<std::atomic<std::uint32_t>[]> cursors_; /**< Per-node next-slot cursor. */

            std::atomic<const void*> pairing_owner_{nullptr};    /**< Task identity that claimed the pairing. */
            std::atomic<std::uint64_t> pairing_steady_ns_{0};      /**< Steady reading beside the pairing submission. */
            std::atomic<std::uint64_t> pairing_uncertainty_ns_{0}; /**< Steady spread across the pairing submit call. */
            std::atomic<std::uint64_t> pairing_event_ns_{0};       /**< Pairing submission's command_submit, at reap. */
            std::uint64_t run_start_steady_ns_ = 0; /**< Fallback identity pairing for an all-native run; driver-thread-only. */
        };

    } // namespace Observability

    namespace Core
    {
        /** @brief Names Observability::ProfilingLedger by its spelling before v0.4.0. */
        using ProfilingLedger [[deprecated("use Observability::ProfilingLedger")]] =
            Observability::ProfilingLedger;

    } // namespace Core
} // namespace SushiRuntime
