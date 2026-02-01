#pragma once

#include <cstdint>
#include "../core/sushi_ptr.h"
#include "event.h"

namespace SushiBLAS 
{
    class Device;

    enum class BlockStatus : uint8_t 
    {
        FREE,
        ALLOCATED,
        PENDING
    };

    struct MemoryBlock
    {
        void* ptr = nullptr;       // Data başlangıcı
        size_t size = 0;           // Data boyutu
        size_t alignment = 64;     // İstenen hizalama
        size_t header_offset = 0;  // Data'dan geriye kaç byte gidilmeli?
        int32_t device_id = -1;
        
        Device* owner_device = nullptr;

        BlockStatus status = BlockStatus::FREE;
        sushi_ptr<Event> ready_event = nullptr;
        const char* tag = nullptr;
        MemoryBlock* next = nullptr;

        MemoryBlock(void* p, size_t s, size_t align, size_t offset, int32_t dev_id) 
            : ptr(p), size(s), alignment(align), header_offset(offset), device_id(dev_id) {}

        ~MemoryBlock();

        inline bool is_ready() const 
        {
            if (status == BlockStatus::FREE) return true;
            return (status == BlockStatus::PENDING && ready_event && ready_event->is_completed());
        }

        static MemoryBlock* from_ptr(void* ptr) 
        {
            if (!ptr) return nullptr;
            return *(reinterpret_cast<MemoryBlock**>(ptr) - 1);
        }
    };
}