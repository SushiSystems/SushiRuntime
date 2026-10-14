# adapter tests

## test_diffusion_backend.cpp

The file tests the adapter example's backend: a uniform field stays uniform, and the status names.

| Test | What it proves |
| --- | --- |
| `AUniformFieldStaysUniform` | Holds the tolerance of a diffused value, absolute; the field is uniform. */ constexpr float K_TOLERANCE = 1e-5f; /** @brief Tests that a uniform field read back after ten steps is still the same value. |
| `EveryCodeHasAName` | Checks the name of every status code. |
