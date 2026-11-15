/**************************************************************************/
/* physical_card_selector.cpp                                             */
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
 * @file physical_card_selector.cpp
 * @brief Defines select_physical_cards.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#include <SushiRuntime/topology/physical_card_selector.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <tuple>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        namespace
        {
            /** @brief Names one PCI function by its four address fields, in bus order. */
            using PCIKey = std::array<std::uint32_t, 4>;

            /** @brief Returns the key two devices share when they sit at one PCI address. */
            PCIKey pci_key(const PCIDomainAddress& address) noexcept
            {
                return PCIKey{address.domain, address.bus, address.device, address.function};
            }

            /** @brief Joins devices into cards. */
            class Cards
            {
                public:
                    explicit Cards(std::size_t count) : parent_(count)
                    {
                        for (std::size_t position = 0; position < count; ++position)
                            parent_[position] = position;
                    }

                    /** @brief Returns the lowest position of the card the device belongs to. */
                    std::size_t root(std::size_t position)
                    {
                        while (parent_[position] != position)
                        {
                            parent_[position] = parent_[parent_[position]];
                            position = parent_[position];
                        }
                        return position;
                    }

                    /** @brief Joins the cards of two devices. */
                    void join(std::size_t left, std::size_t right)
                    {
                        const std::size_t a = root(left);
                        const std::size_t b = root(right);
                        if (a != b)
                            parent_[std::max(a, b)] = std::min(a, b);
                    }

                private:
                    /// @brief The parent of each device; a root is its own parent.
                    std::vector<std::size_t> parent_;
            };

            /** @brief Returns whether the device at left is preferred to the one at right. */
            bool preferred(const std::vector<DeviceIdentity>& identities, std::size_t left,
                           std::size_t right)
            {
                const DeviceIdentity& a = identities[left];
                const DeviceIdentity& b = identities[right];
                const bool a_native = a.backend != DeviceBackend::OpenCL;
                const bool b_native = b.backend != DeviceBackend::OpenCL;
                if (a_native != b_native)
                    return a_native;
                return std::make_tuple(static_cast<std::uint8_t>(a.backend), a.name, left) <
                       std::make_tuple(static_cast<std::uint8_t>(b.backend), b.name, right);
            }
        } // namespace

        std::vector<std::size_t> select_physical_cards(const std::vector<DeviceIdentity>& identities)
        {
            Cards cards(identities.size());
            std::map<HardwareID, std::size_t> by_uuid;
            std::map<PCIKey, std::size_t> by_pci;
            for (std::size_t position = 0; position < identities.size(); ++position)
            {
                const DeviceIdentity& identity = identities[position];
                if (identity.uuid.has_value())
                {
                    const auto placed = by_uuid.emplace(*identity.uuid, position);
                    if (!placed.second)
                        cards.join(position, placed.first->second);
                }
                if (identity.pci_address.has_value())
                {
                    const auto placed = by_pci.emplace(pci_key(*identity.pci_address), position);
                    if (!placed.second)
                        cards.join(position, placed.first->second);
                }
            }

            std::map<std::size_t, std::size_t> best;
            for (std::size_t position = 0; position < identities.size(); ++position)
            {
                const auto placed = best.emplace(cards.root(position), position);
                if (!placed.second && preferred(identities, position, placed.first->second))
                    placed.first->second = position;
            }

            std::vector<std::size_t> kept;
            kept.reserve(best.size());
            for (const auto& card : best)
                kept.push_back(card.second);
            std::sort(kept.begin(), kept.end());
            return kept;
        }
    } // namespace Internal::Topology
} // namespace SushiRuntime
