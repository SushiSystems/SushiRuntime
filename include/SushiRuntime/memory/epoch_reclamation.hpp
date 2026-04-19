/**************************************************************************/
/* epoch_reclamation.hpp                                                  */
/**************************************************************************/
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

            struct EpochThreadLocalState 
            {
                int id = -1;
                RetiredNode* retire_list = nullptr;
                size_t retire_count = 0;
                // Eşik değerini senin isteğin üzerine 128 yaptık, bu CPU'yu rahatlatacaktır.
                static constexpr size_t RETIRE_THRESHOLD = 128; 
                
                ~EpochThreadLocalState(); 
            };

            inline EpochThreadLocalState& get_tls_state() 
            {
                thread_local EpochThreadLocalState state{};
                return state;
            }
        }

        class EpochManager 
        {
        private:
            static constexpr size_t MAX_EPOCH_THREADS = Core::MAX_WORKER_THREADS + 32;
            
            std::atomic<uint64_t> global_epoch_{1};
            
            // MULTI-THREAD DARBOĞAZ ÇÖZÜMÜ: False Sharing (Sahte Paylaşım) Engellendi.
            // Her thread'in verisi 64-byte aralıklarla RAM'e diziliyor. L1/L2 Cache Miss tarihe karıştı!
            struct alignas(Core::CACHE_LINE_SIZE) ThreadEpochSlot 
            {
                std::atomic<uint64_t> epoch{0};
                std::atomic<bool> active{false};
            };
            
            ThreadEpochSlot thread_slots_[MAX_EPOCH_THREADS];
            
            std::atomic<int> thread_id_high_water_{0};
            std::atomic<Detail::RetiredNode*> global_orphan_list_{nullptr};

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
                        delete to_delete;
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

            inline void enter() 
            {
                auto& tls_state = Detail::get_tls_state();
                if (tls_state.id == -1) tls_state.id = register_thread();
                uint64_t current = global_epoch_.load(std::memory_order_acquire);
                thread_slots_[tls_state.id].epoch.store(current, std::memory_order_release);
            }

            inline void exit() 
            {
                auto& tls_state = Detail::get_tls_state();
                if (tls_state.id != -1) 
                {
                    thread_slots_[tls_state.id].epoch.store(0, std::memory_order_release);
                }
            }

            template<typename T>
            void retire(T* ptr) 
            {
                if (!ptr) return;
                auto& tls_state = Detail::get_tls_state();
                if (tls_state.id == -1) tls_state.id = register_thread();

                Detail::RetiredNode* node = new Detail::RetiredNode{
                    ptr,
                    [](void* p) { delete static_cast<T*>(p); }, 
                    global_epoch_.load(std::memory_order_acquire),
                    tls_state.retire_list
                };
                
                tls_state.retire_list = node;
                tls_state.retire_count++;

                if (tls_state.retire_count >= Detail::EpochThreadLocalState::RETIRE_THRESHOLD) 
                    check_and_reclaim();
            }

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

        // DERLEME HATASINA NEDEN OLAN EKSİK KISIM EKLENDİ
        struct EpochGuard 
        {
            EpochGuard() { EpochManager::instance().enter(); }
            ~EpochGuard() { EpochManager::instance().exit(); }
            EpochGuard(const EpochGuard&) = delete;
            EpochGuard& operator=(const EpochGuard&) = delete;
        };

        // YENİ: Thread Local State yok edilirken devir teslim
        inline Detail::EpochThreadLocalState::~EpochThreadLocalState() 
        {
            if (id != -1) 
            {
                EpochManager::instance().unregister_thread(id, retire_list);
                id = -1;
                retire_list = nullptr;
                retire_count = 0;
            }
        }

    } // namespace Memory
} // namespace SushiRuntime