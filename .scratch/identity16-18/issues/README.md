# Identity16-18 execution tickets

Execution authorized by the owner's full-port request on 2026-10-13.
Each ticket is a verifiable slice; acceptance remains gated by its evidence.
Implementation and serial encrypted diagnostics are in progress.

GitHub repository Issues are disabled. These are local execution tickets.
No GitHub writes, commits, pushes or repository setting changes are performed.

| Ticket | Blocked by | Independently verifiable result |
| --- | --- | --- |
| 01 | None | Pinned exact profiles, stage/scaling oracle and current baseline |
| 02 | 01 | Selected encrypted StC/ScaleDown reaches verified entry contract |
| 03 | 02 | Joint entry/ModUp produces correct resident proper + dense branches |
| 04 | 03 | Eight-term fused joint CtS reaches correct level-29 dense outputs |
| 05 | 01 | Both selected Mod1 functions execute correctly using native compiler |
| 06 | 02,05 | Complete selected ordinary refresh reaches level20/scale40 |
| 07 | 04,05 | Complete selected joint refresh reaches level20/scale40 |
| 08 | 06,07 | Both routes fit actual GPU live-memory budget with equivalent outputs |
| 09 | 08 | Performance gates, native fusion retention and regression evidence |

Ticket 01 is the entry frontier. Tickets 02 and 05 can proceed independently
after it; ordinary finish does not depend on joint CtS.

Primary public test seam: BootContext bootstrap with an explicit selected
profile, plus existing ProfileBoot. Existing entry/CtS/polynomial tests are
diagnostic seams. Performance gates are recorded in the specification.

Current qualification: **unqualified**. The selected rounding/merged-rescale
fixes allow ordinary to pass all standalone public patterns on three fresh
key sets. Joint still fails endpoints on all three key sets, and the pinned
Go reference independently fails the same pattern at 4.5005e-6.
Both routes return level20/scale2^40 and pass public/stage exact residues.
Independent deterministic matrices, eight-term exact residues, centered CRT
and full ModDown/rescale integer oracles pass. Formal performance remains
unmeasured and gated. Latest continuation is 2026-10-14.

Evidence and cross-ticket status: `../execution/status.md`.
