#include "../../../src/core/tensor.h"

namespace SushiBLAS {
namespace CPU {
namespace Ref {

/**
 * @brief Modern Tensor-based Reference SGEMM
 * C = alpha * (A @ B) + beta * C
 */
void sgemm(float alpha, const Tensor& A, const Tensor& B,
           float beta, Tensor& C) 
{
    // Basic Rank & Dimension Validation
    SB_THROW_IF(A.rank != 2 || B.rank != 2 || C.rank != 2, 
                "SGEMM: Reference implementation only supports Rank-2 tensors");
    
    SB_THROW_IF(A.shape[1] != B.shape[0], 
                "SGEMM: Inner dimension mismatch. A:{}x{}, B:{}x{}", 
                A.shape[0], A.shape[1], B.shape[0], B.shape[1]);

    int64_t M = A.shape[0];
    int64_t K = A.shape[1];
    int64_t N = B.shape[1];

    // Leading dimensions from strides
    int64_t lda = A.strides[0];
    int64_t ldb = B.strides[0];
    int64_t ldc = C.strides[0];

    const float* a_ptr = A.data();
    const float* b_ptr = B.data();
    float* c_ptr = C.data();

    for (int64_t i = 0; i < M; ++i) {
        for (int64_t j = 0; j < N; ++j) {
            float sum = 0.0f;
            for (int64_t k = 0; k < K; ++k) {
                sum += a_ptr[i * lda + k] * b_ptr[k * ldb + j];
            }
            
            if (beta == 0.0f) {
                c_ptr[i * ldc + j] = alpha * sum;
            } else {
                c_ptr[i * ldc + j] = alpha * sum + beta * c_ptr[i * ldc + j];
            }
        }
    }
}

} // namespace Ref
} // namespace CPU
} // namespace SushiBLAS
