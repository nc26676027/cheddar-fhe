# 01: Unified Linear Transformation Module

**What to build:**
Collapse the shallow `LinearTransform` pass-through wrapper (`include/extension/LinearTransform.h`, `src/extension/LinearTransform.cpp`) and `HoistHandler` (`include/extension/Hoist.h`, `src/extension/Hoist.cu`) into a single deep `LinearTransform<word>` evaluator module (`include/extension/LinearTransform.h`, `src/extension/LinearTransform.cu`). Hide `PlainHoistMap`, BSGS stride deduction, baby-step/giant-step partitioning, and double-hoisted plaintext encoding behind the `LinearTransform` evaluation seam.

**Blocked by:** None

**Status:** completed

- [x] Merge `HoistHandler<word>` implementation and CUDA kernels (`BSFusedKernel`, `GSFusedKernel`) into `LinearTransform<word>` (`src/extension/LinearTransform.cu`) and delete `include/extension/Hoist.h`, `src/extension/Hoist.cu`, and `src/extension/LinearTransform.cpp`
- [x] Support automatic BSGS partitioning constructor `LinearTransform(context, matrix, pt_level, pt_scale)` and context-retaining `Evaluate(res, input, evk_map, min_ks)`
- [x] Add `unittest/LinearTransformTest.cpp` verifying linear transformations across automatic BSGS, explicit BSGS (`min_ks` and standard), and dense-base BSGS in 32-bit and 64-bit
