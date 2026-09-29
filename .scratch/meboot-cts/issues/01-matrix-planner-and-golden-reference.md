# 01: Matrix Planner and Golden Reference Verification

**What to build:**
A matrix planner that takes parameters for $N=2^{16}$ ($n=2^{15}$ slots) and computes the exact four-stage $[4, 4, 3, 4]$ diagonal vectors and twiddle factors for the CtS linear transform. Also includes an automated verification script that exports the reference diagonal matrices from the Go `paper1-meboot` library into a test asset, with a unit test asserting element-by-element equality between C++ generated matrices and the reference asset.

**Blocked by:** None (can start immediately)

**Status:** completed

- [x] Go export utility extracts diagonal polynomial coefficients and Galois rotation indices for $[4, 4, 3, 4]$ from `paper1-meboot` into a JSON/binary test asset
- [x] C++ `MEBootMatrixPlanner` generates the diagonal matrices and BSGS rotation supports for all four stages $[4, 4, 3, 4]$ with dense bases $(64, 8)$
- [x] Unit test in `unittest/MEBootMatrixPlannerTest.cpp` passes, confirming identical diagonals, signs, and rotation indices between C++ output and exported reference data
