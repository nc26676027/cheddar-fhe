# 02: LogN15 Parameter and Matrix Planning

**What to build:**
Enable $\log N = 15$ ($N = 32,768$, complex slot count $M = 16,384$) across Cheddar's parameter system, matrix planning seam, and linear transform infrastructure. Generalize slot-count assumptions in `MEBootMatrixPlanner` and `LinearTransform` so that baby-step/giant-step automorphism rotations for widths $[3, 3, 4, 4]$ and bases $[64, 4]$ can be planned and generated on GPU without hardcoding $\log N = 16$.

**Blocked by:** 01: Export Func15 and CNN fixtures

**Status:** completed

- [x] `Parameter<word>` supports `log_degree = 15` without throwing validation exceptions.
- [x] `BootParameter::Func15RBoot()` factory method added with the certified Q19/P2 primes, entry primes, and subring parameters.
- [x] `MEBootMatrixPlanner` computes valid automorphism supports for $\log M = 14$ and widths $[3, 3, 4, 4]$ (summing to 14).
- [x] `GenerateElementaryCtSStages` and `GenerateElementaryStCStages` correctly construct matrix diagonals for 16,384 slots.
- [x] New unit test `parameter_func15_test` passes, verifying parameter creation, Q/P chain consistency, and rotation support validity.
- [x] Existing $\log N = 16$ `Identity19` and `DiscreteCKKSMeBoot` parameter tests continue passing without regression.
