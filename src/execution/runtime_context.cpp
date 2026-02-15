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

#include "../../include/SushiRuntime/execution/runtime_context.hpp"
#include "../../include/SushiRuntime/topology/hw_discovery.hpp"
#include "../../include/SushiRuntime/topology/numa_mapper.hpp"
#include "../../include/SushiRuntime/core/logger.hpp"
#include <atomic>
#include <array>
#include <map>

namespace SushiRuntime
{
    namespace Execution
    {
        using namespace SushiRuntime::Topology;
        using namespace SushiRuntime::Core;

        /**
         * @brief private implementation of RuntimeContext (pimpl).
         * 
         * handles hardware topology, queues, and allocators.
         */
        struct RuntimeContext::Impl : public Core::RefCounted
        {
            /** @brief hardware inventory. */
            std::vector<HardwareNode> nodes;
            
            /** @brief device discovery units: numa id -> queue list. */
            std::map<int, std::vector<sycl::queue>> numa_aware_queues;
            
            /** @brief flat list for fast access to all queues. */
            std::vector<sycl::queue> all_queues;

            /** @brief numa-local allocators using atomic storage. */
            std::atomic<SushiRuntime::Memory::USMAllocator*> numa_allocators[Core::MAX_NUMA_NODES];

            Impl() 
            {
                for(int i=0; i<Core::MAX_NUMA_NODES; ++i) 
                    numa_allocators[i].store(nullptr, std::memory_order_relaxed);
            }

            ~Impl() 
            {
                /** @brief cleanup atomic pointers manually. */
                for(int i=0; i<Core::MAX_NUMA_NODES; ++i)
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
            SR_LOG_INFO("initializing compute stack...");
            probe_hardware();
        }

        RuntimeContext::~RuntimeContext() = default;

        /**
         * @brief hardware discovery and topology mapping process.
         */
        void RuntimeContext::probe_hardware() 
        {
            try 
            {
                /** @brief step 1: hardware discovery - probe sycl devices. */
                HardwareDiscovery discovery;
                impl_->nodes = discovery.probe_all_devices();

                /** @brief step 2: numa mapping - map devices to numa nodes. */
                NUMAMapper mapper;
                mapper.map_distances(impl_->nodes);

                /** @brief step 3: queue configuration - create sycl queues. */
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

                    SR_LOG_INFO("device: {} | numa: {} | memory: {} mb | compute units: {}", 
                               node.name, 
                               node.numa_node_id,
                               node.global_mem_size / (1024 * 1024),
                               node.max_compute_units);
                }

                SR_LOG_INFO("infrastructure ready. total devices: {}, numa domains: {}", 
                            impl_->all_queues.size(), 
                            impl_->numa_aware_queues.size());

                get_allocator(0);
            }
            catch (const std::exception& e) 
            {
                SR_LOG_ERROR("critical initialization failure: {}", e.what());
                throw;
            }
        }

        sycl::queue& RuntimeContext::get_queue(size_t index) 
        {
            SR_THROW_IF(index >= impl_->all_queues.size(), "queue index out of bounds.");
            return impl_->all_queues[index];
        }

        sushi_ptr<SushiRuntime::Memory::USMAllocator> RuntimeContext::get_allocator() 
        {
            return get_allocator(0);
        }

        /**
         * @brief returns the closest sycl queue for a specific numa node.
         * 
         * falls back to default queue if none found.
         * 
         * @param numa_id target numa node id.
         * @return sycl::queue& reference to the queue.
         */
        sycl::queue& RuntimeContext::get_numa_local_queue(int numa_id)
        {
             auto it = impl_->numa_aware_queues.find(numa_id);
             if (it != impl_->numa_aware_queues.end() && !it->second.empty())
             {
                 // TODO: implement round-robin or least-loaded strategy for queue selection.
                 return it->second[0];
             }
             
             SR_LOG_WARN("no queue found for numa {}, falling back to default.", numa_id);
             return get_queue(0);
        }

        /**
         * @brief returns a numa-aware usm allocator (lazy initialization).
         * 
         * creates a new allocator atomically if it doesn't exist.
         * 
         * @param numa_id target numa node id.
         * @return sushi_ptr<SushiRuntime::Memory::USMAllocator> the allocator.
         */
        sushi_ptr<SushiRuntime::Memory::USMAllocator> RuntimeContext::get_allocator(int numa_id)
        {
            if (numa_id < 0 || numa_id >= Core::MAX_NUMA_NODES) 
            {
                SR_LOG_WARN("invalid numa id {}, falling back to default.", numa_id);
                return get_allocator();
            }

            /** @brief step 1: fast path - check if exists. */
            auto* existing = impl_->numa_allocators[numa_id].load(std::memory_order_acquire);
            if (existing) 
            {
                return sushi_ptr<SushiRuntime::Memory::USMAllocator>(existing);
            }

            /** @brief step 2: slow path - create and try atomic swap. */
            SR_LOG_DEBUG("initializing allocator for numa {} (potential race)", numa_id);
            sycl::queue& q = get_numa_local_queue(numa_id);
            auto new_alloc_sushi = SushiRuntime::Memory::create_allocator(q, numa_id);
            auto* new_alloc_raw = new_alloc_sushi.get();

            new_alloc_raw->add_ref();

            SushiRuntime::Memory::USMAllocator* expected = nullptr;
            
            /** @brief step 3: compare-and-swap (cas). */
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