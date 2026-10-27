/**************************************************************************/
/* pairing_claim.hpp                                                      */
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
 * @file pairing_claim.hpp
 * @brief Declares PairingClaim, one dispatch's claim on its device's clock pairing.
 * @author Mustafa Garip
 * @see modules/platform/observability/README.md
 */

#pragma once

#include <chrono>
#include <cstdint>

#include <SushiRuntime/observability/profiling_ledger.hpp>

namespace SushiRuntime
{
    namespace Internal::Observability
    {
        /**
         * @brief Returns @p t as nanoseconds since the steady clock's epoch.
         * @param t The time point.
         * @return Nanoseconds since the steady clock's epoch.
         */
        inline std::uint64_t steady_ns(std::chrono::steady_clock::time_point t) noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
        }

        /**
         * @brief Holds one dispatch's claim on its device's clock pairing across its submit call.
         *
         * The steady window spans the whole submit call: a kernel's single submit, or a task body
         * with every submission it makes. The pairing's uncertainty is that call's length.
         * @see modules/platform/observability/README.md
         */
        class PairingClaim
        {
        public:
            /**
             * @brief Claims @p device's pairing for @p task when @p ledger is set and no task holds it.
             * @param ledger The run's ledger, or null when the dispatch must not claim.
             * @param device The device the dispatch submits to.
             * @param task Identity of the dispatch.
             */
            PairingClaim(ProfilingLedger* ledger, std::uint32_t device, const void* task) noexcept
                : device_(device), task_(task)
            {
                if (ledger != nullptr && ledger->claim_pairing_owner(device, task))
                {
                    ledger_ = ledger;
                    t0_ = std::chrono::steady_clock::now();
                }
            }

            PairingClaim(const PairingClaim&) = delete;
            PairingClaim& operator=(const PairingClaim&) = delete;

            /**
             * @brief Records the submit window, or gives the claim up when nothing was submitted.
             * @param submitted Whether the dispatch left an event to read at reap.
             * @return True when a won claim was given up, so a later dispatch may claim.
             */
            bool settle(bool submitted) noexcept
            {
                if (ledger_ == nullptr)
                    return false;
                if (!submitted)
                {
                    ledger_->release_pairing_owner(device_, task_);
                    return true;
                }
                const auto t1 = std::chrono::steady_clock::now();
                ledger_->set_pairing_submit_window(device_, steady_ns(t0_), steady_ns(t1) - steady_ns(t0_));
                return false;
            }

        private:
            ProfilingLedger* ledger_ = nullptr;          /**< Set only when the claim won. */
            std::uint32_t device_;                       /**< The claimed device. */
            const void* task_;                           /**< The claiming dispatch. */
            std::chrono::steady_clock::time_point t0_{}; /**< Steady reading before the submit. */
        };
    } // namespace Observability
} // namespace SushiRuntime
