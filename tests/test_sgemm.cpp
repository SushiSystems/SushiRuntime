#include <gtest/gtest.h>
#include <vector>
#include <cmath>

#include "../src/core/tensor.h"
#include "../src/device/device_manager.h"

namespace SushiBLAS {
namespace CPU {
namespace Ref {
    void sgemm(float alpha, const Tensor& A, const Tensor& B,
               float beta, Tensor& C);
}
}
}

using namespace SushiBLAS;

class SGEMMTensorTest : public ::testing::Test {
protected:
    void SetUp() override {
        DeviceManager::instance().probe_devices();
    }

    sushi_ptr<Device> cpu_dev() {
        return DeviceManager::instance().get_device(0);
    }
};

TEST_F(SGEMMTensorTest, TensorBasedAccuracy) {
    auto dev = cpu_dev();
    
    // 3x2 matrix A
    auto storageA = make_sushi<Storage>(dev, 6 * sizeof(float));
    Tensor A(storageA, {3, 2});
    float* dataA = A.data();
    for(int i=0; i<6; ++i) dataA[i] = static_cast<float>(i + 1); // 1, 2, 3, 4, 5, 6
    
    // 2x3 matrix B
    auto storageB = make_sushi<Storage>(dev, 6 * sizeof(float));
    Tensor B(storageB, {2, 3});
    float* dataB = B.data();
    // 7, 8, 9, 1, 2, 3
    dataB[0]=7; dataB[1]=8; dataB[2]=9;
    dataB[3]=1; dataB[4]=2; dataB[5]=3;
    
    // 3x3 matrix C
    auto storageC = make_sushi<Storage>(dev, 9 * sizeof(float));
    Tensor C(storageC, {3, 3});
    std::fill(C.data(), C.data() + 9, 0.0f);

    std::vector<float> expected = {9, 12, 15, 25, 32, 39, 41, 52, 63};

    // A(3,2) * B(2,3) = C(3,3)
    SushiBLAS::CPU::Ref::sgemm(1.0f, A, B, 0.0f, C);

    float* res = C.data();
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_NEAR(res[i], expected[i], 1e-5);
    }
}

TEST_F(SGEMMTensorTest, TensorAlphaBetaTest) {
    auto dev = cpu_dev();

    auto storageA = make_sushi<Storage>(dev, 4 * sizeof(float));
    Tensor A(storageA, {2, 2});
    A.data()[0]=1; A.data()[1]=0; A.data()[2]=0; A.data()[3]=1;

    auto storageB = make_sushi<Storage>(dev, 4 * sizeof(float));
    Tensor B(storageB, {2, 2});
    B.data()[0]=2; B.data()[1]=3; B.data()[2]=4; B.data()[3]=5;

    auto storageC = make_sushi<Storage>(dev, 4 * sizeof(float));
    Tensor C(storageC, {2, 2});
    std::fill(C.data(), C.data() + 4, 10.0f);

    std::vector<float> expected = {21.0f, 21.5f, 22.0f, 22.5f};

    // C = 0.5 * (A*B) + 2.0 * C
    SushiBLAS::CPU::Ref::sgemm(0.5f, A, B, 2.0f, C);

    float* res = C.data();
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_NEAR(res[i], expected[i], 1e-5);
    }
}
