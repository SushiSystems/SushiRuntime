#include <gtest/gtest.h>
#include "../../src/core/tensor.h"
#include "../../src/device/device_manager.h"

using namespace SushiBLAS;

class TensorTest : public ::testing::TestWithParam<int> 
{
protected:
    void SetUp() override 
    {
        DeviceManager::instance().probe_devices();
    }
};

TEST_P(TensorTest, CreationAndMetadata) 
{
    int dev_id = GetParam();
    auto dev = DeviceManager::instance().get_device(dev_id);
    size_t size = 10 * 10 * sizeof(float);
    auto storage = make_sushi<Storage>(dev, size);
    
    Tensor t(storage, {10, 10});
    
    EXPECT_EQ(t.rank, 2);
    EXPECT_EQ(t.num_elements, 100);
    EXPECT_EQ(t.shape[0], 10);
    EXPECT_EQ(t.shape[1], 10);
    EXPECT_EQ(t.strides[0], 10);
    EXPECT_EQ(t.strides[1], 1);
    EXPECT_TRUE(t.is_contiguous());
}

TEST_P(TensorTest, Slicing) 
{
    int dev_id = GetParam();
    auto dev = DeviceManager::instance().get_device(dev_id);
    auto storage = make_sushi<Storage>(dev, 100 * sizeof(float));
    Tensor t(storage, {10, 10});

    // Slice row 2
    auto row2 = t.slice(0, 2, 3);
    EXPECT_EQ(row2.shape[0], 1);
    EXPECT_EQ(row2.shape[1], 10);
    EXPECT_EQ(row2.storage_offset, 2 * 10);
    
    // Slice column 5
    auto col5 = t.slice(1, 5, 6);
    EXPECT_EQ(col5.shape[0], 10);
    EXPECT_EQ(col5.shape[1], 1);
    EXPECT_EQ(col5.storage_offset, 5);
}

TEST_P(TensorTest, Transpose) 
{
    int dev_id = GetParam();
    auto dev = DeviceManager::instance().get_device(dev_id);
    auto storage = make_sushi<Storage>(dev, 100 * sizeof(float));
    Tensor t(storage, {10, 5});

    auto transposed = t.transpose(0, 1);
    EXPECT_EQ(transposed.shape[0], 5);
    EXPECT_EQ(transposed.shape[1], 10);
    EXPECT_EQ(transposed.strides[0], 1);
    EXPECT_EQ(transposed.strides[1], 5);
    EXPECT_FALSE(transposed.is_contiguous());
}

TEST_P(TensorTest, Reshape) 
{
    int dev_id = GetParam();
    auto dev = DeviceManager::instance().get_device(dev_id);
    auto storage = make_sushi<Storage>(dev, 100 * sizeof(float));
    Tensor t(storage, {10, 10});

    auto reshaped = t.reshape({2, 50});
    EXPECT_EQ(reshaped.shape[0], 2);
    EXPECT_EQ(reshaped.shape[1], 50);
    EXPECT_TRUE(reshaped.is_contiguous());
    
    // Non-contiguous reshape should fail
    auto transposed = t.transpose(0, 1);
    EXPECT_THROW(transposed.reshape({100}), std::runtime_error);
}

static std::vector<int> GetActiveDeviceIndices() 
{
    DeviceManager::instance().probe_devices();
    std::vector<int> indices;
    for (int i = 0; i < DeviceManager::instance().get_count(); ++i) 
    {
        indices.push_back(i);
    }
    return indices;
}

INSTANTIATE_TEST_SUITE_P(
    AllDevices,
    TensorTest,
    ::testing::ValuesIn(GetActiveDeviceIndices())
);
