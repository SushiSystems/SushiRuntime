#pragma once

#include <mutex>
#include <atomic>
#include <cassert>
#include <unordered_map>
#include <algorithm>

#include "../common/logging/logger.h"
#include "../common/common.h"
#include "memory_block.h"

#ifdef _MSC_VER
    #include <intrin.h>
#endif

namespace SushiBLAS 
{
    class MemoryPool : public RefCounted 
    {
        private:
            // AŞAMA 2.1 FIX: LocalPool yapısı
            struct alignas(CACHE_LINE_SIZE) LocalPool 
            {
                MemoryBlock* bin_free_lists[MAX_BINS]{};
                MemoryBlock* pending_head = nullptr;
                MemoryBlock* pending_tail = nullptr;

                bool is_processing = false;
                MemoryBlock* deferred_free_head = nullptr;
                
                MemoryPool* owner_pool = nullptr;

                // Constructor: Owner pool'u kaydet
                LocalPool(MemoryPool* p) : owner_pool(p) {}
                LocalPool() : owner_pool(nullptr) {}

                // Destructor: Thread kapanırken cache'i global havuza boşalt
                ~LocalPool() 
                {
                    if (owner_pool) 
                    {
                        owner_pool->flush_cache_to_global(this);

                        pending_head = pending_tail = nullptr;
                        deferred_free_head = nullptr;
                    }
                }
                
                // Move-only types for map safety (optional but good practice)
                LocalPool(const LocalPool&) = delete;
                LocalPool& operator=(const LocalPool&) = delete;
                LocalPool(LocalPool&&) = default; // Map insertion için gerekli olabilir (C++17+)
            };

            // AŞAMA 2.1 FIX: Thread Local Map
            // Sabit array yerine map kullanarak pool_id çakışmasını %100 önlüyoruz.
            // "inline static" -> C++17 ODR compliant.
            inline static thread_local std::unordered_map<MemoryPool*, LocalPool> thread_caches;

            int32_t device_id_; // AŞAMA 2.2 FIX: Invariant check için gerekli
            std::mutex global_mutex;
            MemoryBlock* global_bins[MAX_BINS]{};

            inline int get_bin_idx(size_t size) const 
            {
                if (size <= 1024) return 0;

                #ifdef _MSC_VER
                    unsigned long index;
                    _BitScanReverse64(&index, size - 1);
                    int idx = static_cast<int>(index - 9); 
                #else
                    int idx = 63 - __builtin_clzll(size - 1) - 9;
                #endif

                return (idx < 0) ? 0 : (idx >= MAX_BINS ? MAX_BINS - 1 : idx);
            }

            // Yardımcı Fonksiyon: LocalPool'daki blokları Global'e taşı (Thread Exit veya Overflow)
            void flush_cache_to_global(LocalPool* lp)
            {
                std::lock_guard<std::mutex> lock(global_mutex);

                for (int i = 0; i < MAX_BINS; ++i)
                {
                    MemoryBlock* curr = lp->bin_free_lists[i];
                    while (curr)
                    {
                        MemoryBlock* next = curr->next;
                        
                        // AŞAMA 2.2 FIX: Invariant Checks
                        SB_ASSERT(curr->status == BlockStatus::FREE, "MemoryPool: Flushing non-free block!");
                        SB_ASSERT(curr->device_id == this->device_id_, "MemoryPool: Device ID Mismatch during flush!");
                        
                        // Global listeye ekle (Stack push)
                        curr->next = global_bins[i];
                        global_bins[i] = curr;
                        
                        curr = next;
                    }
                    lp->bin_free_lists[i] = nullptr;
                }
                
                // Pending listesindeki bloklar ne olacak?
                // Thread ölüyorsa pending eventleri bekleyecek kimse kalmaz. 
                // Ancak eventler global event-queue'da yaşıyorsa sorun yok. 
                // Şimdilik pending listesini orphan bırakıyoruz veya logluyoruz.
                // İdealde: Pending bloklar da orphan listesine alınmalı.
                if (lp->pending_head)
                {
                    SB_LOG_WARN("MemoryPool: Thread exiting with pending blocks. Potential leak if events never trigger.");
                }

                SB_ASSERT(lp->pending_head == nullptr, "MemoryPool: Pending blocks during thread exit");
                SB_ASSERT(lp->deferred_free_head == nullptr, "MemoryPool: Deferred blocks during thread exit");
            }

            // Thread-local instance'a güvenli erişim
            LocalPool& get_local_pool() 
            {
                auto it = thread_caches.find(this);
                if (it == thread_caches.end()) 
                {
                    // Map içinde in-place construct (emplace_hint optimized)
                    auto result = thread_caches.emplace(
                        std::piecewise_construct,
                        std::forward_as_tuple(this),
                        std::forward_as_tuple(this)
                    );
                    return result.first->second;
                }
                return it->second;
            }

        public:
            // AŞAMA 2.2: Device ID zorunlu
            MemoryPool(int32_t dev_id) : device_id_(dev_id) {}
            
            // Map kullandığımız için destructor'da map'ten silme yapmıyoruz 
            // (Thread local map'ler thread'e aittir, pool ölürse map entry'leri dangling kalabilir mi?
            //  Evet, bu bir risk. Ancak MemoryPool genelde app lifetime'dır. 
            //  Daha kompleks bir kayıt mekanizması Aşama 3 sonrası düşünülebilir.)
            ~MemoryPool() override 
            {
                // AŞAMA 2.4 FIX: Adres çakışmasını önlemek için yerel cache'i temizle
                // Bu işlem sadece bu havuzu yok eden thread için geçerlidir.
                // Diğer thread'lerdeki girişler, o thread'ler bir sonraki allocate/deallocate 
                // çağrısında 'this' adresini kontrol ettiğinde zaten (teorik olarak) yeni bir 
                // giriş gibi davranacaktır veya dangling kalacaktır.
                thread_caches.erase(this);

                // Global binleri temizle
                std::lock_guard<std::mutex> lock(global_mutex);
                for(int i = 0; i < MAX_BINS; ++i)
                {
                    global_bins[i] = nullptr;
                }
            }

            MemoryBlock* allocate(size_t size) 
            {
                int bin_idx = get_bin_idx(size);
                LocalPool& lp = get_local_pool();

                process_local_pending(lp);

                // 1. Local Cache Hit
                if (lp.bin_free_lists[bin_idx]) 
                {
                    MemoryBlock* b = lp.bin_free_lists[bin_idx];
                    
                    // Sanity Check
                    SB_ASSERT(b->size >= size, "MemoryPool: Bin logic error");

                    lp.bin_free_lists[bin_idx] = b->next;
                    b->status = BlockStatus::ALLOCATED;
                    b->next = nullptr;
                    return b;
                }

                // 2. Global Bin'den Çal
                {
                    std::lock_guard<std::mutex> lock(global_mutex);

                    if (global_bins[bin_idx]) 
                    {
                        MemoryBlock* b = global_bins[bin_idx];
                        
                        // AŞAMA 2.2 FIX: Invariant Check
                        SB_ASSERT(b->device_id == this->device_id_, "MemoryPool: Global bin contamination!");

                        global_bins[bin_idx] = b->next;
                        b->status = BlockStatus::ALLOCATED;
                        b->next = nullptr;
                        return b;
                    }
                }

                return nullptr; // Allocate new from OS (Device handles this)
            }

            void deallocate(MemoryBlock* block, sushi_ptr<Event> ready_event, const char* tag) 
            {
                SB_THROW_IF(block->status == BlockStatus::FREE, "MemoryPool: Double free detected for block at {}", block->ptr);
                
                // Device ID check (Extra safety)
                SB_ASSERT(block->device_id == this->device_id_, "MemoryPool: Deallocating block from wrong device");

                LocalPool& lp = get_local_pool();
                block->tag = tag;

                // Reentrancy Guard
                if (lp.is_processing) 
                {
                    block->next = lp.deferred_free_head;
                    lp.deferred_free_head = block;
                    return;
                }

                if (ready_event) 
                {
                    block->status = BlockStatus::PENDING;
                    block->ready_event = ready_event;
                    block->next = nullptr;
                    
                    if (!lp.pending_tail) 
                    {
                        lp.pending_head = lp.pending_tail = block;  
                    } 
                    else 
                    {
                        lp.pending_tail->next = block;
                        lp.pending_tail = block;
                    }
                } 
                else 
                {
                    return_to_local_bin(lp, block);
                }
            }

        private:
            void return_to_local_bin(LocalPool& lp, MemoryBlock* b) 
            {
                int b_idx = get_bin_idx(b->size);
                b->status = BlockStatus::FREE;
                b->ready_event = nullptr;
                b->next = lp.bin_free_lists[b_idx];
                lp.bin_free_lists[b_idx] = b;
            }

            void process_local_pending(LocalPool& lp) 
            {
                lp.is_processing = true;

                MemoryBlock* prev = nullptr;
                MemoryBlock* curr = lp.pending_head;

                while (curr) 
                {
                    // AŞAMA 2.3 FIX: is_completed() artık thread-safe (CPUEvent::completed atomic oldu)
                    if (curr->is_ready()) 
                    {
                        MemoryBlock* next_node = curr->next;
                        
                        if (prev) 
                            prev->next = next_node;
                        else 
                            lp.pending_head = next_node;

                        if (curr == lp.pending_tail) 
                            lp.pending_tail = prev;

                        return_to_local_bin(lp, curr);
                        curr = next_node;
                    }
                    else 
                    {
                        prev = curr;
                        curr = curr->next;
                    }
                }

                lp.is_processing = false;

                // Deferred işlemleri yap
                while (lp.deferred_free_head) 
                {
                    MemoryBlock* b = lp.deferred_free_head;
                    lp.deferred_free_head = b->next;
                    deallocate(b, b->ready_event, b->tag);
                }
            }
    };
    
} // namespace SushiBLAS