/**************************************************************************/
/* sycl_device_enumerator.cpp                                             */
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
 * @file sycl_device_enumerator.cpp
 * @brief Defines SYCLDeviceEnumerator.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#include <SushiRuntime/topology/sycl_device_enumerator.hpp>

#include <exception>
#include <regex>
#include <string>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/topology/topology_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        namespace
        {
            /** @brief Returns the class SYCL reports for a device. */
            DeviceKind kind_of(const sycl::device& device)
            {
                if (device.is_cpu())
                    return DeviceKind::CPU;
                if (device.is_gpu())
                    return DeviceKind::GPU;
                if (device.is_accelerator())
                    return DeviceKind::Accelerator;
                return DeviceKind::Other;
            }

            /** @brief Returns the PCI address text found in a device's vendor string, or an empty text. */
            std::string pci_text_in_vendor(const sycl::device& device, const std::string& name)
            {
                try
                {
                    const std::string vendor = device.get_info<sycl::info::device::vendor>();
                    const std::regex pci_regex(
                        "([0-9a-fA-F]{4}):([0-9a-fA-F]{2}):([0-9a-fA-F]{2})\\.([0-9a-fA-F])");
                    std::smatch match;
                    if (std::regex_search(vendor, match, pci_regex))
                        return match.str();
                }
                catch (const std::exception& error)
                {
                    SR_LOG_DEBUG(K_TOPOLOGY_LOG_CATEGORY,
                                 "reading the vendor of device '{}' threw '{}'; "
                                 "looking for its PCI address in hwloc instead",
                                 name, error.what());
                }
                return {};
            }

            /** @brief Returns the PCI address text a GPU or an accelerator reports, or an empty text. */
            std::string pci_text_of(const sycl::device& device, const std::string& name)
            {
                try
                {
#if defined(SYCL_EXT_INTEL_DEVICE_INFO) && SYCL_EXT_INTEL_DEVICE_INFO >= 5
                    if (device.has(sycl::aspect::ext_intel_pci_address))
                        return device.get_info<sycl::ext::intel::info::device::pci_address>();
#endif
                    return pci_text_in_vendor(device, name);
                }
                catch (...)
                {
                    SR_LOG_WARN(K_TOPOLOGY_LOG_CATEGORY,
                                "Could not retrieve PCI address for {}, NUMA mapping may be degraded.",
                                name);
                    return {};
                }
            }
        } // namespace

        std::vector<sycl::device> sycl_devices()
        {
            return sycl::device::get_devices(sycl::info::device_type::all);
        }

        Result<std::vector<DeviceDescriptor>> SYCLDeviceEnumerator::enumerate()
        {
            try
            {
                std::vector<DeviceDescriptor> listed;
                for (const sycl::device& device : sycl_devices())
                {
                    DeviceDescriptor entry;
                    entry.id = listed.size();
                    entry.name = device.get_info<sycl::info::device::name>();
                    entry.kind = kind_of(device);
                    entry.fp64 = device.has(sycl::aspect::fp64);
                    entry.global_mem_size = device.get_info<sycl::info::device::global_mem_size>();
                    entry.max_compute_units = device.get_info<sycl::info::device::max_compute_units>();
                    if (entry.kind == DeviceKind::GPU || entry.kind == DeviceKind::Accelerator)
                        entry.pci_text = pci_text_of(device, entry.name);
                    listed.push_back(std::move(entry));
                }
                return listed;
            }
            catch (const sycl::exception& error)
            {
                return make_error(Error{ErrorCode::Unsupported, error.what()});
            }
        }
    } // namespace Internal::Topology
} // namespace SushiRuntime
