# Measured Discrete depth/scaling comparison

Three fresh processes per configuration; five warmups and thirty
unobserved, precision-checked calls per process. RTX 5060 Ti,
WSL Ubuntu, CUDA 12.8, Release SM120. Key generation and decrypt
are outside the timers. Output scale is 2^40 in every row.

| Configuration | Output | GPU median ms | GPU p95 ms | Host median ms | Worst slot bits | Pool peak GiB |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| baseline | L7 | 343.282 | 343.720 | 338.317 | 18.8195 | 13.5567 |
| depth18 | L18 | 524.597 | 525.182 | 517.885 | 18.8501 | 13.6129 |
| latency9 | L9 | 360.904 | 361.341 | 357.331 | 18.8770 | 13.5587 |

Pool peaks are tracked allocator bytes, not total device usage.
Resident evaluation-key arrays: 10497294336 bytes
(9.7764 GiB), unchanged.

| Instrumented stage | L18 median GPU ms | L9 median GPU ms |
| --- | ---: | ---: |
| Accurate bypass and alignment | 55.794 | 55.638 |
| Cleaning, both channels | 118.381 | 61.247 |
| DigitExtract, both channels | 246.014 | 141.655 |
| Integer recombination | 2.336 | 0.791 |
| Lift and sparse-to-dense | 4.642 | 4.638 |
| MEBOOT low CtS | 72.803 | 72.943 |
| Split and affine alignment | 5.216 | 4.675 |
| StC and entry | 19.333 | 19.362 |
| Subtract and normalize | 0.500 | 0.263 |
| Temporary release | 0.001 | 0.001 |

Stage rows aggregate fifteen instrumented profiles per new configuration.
They include event-boundary overhead and must be distinguished from
the unobserved calls in the first table. Group medians need not sum
to the median total. Extraction and cleaning include both channels
and all six digits. Lift and sparse-to-dense are grouped because
the existing observer emits their two marks after both operations.

Some CUDA-event intervals exceed host steady-clock intervals. Both clocks are preserved; no small speedup is inferred from their difference.

These configurations return different output levels; the comparison
does not establish an equal-level speedup over native Identity19.
