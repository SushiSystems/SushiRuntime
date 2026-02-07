#include <cstdint>

namespace SushiBLAS {
namespace CPU {
namespace Kernels {

/**
 * @brief Reference 4x4 Micro-kernel for SGEMM
 * Calculates C[4x4] += A[4xK] * B[Kx4]
 * 
 * In a real OpenBLAS implementation, this would be written in assembly or using SIMD intrinsics.
 */
void sgemm_kernel_4x4(int64_t K, float alpha, 
                      const float* A, int64_t lda,
                      const float* B, int64_t ldb,
                      float* C, int64_t ldc) 
{
    // Accumulate into registers (simulated here by variables)
    float c[4][4] = {0};

    for (int64_t k = 0; k < K; ++k) {
        // Load A column and B row for the micro-tile
        float a0 = A[0 * lda + k];
        float a1 = A[1 * lda + k];
        float a2 = A[2 * lda + k];
        float a3 = A[3 * lda + k];

        float b0 = B[k * ldb + 0];
        float b1 = B[k * ldb + 1];
        float b2 = B[k * ldb + 2];
        float b3 = B[k * ldb + 3];

        // Rank-1 update
        c[0][0] += a0 * b0; c[0][1] += a0 * b1; c[0][2] += a0 * b2; c[0][3] += a0 * b3;
        c[1][0] += a1 * b0; c[1][1] += a1 * b1; c[1][2] += a1 * b2; c[1][3] += a1 * b3;
        c[2][0] += a2 * b0; c[2][1] += a2 * b1; c[2][2] += a2 * b2; c[2][3] += a2 * b3;
        c[3][0] += a3 * b0; c[3][1] += a3 * b1; c[3][2] += a3 * b2; c[3][3] += a3 * b3;
    }

    // Write-back to C
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            C[i * ldc + j] += alpha * c[i][j];
        }
    }
}

} // namespace Kernels
} // namespace CPU
} // namespace SushiBLAS
