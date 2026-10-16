# Remez performance measurements

RTX 5060 Ti, CUDA 12.8, WSL, Release. Each row has three fresh
keysets, five warmups, 30 checked timed calls per keyset, and five
separate instrumented profiles. Output scale is 2^40.

| Configuration | Output | GPU median ms | GPU p95 ms | Host median ms | Worst bits | Pool peak GiB |
|---|---|---:|---:|---:|---:|---:|
| d95 | L19 | 574.786 | 590.096 | 573.547 | 18.9118 | 13.6417 |
| d63_auto8 | L20 | 708.332 | 722.078 | 704.399 | 18.7777 | 13.5694 |
| selected_b16 | L20 | 658.013 | 684.842 | 652.621 | 18.7960 | 13.6251 |

Allocator peaks exclude total device/driver usage. Evaluation-key arrays:
10497294336 bytes (9.7764 GiB), unchanged.

| Instrumented stage | d95 GPU median ms | selected b16 GPU median ms |
|---|---:|---:|
| Accurate bypass and alignment | 59.416 | 59.691 |
| Cleaning, both channels | 132.250 | 139.431 |
| DigitExtract, both channels | 274.904 | 352.980 |
| Integer recombination | 2.513 | 2.718 |
| Lift and sparse-to-dense | 4.656 | 4.726 |
| MEBOOT low CtS | 77.948 | 78.062 |
| Split and affine alignment | 4.460 | 4.461 |
| StC and entry | 20.477 | 19.629 |
| Subtract and normalize | 0.533 | 0.569 |
| Temporary release | 0.001 | 0.001 |

Grouped stage medians need not sum to the total median.
The initial d95/auto8 runs use the initial binary; selected b16 uses
the final binary with an explicit baby-step option. Default legacy
decomposition is preserved; binary hashes are recorded separately.

An additional fresh d95 process on the final binary checks that comparison:
30 calls, GPU median 544.238 ms, p95 544.454 ms, worst 18.9670 bits.

An adjacent fresh selected process on that same final binary:
30 calls, GPU median 620.154 ms, p95 620.611 ms, worst 18.9418 bits.
Adjacent final-build GPU latency change: +13.95%.
The earlier d95 median shifted by roughly 5% at final confirmation;
use the adjacent final-build pair for the final latency comparison.

Some CUDA-event intervals exceed host-clock intervals; both are retained.
No speedup is claimed: the selected route saves one level but has higher latency.
