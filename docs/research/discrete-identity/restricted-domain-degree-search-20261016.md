# Restricted-domain six-bit degree search

The MEBOOT entry profile has `N'=8192`, subring secret weight `H=40`, and
`K=20`. Under the centered Q0 lift and centered residual assumptions used by
the domain audit,

```
|I| <= 21
```

The previous degree-95 manifest was fitted on the complete six-bit
representation domain `I=-32..31`. That domain is conservative: it proves
the extractor for every six-bit signed center, while the HW40/K20
deterministic domain only requires `I=-21..21`.

The generator and exact-rational certificate now accept an explicit symmetric
`integer_domain`. The generator fits the nonnegative half and relies on the
odd remainder relation

```
P_j(-x) = 1 - P_j(x)
```

to cover the negative half. The certificate expands this to every signed
cell and checks the same input radius `2^-12`, normalized to `2^-17` in the
polynomial argument.

## Search results

The following candidates use the current sampled-minimax formulation,
Chebyshev basis, 17 points per cell, coefficient bound 1024, and the exact
binary64 continuum certificate. The acceptance threshold is `1/512` for
every extracted bit.

| Domain | Degree | Worst certified bit error | Result |
|---|---:|---:|---|
| `[-21,21]` | 47 | `5.0000003039e-1` | fail |
| `[-21,21]` | 63 | `5.0000030912e-1` | fail |
| `[-21,21]` | 79 | `4.5223603738e-1` | fail |
| `[-21,21]` | 95, current coefficients | `2.6508472621e-4` | pass |

The failing rows are not close to the required error. The degree-47 and
degree-63 fits leave the lowest bit near an unresolved half-scale transition;
degree-79 improves other bits but still fails the lowest bit. Therefore
reducing the current extractor to six multiplication levels requires a
different approximation construction or a different normalization/domain
contract. It is not valid to truncate the degree-95 coefficient rows.

The current runtime remains bound to the degree-95 profile. The restricted
domain is now represented in the research manifest and certificate, but the
runtime keeps the full-domain degree-95 coefficients until a lower-degree
profile passes both the exact certificate and the encrypted 32768-slot
identity gate.

## Reproducibility

```text
python tools/research/generate_discrete_extractor.py \
  --width 6 --degree 47 --max-abs-integer 21 \
  --output build/research/discrete_direct6_k21_d47.json

python tools/research/certify_discrete_extractor.py \
  --manifest build/research/discrete_direct6_k21_d47.json
```

The current degree-95 restricted-domain manifest is
`parameters/research/discrete_direct6_k21_d95.json`; its certificate is
`build/research/discrete_direct6_k21_d95-cert.json`. The original full-domain
manifest remains unchanged.
