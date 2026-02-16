/**************************************************************************/
/* runtime_context.cpp                                                    */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#include <map>
#include <array>
#include <atomic>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Execution
    {
        using namespace SushiRuntime::Topology;
        using namespace SushiRuntime::Core;

        /**
         * @brief Private implementation of RuntimeContext (Pimpl).
         * 
         * Handles hardware topology, queues, and allocators.
         */
        struct RuntimeContext::Impl : public Core::RefCounted
        {
            /** @brief Hardware inventory. */
            std::vector<HardwareNode> nodes;
            
            /** @brief Device discovery units: NUMA ID -> queue list. */
            std::map<int, std::vector<sycl::queue>> numa_aware_queues;
            
            /** @brief Flat list for fast access to all queues. */
            std::vector<sycl::queue> all_queues;

            /** @brief NUMA-local allocators using atomic storage. */
            std::atomic<SushiRuntime::Memory::USMAllocator*> numa_allocators[Core::MAX_NUMA_NODES];

            Impl() 
            {
                for (size_t i = 0; i < Core::MAX_NUMA_NODES; ++i) 
                    numa_allocators[i].store(nullptr, std::memory_order_relaxed);
            }

            ~Impl() 
            {
                /** @brief Cleanup atomic pointers manually. */
                for (size_t i = 0; i < Core::MAX_NUMA_NODES; ++i)
                {
                    auto* ptr = numa_allocators[i].load(std::memory_order_relaxed);
                    if (ptr) 
                    {
                        ptr->release();
                    }
                }

                for (auto& q : all_queues) 
                {
                    q.wait_and_throw();
                }
            }
        };

        RuntimeContext::RuntimeContext() : impl_(make_sushi<Impl>()) 
        {
            SR_LOG_INFO("Initializing compute stack...");
            probe_hardware();
        }

        RuntimeContext::~RuntimeContext() = default;

        /**
         * @brief Hardware discovery and topology mapping process.
         */
        void RuntimeContext::probe_hardware() 
        {
            try 
            {
                /** @brief Step 1: hardware discovery - probe SYCL devices. */
                HardwareDiscovery discovery;
                impl_->nodes = discovery.probe_all_devices();

                /** @brief Step 2: NUMA mapping - map devices to NUMA nodes. */
                NUMAMapper mapper;
                mapper.map_distances(impl_->nodes);

                /** @brief Step 3: queue configuration - create SYCL queues. */
                impl_->all_queues.clear();
                impl_->numa_aware_queues.clear();

                for (const auto& node : impl_->nodes) 
                {
                    sycl::property_list props{ sycl::property::queue::in_order() };
                    
                    sycl::queue q(node.sycl_dev, [](sycl::exception_list elist) 
                    {
                        for (auto e : elist) std::rethrow_exception(e);
                    }, props);

                    impl_->all_queues.push_back(q);
                    impl_->numa_aware_queues[node.numa_node_id].push_back(q);

                    SR_LOG_INFO("Device: {} | NUMA: {} | Memory: {} MB | Compute units: {}", 
                               node.name, 
                               node.numa_node_id,
                               node.global_mem_size / (1024 * 1024),
                               node.max_compute_units);
                }

                SR_LOG_INFO("Infrastructure ready. Total devices: {}, NUMA domains: {}", 
                            impl_->all_queues.size(), 
                            impl_->numa_aware_queues.size());

                get_allocator(0);
            }
            catch (const std::exception& e) 
            {
                SR_LOG_ERROR("Critical initialization failure: {}", e.what());
                throw;
            }
        }

        sycl::queue& RuntimeContext::get_queue(size_t index) 
        {
            SR_THROW_IF(index >= impl_->all_queues.size(), "Queue index out of bounds.");
            return impl_->all_queues[index];
        }

        sushi_ptr<SushiRuntime::Memory::USMAllocator> RuntimeContext::get_allocator() 
        {
            return get_allocator(0);
        }

        /**
         * @brief Returns the closest SYCL queue for a specific NUMA node.
         * 
         * Falls back to default queue if none found.
         * 
         * @param numa_id Target NUMA node ID.
         * @return sycl::queue& Reference to the queue.
         */
        sycl::queue& RuntimeContext::get_numa_local_queue(int numa_id)
        {
             auto it = impl_->numa_aware_queues.find(numa_id);
             if (it != impl_->numa_aware_queues.end() && !it->second.empty())
             {
                 // TODO: implement round-robin or least-loaded strategy for queue selection.
                 return it->second[0];
             }
             
             SR_LOG_WARN("No queue found for NUMA {}, falling back to default.", numa_id);
             return get_queue(0);
        }

        /**
         * @brief Returns a NUMA-aware USM allocator (lazy initialization).
         * 
         * Creates a new allocator atomically if it doesn't exist.
         * 
         * @param numa_id Target NUMA node ID.
         * @return sushi_ptr<SushiRuntime::Memory::USMAllocator> The allocator.
         */
        sushi_ptr<SushiRuntime::Memory::USMAllocator> RuntimeContext::get_allocator(int numa_id)
        {
            if (numa_id < 0 || static_cast<size_t>(numa_id) >= Core::MAX_NUMA_NODES) 
            {
                SR_LOG_WARN("Invalid NUMA ID {}, falling back to default.", numa_id);
                return get_allocator();
            }

            /** @brief Step 1: fast path - check if exists. */
            auto* existing = impl_->numa_allocators[numa_id].load(std::memory_order_acquire);
            if (existing) 
            {
                return sushi_ptr<SushiRuntime::Memory::USMAllocator>(existing);
            }

            /** @brief Step 2: slow path - create and try atomic swap. */
            SR_LOG_DEBUG("Initializing allocator for NUMA {} (potential race)", numa_id);
            sycl::queue& q = get_numa_local_queue(numa_id);
            auto new_alloc_sushi = SushiRuntime::Memory::create_allocator(q, numa_id);
            auto* new_alloc_raw = new_alloc_sushi.get();

            new_alloc_raw->add_ref();

            SushiRuntime::Memory::USMAllocator* expected = nullptr;
            
            /** @brief Step 3: compare-and-swap (CAS). */
            if (impl_->numa_allocators[numa_id].compare_exchange_strong(expected, new_alloc_raw, 
                                                                        std::memory_order_release, 
                                                                        std::memory_order_acquire)) 
            {
                return new_alloc_sushi;
            }
            else 
            {
                new_alloc_raw->release();
                return sushi_ptr<SushiRuntime::Memory::USMAllocator>(expected);
            }
        }

        void RuntimeContext::wait_all() 
        {
            for (auto& q : impl_->all_queues) 
            {
                q.wait_and_throw();
            }
        }

    } // namespace Execution
} // namespace SushiRuntime