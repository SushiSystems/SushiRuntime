#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <random>
#include <atomic>
#include <chrono>

#include "../src/core/tensor.h"
#include "../src/device/device_manager.h"

using namespace SushiBLAS;

class TensorStressTest : public ::testing::Test {
protected:
    void SetUp() override {
        DeviceManager::instance().probe_devices();
    }
};

TEST_F(TensorStressTest, MultithreadedTensorLifecycle) {
    int num_threads = 12;
    int iterations = 100; // Azaltılmış iterasyon
    std::atomic<int> error_count{0};

    auto& manager = DeviceManager::instance();
    int dev_count = manager.get_count();

    auto stress_worker = [&](int tid) {
        std::mt19937 rng( tid + 42 );
        
        for (int i = 0; i < iterations; ++i) {
            try {
                int dev_idx = rng() % dev_count;
                auto dev = manager.get_device(dev_idx);
                int64_t d1 = 100;
                int64_t d2 = 100;
                size_t num_elements = static_cast<size_t>(d1 * d2);

                auto storage = make_sushi<Storage>(dev, num_elements * sizeof(float));
                Tensor t(storage, {d1, d2});

                std::vector<float> host_data(num_elements, static_cast<float>(tid ^ i));
                auto stream = dev->get_default_stream();

                dev->copy_h2d(t.data(), host_data.data(), num_elements * sizeof(float), stream.get());
                dev->synchronize();

                if (d1 > 5) {
                    Tensor sub = t.slice(0, 0, 5);
                }

                std::vector<float> verify_data(num_elements, 0.0f);
                dev->copy_d2h(verify_data.data(), t.data(), num_elements * sizeof(float), stream.get());
                dev->synchronize();

                if (verify_data[0] != host_data[0]) {
                    error_count++;
                }

            } catch (const std::exception& e) {
                std::cerr << tid << ": " << e.what() << std::endl;
                error_count++;
            }
        }
    };

    std::vector<std::thread> workers;
    for (int i = 0; i < num_threads; ++i) workers.emplace_back(stress_worker, i);
    for (auto& w : workers) w.join();

    EXPECT_EQ(error_count.load(), 0);
}

TEST_F(TensorStressTest, SharedStorageRefCounting) {
    auto dev = DeviceManager::instance().get_device(0);
    constexpr size_t N = 1000;
    auto storage = make_sushi<Storage>(dev, N * sizeof(float));
    Tensor root(storage, { (int64_t)N });
    
    std::atomic<bool> stop{false};
    auto reader = [&]() {
        while(!stop) {
            Tensor view = root.slice(0, 0, N / 2);
            std::this_thread::yield();
        }
    };

    std::vector<std::thread> threads;
    for(int i=0; i<12; ++i) threads.emplace_back(reader);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop = true;
    for(auto& t : threads) t.join();

    EXPECT_GT(storage->get_ref_count(), 0);
}
