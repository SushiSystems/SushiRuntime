#include <gtest/gtest.h>
#include "../src/device/device_manager.h"
#include "../src/device/cuda_device.h"
#include <vector>
#include <numeric>

using namespace SushiBLAS;

class CUDATest : public ::testing::Test {
protected:
    void SetUp() override {
        DeviceManager::instance().probe_devices();
    }
};

TEST_F(CUDATest, BasicMemoryCopy) {
    auto& manager = DeviceManager::instance();
    bool cuda_found = false;
    sushi_ptr<Device> cuda_dev = nullptr;

    for (int i = 0; i < manager.get_count(); ++i) {
        auto dev = manager.get_device(i);
        if (std::string(dev->get_name()).find("NVIDIA") != std::string::npos || 
            std::string(dev->get_name()).find("GTX") != std::string::npos ||
            std::string(dev->get_name()).find("RTX") != std::string::npos) {
            cuda_dev = dev;
            cuda_found = true;
            break;
        }
    }

    if (!cuda_found) {
        GTEST_SKIP() << "CUDA device not found, skipping test.";
        return;
    }

    std::cout << "[INFO] Testing CUDA Device: " << cuda_dev->get_name() << " (ID: " << cuda_dev->get_id() << ")" << std::endl;

    constexpr size_t N = 1024 * 1024; // 1M elements
    constexpr size_t size = N * sizeof(float);

    std::vector<float> h_in(N);
    std::iota(h_in.begin(), h_in.end(), 1.0f);
    std::vector<float> h_out(N, 0.0f);

    auto stream = cuda_dev->get_default_stream();
    ASSERT_NE(stream, nullptr);

    // 1. Allocate
    void* d_ptr = cuda_dev->allocate(size);
    ASSERT_NE(d_ptr, nullptr);

    // 2. H2D
    cuda_dev->copy_h2d(d_ptr, h_in.data(), size, stream.get());

    // 3. D2H
    cuda_dev->copy_d2h(h_out.data(), d_ptr, size, stream.get());

    // 4. Synchronize
    cuda_dev->synchronize();

    // 5. Verify
    for (size_t i = 0; i < N; ++i) {
        if (h_in[i] != h_out[i]) {
            FAIL() << "Data mismatch at index " << i << ": expected " << h_in[i] << ", got " << h_out[i];
        }
    }

    // 6. Deallocate
    cuda_dev->deallocate(d_ptr, nullptr, "cuda_test_cleanup");
    
    std::cout << "[INFO] CUDA BasicMemoryCopy passed." << std::endl;
}

TEST_F(CUDATest, MultistreamStress) {
    auto& manager = DeviceManager::instance();
    sushi_ptr<Device> cuda_dev = nullptr;

    for (int i = 0; i < manager.get_count(); ++i) {
        if (manager.get_device(i)->get_id() >= 0) { // CUDA devices have ID >= 0
            cuda_dev = manager.get_device(i);
            break;
        }
    }

    if (!cuda_dev) {
        GTEST_SKIP() << "CUDA device not found.";
        return;
    }

    constexpr int num_streams = 4;
    constexpr size_t N = 256 * 1024;
    constexpr size_t size = N * sizeof(int);

    std::vector<sushi_ptr<Stream>> streams;
    std::vector<void*> d_ptrs;
    std::vector<std::vector<int>> h_buffers(num_streams, std::vector<int>(N));

    for (int i = 0; i < num_streams; ++i) {
        streams.push_back(cuda_dev->create_stream());
        d_ptrs.push_back(cuda_dev->allocate(size));
        std::fill(h_buffers[i].begin(), h_buffers[i].end(), i + 100);
    }

    // Async copies on different streams
    for (int i = 0; i < num_streams; ++i) {
        cuda_dev->copy_h2d(d_ptrs[i], h_buffers[i].data(), size, streams[i].get());
    }

    cuda_dev->synchronize();

    // Clean up
    for (int i = 0; i < num_streams; ++i) {
        cuda_dev->deallocate(d_ptrs[i], nullptr, "cleanup");
    }

    std::cout << "[INFO] CUDA MultistreamStress passed." << std::endl;
}
