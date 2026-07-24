/**************************************************************************/
/* usm_allocator.cpp                                                      */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#include <vector>
#include <algorithm>
#include <cstdint>
#include <shared_mutex>
#include <sycl/sycl.hpp>
#include <SushiRuntime/memory/epoch_reclamation.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/memory_pool.hpp>
#include <SushiRuntime/core/logger.hpp>

namespace SushiRuntime
{
    namespace Memory
    {
        using namespace SushiRuntime::Core;

        /**
         * @brief Global allocator ID counter.
         * Used to assign a unique ID to each allocator instance.
         */
        static std::atomic<uint64_t> g_allocator_id_counter{1};

        /**
         * @brief Global flush callback pointer.
         * Used to notify the system when a magazine is flushed.
         */
        std::atomic<FlushCallback> g_flush_callback{nullptr};

        class USMManagerImpl;

        /**
         * @brief Registry of all active USM managers.
         * Indexed by allocator ID modulo 256.
         */
        static std::atomic<USMManagerImpl*> g_allocator_registry[256];

        /**
         * @brief Serializes magazine flushes against allocator teardown.
         *
         * Flushes (global_flush_magazine, from any thread including a dying
         * thread's magazine) take it shared; an allocator's destructor takes it
         * exclusive while it nulls its registry slot and frees its pages. This
         * guarantees no flush touches an allocator's pages while they are being
         * freed, and any flush that starts after teardown sees a null registry
         * slot and becomes a no-op — so a thread that outlives its RuntimeContext
         * can still exit cleanly.
         */
        static std::shared_timed_mutex g_flush_mutex;

        /**
         * @brief Global callback function to flush a magazine batch.
         * Routes the flush request to the appropriate allocator instance.
         * @param alloc_id The ID of the allocator.
         * @param bucket_idx The index of the size bucket.
         * @param ptrs Array of pointers to flush.
         * @param count Number of pointers in the array.
         */
        void global_flush_magazine(uint64_t alloc_id, int bucket_idx, void** ptrs, size_t count);

        /**
         * @brief Per-thread pool slot ID. Assigned once on first allocation.
         * Eliminates cross-thread contention on pool expansion entirely.
         */
        static std::atomic<int> g_pool_slot_counter{0};
        thread_local int t_pool_slot{-1};

        /**
         * @brief Lock-Free 3-Level Radix Tree (PageMap) for O(1) pointer-to-page resolution.
         * Modeled after TCMalloc. Replaces all global locks and binary searches.
         * Maps a 50-bit virtual address space down to 4KB granular chunks.
         */
        class LockFreePageMap 
        {
        private:
            static constexpr size_t PAGE_SHIFT = 12; // 4KB Page Granularity
            
            // 3-Level Trie constraints covering 38 bits of PageID (50 bits of VMA)
            static constexpr size_t L1_BITS = 14; 
            static constexpr size_t L2_BITS = 12;
            static constexpr size_t L3_BITS = 12;

            static constexpr size_t L1_SIZE = 1ULL << L1_BITS; // 16384
            static constexpr size_t L2_SIZE = 1ULL << L2_BITS; // 4096
            static constexpr size_t L3_SIZE = 1ULL << L3_BITS; // 4096

            struct Leaf 
            {
                std::atomic<MemoryPage*> entries[L3_SIZE];
                Leaf() { for (auto& e : entries) e.store(nullptr, std::memory_order_relaxed); }
            };

            struct Node 
            {
                std::atomic<Leaf*> entries[L2_SIZE];
                Node() { for (auto& e : entries) e.store(nullptr, std::memory_order_relaxed); }
            };

            // Root array is statically allocated but highly cache-friendly (131 KB)
            std::atomic<Node*> root_[L1_SIZE];

        public:
            LockFreePageMap() 
            {
                for (auto& e : root_) e.store(nullptr, std::memory_order_relaxed);
            }

            ~LockFreePageMap() 
            {
                for (size_t i = 0; i < L1_SIZE; ++i) 
                {
                    Node* n = root_[i].load(std::memory_order_relaxed);
                    if (n) 
                    {
                        for (size_t j = 0; j < L2_SIZE; ++j) 
                        {
                            Leaf* l = n->entries[j].load(std::memory_order_relaxed);
                            if (l) delete l;
                        }
                        delete n;
                    }
                }
            }

            /**
             * @brief Registers a memory span into the Radix Tree.
             * Allocates trie nodes dynamically and lock-free via CAS.
             * @param ptr Pointer to the start of the memory span.
             * @param size Size of the memory span in bytes.
             * @param page Pointer to the MemoryPage to associate with the span.
             */
            void set(void* ptr, size_t size, MemoryPage* page) 
            {
                uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
                uintptr_t end = start + size;
                
                uintptr_t start_page = start >> PAGE_SHIFT;
                uintptr_t end_page = (end - 1) >> PAGE_SHIFT;

                for (uintptr_t p = start_page; p <= end_page; ++p) 
                {
                    uintptr_t i1 = (p >> (L2_BITS + L3_BITS)) & (L1_SIZE - 1);
                    uintptr_t i2 = (p >> L3_BITS) & (L2_SIZE - 1);
                    uintptr_t i3 = p & (L3_SIZE - 1);

                    Node* node = root_[i1].load(std::memory_order_acquire);
                    if (!node) 
                    {
                        Node* new_node = new Node();
                        Node* expected = nullptr;
                        if (root_[i1].compare_exchange_strong(expected, new_node, 
                                                              std::memory_order_acq_rel, 
                                                              std::memory_order_acquire))
                            node = new_node; 
                        else 
                        {
                            delete new_node;
                            node = expected;
                        }
                    }

                    Leaf* leaf = node->entries[i2].load(std::memory_order_acquire);
                    if (!leaf) 
                    {
                        Leaf* new_leaf = new Leaf();
                        Leaf* expected = nullptr;
                        if (node->entries[i2].compare_exchange_strong(expected, new_leaf, 
                                                                      std::memory_order_acq_rel, 
                                                                      std::memory_order_acquire))
                            leaf = new_leaf;
                        else 
                        {
                            delete new_leaf;
                            leaf = expected;
                        }
                    }

                    // Map the exact 4KB chunk to our MemoryPage.
                    leaf->entries[i3].store(page, std::memory_order_release);
                }
            }

            /**
             * @brief Ultra-fast, wait-free, lock-free pointer resolution. O(1).
             * Resolves a given pointer to its associated MemoryPage.
             * @param ptr Pointer to resolve.
             * @return Pointer to the associated MemoryPage, or nullptr if not found.
             */
            MemoryPage* get(void* ptr) const 
            {
                uintptr_t p = reinterpret_cast<uintptr_t>(ptr) >> PAGE_SHIFT;
                
                uintptr_t i1 = (p >> (L2_BITS + L3_BITS)) & (L1_SIZE - 1);
                Node* node = root_[i1].load(std::memory_order_acquire);
                if (!node) return nullptr;

                uintptr_t i2 = (p >> L3_BITS) & (L2_SIZE - 1);
                Leaf* leaf = node->entries[i2].load(std::memory_order_acquire);
                if (!leaf) return nullptr;

                uintptr_t i3 = p & (L3_SIZE - 1);
                return leaf->entries[i3].load(std::memory_order_acquire);
            }
        };

        class USMManagerImpl : public USMAllocator 
        {
            public:
                /**
                 * @brief Constructs a new USMManagerImpl.
                 * Initializes the manager with the given SYCL queue and NUMA ID.
                 * @param q The SYCL queue to associate with the allocator.
                 * @param numa_id The NUMA node ID to bind allocations to.
                 */
                explicit USMManagerImpl(sycl::queue& q, int numa_id) 
                    : queue_(q), numa_id_(numa_id), allocator_id_(g_allocator_id_counter.fetch_add(1, std::memory_order_relaxed)) 
                {
                    g_allocator_registry[allocator_id_ % 256].store(this, std::memory_order_release);
                    
                    FlushCallback expected = nullptr;
                    g_flush_callback.compare_exchange_strong(expected, global_flush_magazine);

                    // thread_slots_[i] kendi ThreadPoolSlot() constructor'ında başlatılır.
                    // Per-thread model sayesinde warmup çağrısına gerek yok:
                    // her thread ilk allocate() çağrısında kendi slotunu expand eder.
                }

                /**
                 * @brief Destroys the USMManagerImpl.
                 * Cleans up all resources, drains remote-free queues, and releases SYCL allocations.
                 */
                ~USMManagerImpl() override
                {
                    // Hold the flush lock exclusively for the whole teardown: it
                    // blocks until any in-flight magazine flush targeting this
                    // allocator completes, and makes the registry-null below
                    // visible before pages are freed, so no flush can touch a
                    // freed page. A flush that starts after this sees the null slot.
                    std::unique_lock<std::shared_timed_mutex> teardown_lock(g_flush_mutex);

                    // Only clear the slot if it still points to US. Allocator ids
                    // collide modulo 256, so a longer-lived allocator sharing this
                    // slot could have overwritten our registration; an unconditional
                    // store(nullptr) would then unregister IT, stranding its
                    // remote-frees and leaking its pool. compare_exchange leaves a
                    // slot owned by someone else untouched.
                    USMManagerImpl* self = this;
                    g_allocator_registry[allocator_id_ % 256].compare_exchange_strong(
                        self, nullptr, std::memory_order_release, std::memory_order_relaxed);

                    // Drain any remote-free queues into the page bitmasks before
                    // releasing the underlying SYCL allocations. Skipping this
                    // would leak pointers that are still on a remote-free stack.
                    for (int t = 0; t < MAX_THREAD_SLOTS; ++t)
                    {
                        for (int i = 0; i < NUM_BUCKETS; ++i)
                        {
                            void* head = thread_slots_[t].remote_free_.heads[i].exchange(
                                nullptr, std::memory_order_acquire);
                            while (head)
                            {
                                void* next = *static_cast<void**>(head);
                                MemoryPage* owner = page_map_.get(head);
                                if (owner)
                                    pool_.deallocate_to_page(*owner, head);
                                head = next;
                            }
                        }
                    }

                    // Tüm thread slotlarındaki sayfaları temizle
                    for (int t = 0; t < MAX_THREAD_SLOTS; ++t)
                    {
                        for (int i = 0; i < NUM_BUCKETS; ++i)
                        {
                            MemoryPage* curr = thread_slots_[t].root_pages[i].load(std::memory_order_relaxed);
                            while (curr)
                            {
                                MemoryPage* next = curr->next.load(std::memory_order_relaxed);
                                if (curr->start_ptr)
                                    sycl::free(curr->start_ptr, queue_);
                                delete curr;
                                curr = next;
                            }
                        }
                    }
                }

                /**
                 * @brief Flushes a batch of pointers back to their owner pages.
                 * Resolves the owner page for each pointer and deallocates it.
                 * @param bucket_idx The index of the size bucket (unused in this implementation).
                 * @param ptrs Array of pointers to flush.
                 * @param count Number of pointers in the array.
                 */
                void flush_batch(int /*bucket_idx*/, void** ptrs, size_t count)
                {
                    for (size_t i = 0; i < count; ++i)
                    {
                        MemoryPage* owner = page_map_.get(ptrs[i]);
                        if (owner)
                            pool_.deallocate_to_page(*owner, ptrs[i]);
                    }
                }

                /**
                 * @brief Allocate a single object of the requested size.
                 *
                 * Hot path for poolable allocations:
                 *   1. Pop from the magazine (zero-atomic fast path). Serves a
                 *      full BATCH_SIZE run of allocates before missing.
                 *   2. On miss: lazily drain this slot's remote-free stack
                 *      (memory reclaimed from cross-thread frees) and retry the
                 *      pop. Draining here rather than on every allocate amortizes
                 *      the contended atomics over the magazine batch.
                 *   3. Still empty: bulk_claim_capped from this slot's pages,
                 *      sized to the magazine's free capacity so no slots wasted.
                 *   4. On page exhaustion: expand this slot's pages by
                 *      requesting more memory from SYCL.
                 *   5. Fallback: route directly to the strategy's raw allocator
                 *      for non-poolable strategies or oversized requests.
                 * @param bytes Number of bytes to allocate.
                 * @param strategy The allocation strategy to use.
                 * @param alignment Alignment of the allocation.
                 * @return Pointer to the allocated memory, or nullptr on failure.
                 */
                void* allocate(size_t bytes, 
                            const IAllocatorStrategy& strategy = Strategies::Shared::instance(),
                            size_t alignment = DEFAULT_ALIGNMENT) override 
                {
                    if (strategy.is_poolable() && bytes <= MAX_POOL_ALLOC_SIZE && alignment <= DEFAULT_ALIGNMENT)
                    {
                        int bucket_idx = get_bucket_index(bytes);

                        // ── SLOT ATAMASI (thread başına bir kez) ─────────────────────────
                        // Slot assignment must precede remote-queue drain because
                        // the queue we drain is keyed by t_pool_slot.
                        if (t_pool_slot < 0)
                            t_pool_slot = g_pool_slot_counter.fetch_add(1, std::memory_order_relaxed) % MAX_THREAD_SLOTS;
                        const int slot = t_pool_slot;

                        // ── (1) HOT PATH: thread-local magazine ──────────────────────
                        // In steady state this serves a full BATCH_SIZE run of
                        // allocates with zero atomics and zero coherence traffic --
                        // the magazine acting as the batching buffer it is meant to
                        // be. Only on a miss do we touch shared state, below.
                        if (void* ptr = t_mag.pop(bucket_idx, allocator_id_))
                            return ptr;

                        // Magazine empty: refill. Prefer memory reclaimed from
                        // cross-thread frees (bounded, no new pages) over claiming
                        // fresh page slots.

                        // ── (2) LAZY DRAIN of the remote-free stack ─────────────────
                        // Draining only here (on a miss) rather than on every
                        // allocate is the key producer/consumer win: it amortizes the
                        // contended exchange and the coherence-missing head load over
                        // the whole magazine batch instead of paying them per op. The
                        // freed-block chain walk itself is unavoidable (one cross-core
                        // read per reclaimed pointer), but it now happens once per
                        // ~BATCH_SIZE allocates, not every one.
                        if (thread_slots_[slot].remote_free_.heads[bucket_idx].load(
                                std::memory_order_relaxed) != nullptr)
                        {
                            drain_remote_free_into_magazine(slot, bucket_idx);
                            if (void* ptr = t_mag.pop(bucket_idx, allocator_id_))
                                return ptr;
                        }

                        // ── (3) MEDIUM PATH: capped bulk_claim from this slot ───────
                        // Phase 1 wire-up: bulk_claim_capped sizes the claim to the
                        // magazine's free capacity. This avoids wasting slots when
                        // the magazine could only absorb a few of the page's 64
                        // available pointers.
                        void* batch[BATCH_SIZE];
                        MemoryPage* root = thread_slots_[slot].root_pages[bucket_idx].load(std::memory_order_acquire);
                        size_t cap = t_mag.free_capacity(bucket_idx, allocator_id_);
                        if (cap > BATCH_SIZE) cap = BATCH_SIZE;
                        int claimed = pool_.bulk_claim_capped(root, batch, static_cast<int>(cap));

                        if (claimed > 0)
                        {
                            for (int i = 1; i < claimed; ++i)
                                t_mag.push(bucket_idx, batch[i], allocator_id_);
                            return batch[0];
                        }

                        // ── (4) SLOW PATH: expand this slot's pages ───────────────────
                        if (allocate_new_page(slot, bucket_idx))
                        {
                            root    = thread_slots_[slot].root_pages[bucket_idx].load(std::memory_order_acquire);
                            // After expansion, the magazine is still empty so we can
                            // safely take the full page worth via bulk_claim. Use the
                            // capped variant anyway for consistency; cap is unchanged.
                            cap = t_mag.free_capacity(bucket_idx, allocator_id_);
                            if (cap > BATCH_SIZE) cap = BATCH_SIZE;
                            claimed = pool_.bulk_claim_capped(root, batch, static_cast<int>(cap));

                            if (claimed > 0)
                            {
                                for (int i = 1; i < claimed; ++i)
                                    t_mag.push(bucket_idx, batch[i], allocator_id_);
                                return batch[0];
                            }
                        }
                    }

                    // ── FALLBACK: havuz kapsamı dışındaki allocasyon ─────────────────────
                    void* ptr = nullptr;
                    try 
                    {
                        ptr = strategy.allocate_raw(bytes, alignment, queue_);
                    } 
                    catch (const sycl::exception& e) 
                    {
                        SR_LOG_ERROR("SYCL allocation failed: {}", e.what());
                    }
                    return ptr;
                }

                /**
                 * @brief Deallocate a previously-allocated pointer.
                 *
                 * The dispatch is determined by which slot owns the page:
                 *
                 *  - Local free (the freeing thread's slot owns the page):
                 *    push onto the local magazine. On magazine-full, drain
                 *    a partial flush back to the page bitmask (the existing
                 *    fast path).
                 *
                 *  - Remote free (another slot owns the page): push onto the
                 *    owning slot's remote-free intrusive stack. The push is
                 *    a single CAS on the stack head, with the next-pointer
                 *    embedded in the freed memory itself (no node allocation).
                 *    The owning slot drains this stack on its next allocate,
                 *    converting the pointers back into magazine entries.
                 *
                 *  - Out-of-pool (page_map miss): straight to sycl::free.
                 *    These are oversized allocations or non-poolable strategy
                 *    allocations that bypassed the pool entirely.
                 * @param ptr Pointer to the memory to deallocate.
                 */
                void deallocate(void* ptr) override 
                {
                    if (!ptr) return;

                    MemoryPage* owner = page_map_.get(ptr);
                    
                    if (owner)
                    {
                        // Determine whether this is a local or remote free.
                        // owning_slot is set at page creation and never changes,
                        // so this read is race-free without any synchronization.
                        const int my_slot = t_pool_slot;
                        const int page_slot = owner->owning_slot;

                        if (my_slot == page_slot)
                        {
                            // ── LOCAL FREE: existing magazine fast path ────────
                            if (t_mag.push(owner->bucket_idx, ptr, allocator_id_)) return;

                            // Magazine full: do a partial drain so that the
                            // current ptr has a place to go. This is the same
                            // emergency-flush path as the original code.
                            for (int i = 0; i < 64; ++i) 
                            {
                                void* flushed_ptr = t_mag.pop(owner->bucket_idx, allocator_id_);
                                if (!flushed_ptr) break;
                                MemoryPage* flushed_owner = page_map_.get(flushed_ptr);
                                if (flushed_owner)
                                    pool_.deallocate_to_page(*flushed_owner, flushed_ptr);
                            }

                            t_mag.push(owner->bucket_idx, ptr, allocator_id_);
                            return;
                        }

                        // ── REMOTE FREE: push to owning slot's remote stack ────
                        // Treiber-stack push using the freed memory itself as
                        // the next-pointer storage. Bucket size is at least
                        // 128 bytes so 8 bytes for next is always safe.
                        push_remote_free(page_slot, owner->bucket_idx, ptr);
                        return;
                    }

                    sycl::free(ptr, queue_.get_context());
                }

                /**
                 * @brief Retrieves the SYCL device associated with a given pointer.
                 * Queries the SYCL runtime for the device owning the allocation.
                 * @param ptr Pointer to query.
                 * @return The SYCL device that owns the pointer.
                 */
                sycl::device get_device_of(void* ptr) override
                {
                    return sycl::get_pointer_device(ptr, queue_.get_context());
                }

                /**
                 * @brief Confirms this allocator owns a given id.
                 *
                 * Guards against an id%256 registry collision: a stale flush whose
                 * id maps to this slot but belongs to a since-destroyed allocator
                 * must not be applied to this allocator's pages.
                 * @param id The allocator id carried by a flush request.
                 * @return True if @p id is this allocator's id.
                 */
                bool owns_id(uint64_t id) const { return allocator_id_ == id; }

            private:
                // Stored BY VALUE, not by reference: sycl::queue is a lightweight
                // handle (shared-ownership internally), and the destructor calls
                // sycl::free(..., queue_). A reference member would dangle — and the
                // free would be a use-after-free — if the referenced queue were
                // destroyed before this allocator. A value copy keeps it alive.
                sycl::queue queue_;
                int numa_id_;
                uint64_t allocator_id_;
                BitmaskMemoryPool pool_;
                
                static constexpr int NUM_BUCKETS = 4;
                static constexpr size_t BUCKET_SIZES[NUM_BUCKETS] = {128, 256, 1024, 4096};
                static constexpr size_t MAX_POOL_ALLOC_SIZE = 4096;
                static constexpr int SLOTS_PER_PAGE = 64;  // uint64_t bitmask sınırı
                static constexpr int BATCH_SIZE = 64;
                static constexpr int PAGES_PER_EXPANSION = 8;

                /** @brief Per-allocate cap on remote-free drain to bound worst-case latency. */
                static constexpr int REMOTE_DRAIN_CAP = 256;

                // ----------------------------------------------------------------
                // Per-thread pool slots: çekişmeyi kökte keser.
                //
                // Eski mimari: root_pages_[NUM_BUCKETS] tüm thread'ler tarafından
                //   paylaşılıyordu. expansion_locks_[bucket_idx] için 12 thread yarışıyor,
                //   sycl::malloc_shared sıralı çağrılıyordu → thread artışıyla throughput düşüyordu.
                //
                // Yeni mimari: her thread kendi ThreadPoolSlot'una yazar/okur.
                //   expansion sırasında başka thread'ler beklemek zorunda değil;
                //   kendi slotlarını bağımsız olarak genişletebilirler.
                //   sycl::malloc_shared çağrıları artık paralel yapılabilir.
                //
                // Slot sayısı 64: Ryzen 7600X 12 thread'e karşı 5.3× fazladan marj.
                //
                // Phase 2: ThreadPoolSlot now also holds remote_free_head[], an
                // intrusive Treiber stack of pointers that other threads have
                // freed but which belong to pages owned by this slot. The slot
                // owner drains this stack on its next allocate; remote freers
                // pay only one CAS per free. This eliminates the producer/
                // consumer p999 spike where the consumer's magazine fills with
                // foreign pointers and triggers full-magazine flush storms.
                // ----------------------------------------------------------------
                static constexpr int MAX_THREAD_SLOTS = 64;

                struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) ThreadPoolSlot
                {
                    std::atomic<MemoryPage*> root_pages[NUM_BUCKETS];
                    std::atomic<bool>        expanding[NUM_BUCKETS];

                    /**
                     * @brief Per-bucket remote-free intrusive stack heads, on their
                     *        own cache line.
                     *
                     * Producers (other threads freeing into this slot) push onto
                     * these stacks via CAS; the next-pointer is stored in the freed
                     * memory itself. The slot owner drains the entire stack with one
                     * exchange() on its next allocate.
                     *
                     * These are written by REMOTE threads on every cross-thread free,
                     * whereas root_pages/expanding are read by the OWNER on its hot
                     * allocate path. The struct's outer alignas only aligns its start,
                     * so without a dedicated inner cache line the heads shared a line
                     * with root_pages and every remote free invalidated the owner's
                     * fast-path line (false sharing). The nested alignas isolates them.
                     */
                    struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) RemoteFreeHeads
                    {
                        std::atomic<void*> heads[NUM_BUCKETS];
                    } remote_free_;

                    ThreadPoolSlot()
                    {
                        for (int i = 0; i < NUM_BUCKETS; ++i)
                        {
                            root_pages[i].store(nullptr, std::memory_order_relaxed);
                            expanding[i].store(false,   std::memory_order_relaxed);
                            remote_free_.heads[i].store(nullptr, std::memory_order_relaxed);
                        }
                    }
                };

                ThreadPoolSlot thread_slots_[MAX_THREAD_SLOTS];

                // TCMalloc style Radix Tree: O(1) pointer → sayfa çözümlemesi
                LockFreePageMap page_map_;


                /**
                 * @brief Gets the bucket index for a given allocation size.
                 * Uses hardware-accelerated 'Count Leading Zeros' to find the bucket index in O(1).
                 * @param size The allocation size in bytes.
                 * @return The index of the corresponding size bucket.
                 */
                inline int get_bucket_index(size_t size)
                {
                    // Use hardware-accelerated 'Count Leading Zeros' to find the bucket index in O(1).
                    // (size-1)|127 ensures all sizes <= 128 map to bit 6, simplifying the LUT.
                    // The ternary handles size=0 to avoid underflow.
                    uint64_t val = (size > 0 ? static_cast<uint64_t>(size - 1) : 0) | 127;
                    int bit = 63 - __builtin_clzll(val);
                    
                    // bit 6  (size <= 128)  -> index 0
                    // bit 7  (size <= 256)  -> index 1
                    // bit 8-9 (size <= 1024) -> index 2
                    // bit 10-11 (size <= 4096) -> index 3
                    // Clamp bit to avoid out-of-bounds if size > 4096
                    int idx = bit - 6;
                    if (idx > 5) idx = 5;
                    
                    static constexpr int8_t bucket_map[] = {0, 1, 2, 2, 3, 3};
                    return bucket_map[idx];
                }

                /**
                 * @brief Push a pointer onto another slot's remote-free stack.
                 *
                 * The freed memory's first 8 bytes are repurposed as the next-
                 * pointer of the intrusive stack node. This is safe because:
                 *   1. The minimum bucket size is 128 bytes, so 8 bytes for
                 *      the next-pointer is always available.
                 *   2. The freed memory will not be read by the user again
                 *      until it is reclaimed and re-allocated, at which point
                 *      the user gets a fresh pointer with no expectation of
                 *      its prior contents.
                 *
                 * Producers (the freeing thread) only do one CAS per free.
                 * The release ordering on the CAS ensures that any writes to
                 * the freed memory by the user before deallocate() are visible
                 * to the slot owner before it observes the new head.
                 * @param slot The ID of the owning slot.
                 * @param bucket_idx The index of the size bucket.
                 * @param ptr Pointer to the memory being freed.
                 */
                inline void push_remote_free(int slot, int bucket_idx, void* ptr) noexcept
                {
                    auto& head = thread_slots_[slot].remote_free_.heads[bucket_idx];
                    void* old_head = head.load(std::memory_order_relaxed);
                    do {
                        *static_cast<void**>(ptr) = old_head;
                    } while (!head.compare_exchange_weak(
                        old_head, ptr,
                        std::memory_order_release,
                        std::memory_order_relaxed));
                }

                /**
                 * @brief Drain the slot's own remote-free stack into the magazine.
                 *
                 * Called only by the slot owner, on its own slot, so there is
                 * no contention with other consumers. Producers may continue
                 * pushing during the drain; their pushes either land on the
                 * fresh empty stack (post-exchange) or on whatever new head
                 * exists then. We process exactly the snapshot we exchanged.
                 *
                 * Capped at REMOTE_DRAIN_CAP to bound the worst case: a slot
                 * that has accumulated thousands of remote frees should not
                 * pay all of them on a single allocate.
                 * @param slot The ID of the slot being drained.
                 * @param bucket_idx The index of the size bucket.
                 */
                inline void drain_remote_free_into_magazine(int slot, int bucket_idx) noexcept
                {
                    void* head = thread_slots_[slot].remote_free_.heads[bucket_idx].exchange(
                        nullptr, std::memory_order_acquire);

                    int processed = 0;
                    while (head && processed < REMOTE_DRAIN_CAP)
                    {
                        void* next = *static_cast<void**>(head);
                        if (!t_mag.push(bucket_idx, head, allocator_id_))
                        {
                            // Magazine filled mid-drain. Re-stash the rest
                            // (including head) on this slot's own remote
                            // stack so it is picked up by the next allocate
                            // after the magazine is drained by pops.
                            requeue_remote_chain(slot, bucket_idx, head);
                            return;
                        }
                        head = next;
                        ++processed;
                    }

                    // If we hit REMOTE_DRAIN_CAP with more left, requeue the
                    // remainder so we don't lose pointers.
                    if (head)
                    {
                        requeue_remote_chain(slot, bucket_idx, head);
                    }
                }

                /**
                 * @brief Push an entire chain back onto the remote-free stack.
                 *
                 * Used when drain cannot consume the full snapshot (magazine
                 * full or drain cap reached). Walks to the tail of the chain
                 * to splice it onto whatever the current head is.
                 *
                 * O(N) in chain length; only called on overflow paths. The
                 * common case (drain succeeds entirely) does not invoke this.
                 * @param slot The ID of the owning slot.
                 * @param bucket_idx The index of the size bucket.
                 * @param chain_head Pointer to the head of the chain to requeue.
                 */
                inline void requeue_remote_chain(int slot, int bucket_idx, void* chain_head) noexcept
                {
                    if (!chain_head) return;

                    // Find tail of the chain we are putting back.
                    void* tail = chain_head;
                    while (*static_cast<void**>(tail) != nullptr)
                        tail = *static_cast<void**>(tail);

                    auto& head_atomic = thread_slots_[slot].remote_free_.heads[bucket_idx];
                    void* old_head = head_atomic.load(std::memory_order_relaxed);
                    do {
                        *static_cast<void**>(tail) = old_head;
                    } while (!head_atomic.compare_exchange_weak(
                        old_head, chain_head,
                        std::memory_order_release,
                        std::memory_order_relaxed));
                }

                /**
                 * @brief Allocates new pages from SYCL and assigns them to the given slot and bucket.
                 * Expands the capacity of the memory pool for the given thread slot.
                 * @param slot The ID of the slot to expand.
                 * @param bucket_idx The index of the size bucket.
                 * @return true if expansion was successful, false otherwise.
                 */
                bool allocate_new_page(int slot, int bucket_idx)
                {
                    size_t slot_size = BUCKET_SIZES[bucket_idx];

                    // Per-thread expanding flag: normalde başka thread bu slot'a dokunmaz.
                    // Try-lock yeterli; çekişme pratikte sıfır.
                    bool expected = false;
                    if (!thread_slots_[slot].expanding[bucket_idx].compare_exchange_strong(
                            expected, true,
                            std::memory_order_acquire,
                            std::memory_order_relaxed))
                        return false;

                    const size_t page_size = SLOTS_PER_PAGE * slot_size;
                    int pages_opened = 0;

                    for (int p = 0; p < PAGES_PER_EXPANSION; ++p)
                    {
                        void* raw = nullptr;
                        // Align the page to DEFAULT_ALIGNMENT (128). The pool accepts
                        // requests with alignment <= DEFAULT_ALIGNMENT, and blocks sit
                        // at offsets that are multiples of slot_size (128/256/1024/
                        // 4096 — all multiples of 128). A plain malloc_shared only
                        // guarantees the backend's natural alignment (often 16), so a
                        // caller asking for 64/128-byte alignment could get a 16-byte-
                        // aligned block. Aligning the page start to 128 makes every
                        // block honour the promise.
                        try { raw = sycl::aligned_alloc_shared(DEFAULT_ALIGNMENT, page_size, queue_); } catch (...) {}

                        if (!raw)
                        {
                            SR_LOG_ERROR("Pool expansion failed at page {}/{} for slot {} bucket {}",
                                         p + 1, PAGES_PER_EXPANSION, slot, bucket_idx);
                            break;
                        }

                        MemoryPage* new_page = new MemoryPage();
                        new_page->start_ptr    = raw;
                        new_page->bitmask.store(0, std::memory_order_relaxed);
                        new_page->numa_node_id = numa_id_;
                        new_page->slot_size    = slot_size;
                        new_page->bucket_idx   = bucket_idx;
                        // Permanently bind the page to its owning slot. This
                        // identity drives remote-free dispatch in deallocate().
                        new_page->owning_slot  = slot;

                        MemoryPage* old_root = thread_slots_[slot].root_pages[bucket_idx].load(std::memory_order_relaxed);
                        do
                        {
                            new_page->next.store(old_root, std::memory_order_relaxed);
                        }
                        while (!thread_slots_[slot].root_pages[bucket_idx].compare_exchange_weak(
                                   old_root, new_page,
                                   std::memory_order_release,
                                   std::memory_order_relaxed));

                        page_map_.set(raw, page_size, new_page);
                        ++pages_opened;
                    }

                    thread_slots_[slot].expanding[bucket_idx].store(false, std::memory_order_release);
                    return pages_opened > 0;
                }
        };

        void global_flush_magazine(uint64_t alloc_id, int bucket_idx, void** ptrs, size_t count)
        {
            // Shared lock: many threads may flush concurrently, but all are
            // excluded while an allocator tears down (see g_flush_mutex). Once the
            // lock is held, a destroyed allocator's slot reads null and a collided
            // slot fails owns_id, so the flush safely no-ops in both cases.
            std::shared_lock<std::shared_timed_mutex> flush_lock(g_flush_mutex);

            USMManagerImpl* mgr = g_allocator_registry[alloc_id % 256].load(std::memory_order_acquire);
            if (mgr && mgr->owns_id(alloc_id))
                mgr->flush_batch(bucket_idx, ptrs, count);
        }

        /**
         * @brief Creates a new USM allocator instance.
         * Instantiates a USMManagerImpl with the provided queue and NUMA node.
         * @param q The SYCL queue to associate with the allocator.
         * @param numa_id The NUMA node ID to bind allocations to.
         * @return A smart pointer to the created USM allocator.
         */
        sushi_ptr<USMAllocator> create_allocator(sycl::queue& q, int numa_id) 
        {
            return make_sushi<USMManagerImpl>(q, numa_id);
        }
        
    } 
}