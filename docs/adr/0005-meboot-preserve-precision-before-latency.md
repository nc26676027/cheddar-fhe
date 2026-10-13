# Preserve standard precision while retaining the CtS level saving

Accepted on 2026-10-12. The owner requires MEBOOT precision close to standard
bootstrap (about 129 dB in the recorded 32-bit run), retains the one-level CtS
saving, and targets standard Basic latency. The previous absolute-error
acceptance allowed a roughly 31 dB relative precision loss, so future performance
comparisons must report paired precision and depth alongside latency; improving
speed by accepting that loss is outside the agreed trade-off.
