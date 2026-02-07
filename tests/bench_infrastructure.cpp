#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <iostream>
#include <iomanip>

#include "../src/device/device_manager.h"
#include "../src/memory/memory_pool.h"

using namespace SushiBLAS;

/**
 * @brief False Sharing ve Lock Contention Analiz Testi
 */
class InfrastructureBench : public ::testing::Test {
protected:
    void SetUp() override {
        DeviceManager::instance().probe_devices();
    }
};

// 1. Lock Contention Benchmark
// Aynı cihaz üzerinde çok sayıda thread'in sürekli allocate/deallocate yapması.
// Bu test, MemoryPool global_mutex ve Device allocation_mutex üzerindeki baskıyı ölçer.
TEST_F(InfrastructureBench, LockContentionBenchmark) {
    auto dev = DeviceManager::instance().get_device(0); // CPU
    
    auto run_bench = [&](int num_threads, int iterations) {
        std::vector<std::thread> threads;
        auto start = std::chrono::high_resolution_clock::now();
        
        std::atomic<int> ready_count{0};
        std::atomic<bool> start_signal{false};

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&]() {
                ready_count++;
                while(!start_signal); // Bariyer

                for (int j = 0; j < iterations; ++j) {
                    // Local cache'i bypass etmek için her seferinde farklı bin boyutları kullanalım 
                    // ya da çok fazla allocation yapalım ki global pool'a düşsün.
                    void* p = dev->allocate(128 + (j % 16) * 64);
                    dev->deallocate(p, nullptr, "bench");
                }
            });
        }

        while(ready_count < num_threads);
        start_signal = true;

        for (auto& t : threads) t.join();
        
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    };

    std::cout << "\n[BENCHMANRK] Lock Contention (Allocation/Deallocation)" << std::endl;
    std::cout << std::setw(10) << "Threads" << std::setw(15) << "Time (ms)" << std::setw(20) << "Ops/ms" << std::endl;
    
    for (int nt : {1, 2, 4, 8, 12, 16}) {
        int iters = 10000 / nt;
        double ms = run_bench(nt, iters);
        double ops_per_ms = (nt * iters) / ms;
        std::cout << std::setw(10) << nt << std::setw(15) << std::fixed << std::setprecision(2) << ms 
                  << std::setw(20) << ops_per_ms << std::endl;
    }
}

// 2. False Sharing Simülasyonu ve Altyapı Kontrolü
// SushiBLAS içindeki yapıların (LocalPool, GlobalBin vb.) bitişik olması durumunda performansı ölçer.
TEST_F(InfrastructureBench, FalseSharingDemonstration) {
    struct alignas(64) PaddedAtomic {
        std::atomic<int64_t> val{0};
        char padding[64 - sizeof(std::atomic<int64_t>)];
    };

    struct UnpaddedAtomic {
        std::atomic<int64_t> val{0};
    };

    auto bench_unpadded = [](int num_threads, int iters) {
        std::vector<UnpaddedAtomic> counters(num_threads);
        std::vector<std::thread> threads;
        auto start = std::chrono::high_resolution_clock::now();
        
        for(int i=0; i<num_threads; ++i) {
            threads.emplace_back([&, i]() {
                for(int j=0; j<iters; ++j) counters[i].val.fetch_add(1, std::memory_order_relaxed);
            });
        }
        for(auto& t : threads) t.join();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    };

    auto bench_padded = [](int num_threads, int iters) {
        std::vector<PaddedAtomic> counters(num_threads);
        std::vector<std::thread> threads;
        auto start = std::chrono::high_resolution_clock::now();
        
        for(int i=0; i<num_threads; ++i) {
            threads.emplace_back([&, i]() {
                for(int j=0; j<iters; ++j) counters[i].val.fetch_add(1, std::memory_order_relaxed);
            });
        }
        for(auto& t : threads) t.join();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    };

    int nt = std::thread::hardware_concurrency();
    int iters = 1000000;
    
    std::cout << "\n[BENCHMARK] False Sharing Impact (Total Iters: " << nt * iters << ")" << std::endl;
    
    double ms_unpadded = bench_unpadded(nt, iters);
    std::cout << "Unpadded (Contended Cache Lines) : " << ms_unpadded << " ms" << std::endl;

    double ms_padded = bench_padded(nt, iters);
    std::cout << "Padded (Isolated Cache Lines)   : " << ms_padded << " ms" << std::endl;
    
    double overhead = (ms_unpadded / ms_padded);
    std::cout << "False Sharing Overhead Factor   : " << std::fixed << std::setprecision(2) << overhead << "x" << std::endl;

    // SushiBLAS Analizi:
    // MemoryPool::global_bins bir array'dir. Eğer çok fazla thread farklı binlere yazsaydı false sharing olurdu.
    // ANCAK biz global_mutex kullandığımız için seri erişim var, false sharing lock contention'ın arkasında gizli kalıyor.
}
