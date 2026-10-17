# adapter tests

## test_diffusion_backend.cpp

The file tests the adapter example's backend: a uniform field stays uniform, and the status names.

| Test | What it proves |
| --- | --- |
| `AUniformFieldStaysUniform` | A uniform field read back after ten steps keeps its value within an absolute tolerance. Skipped under ThreadSanitizer, whose device sanitizer aborts the kernel. |
| `EveryCodeHasAName` | Checks the name of every status code. |
