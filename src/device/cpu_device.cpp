#include <thread>
#include <cstdlib>
#include <cstring>
#include <intrin.h>

#include "../stream/cpu_stream.h"
#include "../event/cpu_event.h"
#include "cpu_device.h"

#ifdef _WIN32

    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>

#elif defined(__linux__) || defined(__unix__)

    #include <unistd.h>
    #include <cpuid.h>

#elif defined(__APPLE__)

    #include <sys/sysctl.h>

#endif

namespace SushiBLAS 
{
    CPUDevice::CPUDevice() : Device(-1) 
    {
        cached_info_ = get_hardware_info();
        default_stream_ = create_stream();
    }

    CPUDevice::~CPUDevice() 
    {
        synchronize();
    }

    // TODO: Multi-CPU and NUMA awareness
    int CPUDevice::get_id() const 
    { 
        return -1; 
    }

    const char* CPUDevice::get_name() const 
    { 
        return cached_info_.device_name.c_str(); 
    }

    sushi_ptr<Stream> CPUDevice::create_stream() 
    { 
        return make_sushi<CPUStream>(); 
    }

    sushi_ptr<Stream> CPUDevice::get_default_stream() 
    { 
        return default_stream_; 
    }

    sushi_ptr<Event> CPUDevice::create_event(const char* name) 
    { 
        return make_sushi<CPUEvent>(name); 
    }

    void CPUDevice::synchronize() 
    { 
        // CPU operations are already synchronous.
    }

    void CPUDevice::copy_h2d(void* dst, const void* src, size_t size, Stream* s) 
    { 
        std::memcpy(dst, src, size); 
    }

    void CPUDevice::copy_d2h(void* dst, const void* src, size_t size, Stream* s) 
    { 
        std::memcpy(dst, src, size); 
    }

    void CPUDevice::copy_d2d(void* dst, Device* src_dev, const void* src, size_t size, Stream* s) 
    { 
        std::memcpy(dst, src, size); 
    }

    void* CPUDevice::allocate_raw(size_t size) 
    {
        #if defined(_MSC_VER) || defined(__MINGW32__)
            return _aligned_malloc(size, CACHE_LINE_SIZE);
        #else

            void* ptr = nullptr;
            if (posix_memalign(&ptr, CACHE_LINE_SIZE, size) != 0) return nullptr;
            return ptr;

        #endif
    }

    void CPUDevice::free_raw(void* ptr) 
    {
        #if defined(_MSC_VER) || defined(__MINGW32__)
            _aligned_free(ptr);
        #else
            std::free(ptr);
        #endif
    }

    HardwareInfo CPUDevice::get_hardware_info() const
    {
        if (!cached_info_.device_name.empty() && cached_info_.device_name != "CPU (Generic)") 
        {
            return cached_info_;
        }

        HardwareInfo info;
        info.device_name = "CPU (Generic)"; 
        info.is_cuda = false;
        
        #if defined(__x86_64__) || defined(_M_X64) || defined(i386) || defined(_M_IX86)
            #if defined(_MSC_VER)

                int regs[4] = { 0 };
                char brand[0x41] = { 0 };
                __cpuid(regs, 0x80000000);
                if ((unsigned int)regs[0] >= 0x80000004) 
                {
                    __cpuid((int*)(brand), 0x80000002);
                    __cpuid((int*)(brand + 16), 0x80000003);
                    __cpuid((int*)(brand + 32), 0x80000004);
                    info.device_name = brand;
                }

            #elif defined(__GNUC__) || defined(__clang__)

                unsigned int regs[4];
                char brand[0x41] = { 0 };
                __get_cpuid(0x80000000, &regs[0], &regs[1], &regs[2], &regs[3]);
                if (regs[0] >= 0x80000004) 
                {
                    __get_cpuid(0x80000002, (unsigned int*)(brand),    (unsigned int*)(brand+4), (unsigned int*)(brand+8), (unsigned int*)(brand+12));
                    __get_cpuid(0x80000003, (unsigned int*)(brand+16), (unsigned int*)(brand+20), (unsigned int*)(brand+24), (unsigned int*)(brand+28));
                    __get_cpuid(0x80000004, (unsigned int*)(brand+32), (unsigned int*)(brand+36), (unsigned int*)(brand+40), (unsigned int*)(brand+44));
                    info.device_name = brand;
                }

            #endif
        #endif

        #if defined(_WIN32)

            MEMORYSTATUSEX status;
            status.dwLength = sizeof(status);
            if (GlobalMemoryStatusEx(&status)) 
                info.total_global_memory = status.ullTotalPhys;

        #elif defined(__linux__) || defined(__unix__)

            long pages = sysconf(_SC_PHYS_PAGES);
            long page_size = sysconf(_SC_PAGE_SIZE);
            if (pages != -1 && page_size != -1) 
                info.total_global_memory = (size_t)pages * page_size;

        #elif defined(__APPLE__)

            int mib[2] = { CTL_HW, HW_MEMSIZE };
            size_t mem;
            size_t len = sizeof(mem);
            if (sysctl(mib, 2, &mem, &len, NULL, 0) == 0) 
                info.total_global_memory = mem;

        #endif

        info.core_count = std::thread::hardware_concurrency();
        info.max_threads_per_block = info.core_count; 

        return info;
    }

} // namespace SushiBLAS