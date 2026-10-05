/****************************************************************/
/* epoch_reclamation.hpp                                        */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file epoch_reclamation.hpp
 * @brief Declares EpochManager, the epoch-based memory reclamation manager.
 * @author Mustafa Garip
 * @see include/SushiRuntime/memory/README.md
 */

#pragma once

#include <atomic>
#include <stdexcept>
#include <SushiRuntime/core/common.hpp>

namespace SushiRuntime 
{
    namespace Memory 
    {
        namespace Detail 
        {
            struct RetiredNode 
            {
                void* ptr;
                void (*deleter)(void*);
                uint64_t epoch;
                RetiredNode* next;
            };

            /**
             * @brief Holds the thread-local state for epoch-based reclamation.
             * @see include/SushiRuntime/memory/README.md
             */
            struct EpochThreadLocalState 
            {
                int id = -1;

                /**
                 * @brief Counts the re-entrancy depth of enter()/exit() on this thread.
                 * @see include/SushiRuntime/memory/README.md
                 */
                uint32_t nest_depth = 0;

                RetiredNode* retire_list = nullptr;
                size_t retire_count = 0;
                static constexpr size_t RETIRE_THRESHOLD = 128; 

                /** @brief Maximum pooled RetiredNodes per thread. */
                static constexpr size_t FREE_POOL_CAP = 512;

                /** @brief Singly-linked freelist of reusable RetiredNode storage. */
                RetiredNode* free_node_pool = nullptr;
                size_t free_node_pool_count = 0;

                /**
                 * @brief Acquires a RetiredNode, preferring the pool over heap.
                 *
                 * Pop is O(1) and does not touch the global allocator. Falls
                 * back to operator new only when the pool is empty.
                 */
                inline RetiredNode* acquire_node() 
                {
                    if (free_node_pool) 
                    {
                        RetiredNode* node = free_node_pool;
                        free_node_pool = node->next;
                        --free_node_pool_count;
                        return node;
                    }
                    return new RetiredNode;
                }

                /**
                 * @brief Releases a RetiredNode back to the pool, or to heap if full.
                 *
                 * The caller has already invoked the deleter on the underlying
                 * payload. This call only manages the RetiredNode storage itself.
                 */
                inline void release_node(RetiredNode* node) noexcept 
                {
                    if (free_node_pool_count < FREE_POOL_CAP) 
                    {
                        node->next = free_node_pool;
                        free_node_pool = node;
                        ++free_node_pool_count;
                    } 
                    else 
                    {
                        delete node;
                    }
                }

                /**
                 * @brief Drains the free pool entirely. Called from the destructor.
                 */
                inline void drain_free_pool() noexcept 
                {
                    while (free_node_pool) 
                    {
                        RetiredNode* next = free_node_pool->next;
                        delete free_node_pool;
                        free_node_pool = next;
                    }
                    free_node_pool_count = 0;
                }
                
                ~EpochThreadLocalState(); 
            };

            inline EpochThreadLocalState& get_tls_state() 
            {
                thread_local EpochThreadLocalState state{};
                return state;
            }
        }

        /**
         * @brief Manages epoch-based reclamation through a global epoch and per-thread slots.
         * @see include/SushiRuntime/memory/README.md
         */
        class EpochManager
        {
        private:
            static constexpr size_t MAX_EPOCH_THREADS = Core::MAX_WORKER_THREADS + 32;

            std::atomic<uint64_t> global_epoch_{1};

            /**
             * @brief Holds one thread's epoch on its own cache line, so stores do not false-share.
             */
            struct alignas(Core::CACHE_LINE_SIZE) ThreadEpochSlot
            {
                std::atomic<uint64_t> epoch{0};
                std::atomic<bool> active{false};
            };
            
            ThreadEpochSlot thread_slots_[MAX_EPOCH_THREADS];
            
            std::atomic<int> thread_id_high_water_{0};
            std::atomic<Detail::RetiredNode*> global_orphan_list_{nullptr};

            /**
             * @brief Registers the calling thread and returns its slot id.
             * @return Slot index in [0, MAX_EPOCH_THREADS).
             * @throws std::runtime_error if all slots are occupied.
             */
            int register_thread()
            {
                for (int i = 0; i < static_cast<int>(MAX_EPOCH_THREADS); ++i) 
                {
                    bool expected = false;
                    if (thread_slots_[i].active.compare_exchange_strong(expected, true, std::memory_order_acquire)) 
                    {
                        thread_slots_[i].epoch.store(0, std::memory_order_relaxed);
                        int current_max = thread_id_high_water_.load(std::memory_order_relaxed);
                        while (i >= current_max) 
                        {
                            if (thread_id_high_water_.compare_exchange_weak(current_max, i + 1, std::memory_order_release)) 
                                break;
                        }
                        return i;
                    }
                }
                throw std::runtime_error("EBR: Maximum thread limit reached.");
            }

            /**
             * @brief Attempts to advance the global epoch and frees any safely
             *        retired objects whose epoch is no longer reachable.
             */
            void check_and_reclaim()
            {
                auto& tls_state = Detail::get_tls_state();
                
                Detail::RetiredNode* orphans = global_orphan_list_.exchange(nullptr, std::memory_order_acquire);
                if (orphans) 
                {
                    Detail::RetiredNode* tail = orphans;
                    size_t orphan_count = 1;
                    while (tail->next) { tail = tail->next; orphan_count++; }
                    tail->next = tls_state.retire_list;
                    tls_state.retire_list = orphans;
                    tls_state.retire_count += orphan_count;
                }

                uint64_t current = global_epoch_.load(std::memory_order_acquire);
                uint64_t min_active = current;
                int active_threads = thread_id_high_water_.load(std::memory_order_relaxed);
                
                for (int i = 0; i < active_threads; ++i) 
                {
                    if (thread_slots_[i].active.load(std::memory_order_acquire)) 
                    {
                        uint64_t t_ep = thread_slots_[i].epoch.load(std::memory_order_acquire);
                        if (t_ep != 0 && t_ep < min_active) 
                            min_active = t_ep;
                    }
                }

                if (min_active == current) 
                {
                    uint64_t expected = current;
                    global_epoch_.compare_exchange_strong(expected, current + 1, std::memory_order_release);
                }

                Detail::RetiredNode* prev = nullptr;
                Detail::RetiredNode* curr = tls_state.retire_list;
                
                while (curr) 
                {
                    if (curr->epoch < min_active) 
                    {
                        curr->deleter(curr->ptr); 
                        Detail::RetiredNode* to_delete = curr;
                        curr = curr->next;
                        if (prev) prev->next = curr;
                        else tls_state.retire_list = curr;
                        // The RetiredNode storage returns to the thread-local pool.
                        tls_state.release_node(to_delete);
                        tls_state.retire_count--;
                    } 
                    else 
                    {
                        prev = curr;
                        curr = curr->next;
                    }
                }
            }

        public:
            EpochManager() 
            {
                for (size_t i = 0; i < MAX_EPOCH_THREADS; ++i) 
                {
                    thread_slots_[i].epoch.store(0, std::memory_order_relaxed);
                    thread_slots_[i].active.store(false, std::memory_order_relaxed);
                }
            }

            static EpochManager& instance() 
            {
                static EpochManager inst;
                return inst;
            }

            /**
             * @brief Enters a read-side critical section.
             *
             * Stamps the calling thread's slot with the current global epoch so
             * concurrent retire() calls know the thread is active and cannot
             * reclaim objects from this epoch yet.
             */
            inline void enter()
            {
                auto& tls_state = Detail::get_tls_state();
                if (tls_state.id == -1) tls_state.id = register_thread();

                // Only the outermost enter() publishes the slot.
                if (tls_state.nest_depth++ > 0)
                    return;

                uint64_t current = global_epoch_.load(std::memory_order_acquire);
                thread_slots_[tls_state.id].epoch.store(current, std::memory_order_release);
                // The fence orders the slot publication before any later load.
                std::atomic_thread_fence(std::memory_order_seq_cst);
            }

            /**
             * @brief Exits the read-side critical section.
             *
             * Clears the calling thread's epoch slot (stores 0), signalling that
             * it holds no live references and reclamation of older epochs may proceed.
             */
            inline void exit()
            {
                auto& tls_state = Detail::get_tls_state();
                if (tls_state.id == -1) return;

                // Only the outermost exit() clears the slot; an unmatched exit() is ignored.
                if (tls_state.nest_depth == 0) return;
                if (--tls_state.nest_depth > 0) return;

                thread_slots_[tls_state.id].epoch.store(0, std::memory_order_release);
            }

            template<typename T>
            void retire(T* ptr) 
            {
                if (!ptr) return;
                auto& tls_state = Detail::get_tls_state();
                if (tls_state.id == -1) tls_state.id = register_thread();

                // A RetiredNode comes from the thread-local pool, or operator new if it is empty.
                Detail::RetiredNode* node = tls_state.acquire_node();
                node->ptr = ptr;
                node->deleter = [](void* p) { delete static_cast<T*>(p); };
                node->epoch = global_epoch_.load(std::memory_order_acquire);
                node->next = tls_state.retire_list;
                
                tls_state.retire_list = node;
                tls_state.retire_count++;

                if (tls_state.retire_count >= Detail::EpochThreadLocalState::RETIRE_THRESHOLD) 
                    check_and_reclaim();
            }

            /**
             * @brief Deregisters a thread slot and hands its retire list to the global orphan list.
             * @param id Slot id returned by register_thread().
             * @param remaining_nodes Head of the thread's unreclaimed retire list, or null.
             * @see include/SushiRuntime/memory/README.md
             */
            void unregister_thread(int id, Detail::RetiredNode* remaining_nodes)
            {
                if (id < 0 || id >= static_cast<int>(MAX_EPOCH_THREADS)) return;

                thread_slots_[id].epoch.store(0, std::memory_order_release);
                
                if (remaining_nodes) 
                {
                    Detail::RetiredNode* tail = remaining_nodes;
                    while (tail->next) tail = tail->next;
                    
                    Detail::RetiredNode* old_head = global_orphan_list_.load(std::memory_order_relaxed);
                    do {
                        tail->next = old_head;
                    } while (!global_orphan_list_.compare_exchange_weak(old_head, remaining_nodes, 
                                                                      std::memory_order_release, 
                                                                      std::memory_order_relaxed));
                }
                
                thread_slots_[id].active.store(false, std::memory_order_release);
            }
        };

        /**
         * @brief RAII guard that enters the EBR critical section on construction and exits on destruction.
         */
        struct EpochGuard
        {
            EpochGuard() { EpochManager::instance().enter(); }
            ~EpochGuard() { EpochManager::instance().exit(); }
            EpochGuard(const EpochGuard&) = delete;
            EpochGuard& operator=(const EpochGuard&) = delete;
        };

        inline Detail::EpochThreadLocalState::~EpochThreadLocalState()
        {
            if (id != -1) 
            {
                EpochManager::instance().unregister_thread(id, retire_list);
                id = -1;
                retire_list = nullptr;
                retire_count = 0;
            }
            // Free pool storage belongs to this thread alone and is released directly.
            drain_free_pool();
        }

    } // namespace Memory
} // namespace SushiRuntime