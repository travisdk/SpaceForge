# Status (2026-10-29)

- `fix/lcg-mod-phase` is merged into `main` (fast-forward, pushed). Windows CI run 40769406124 was green (all 8 tests) on the code commit 945f58c.
- Change: Cloud Seed modulation phases come from a per-engine generator (glibc's random() algorithm, seed 1) instead of global `std::rand()`.
- Proof of unchanged sound: `sf2_regress` old vs new, 294/294 renders bit for bit identical.
- Windows build (`SpaceForge.clap`, `.vst3`, `.exe`) downloaded from the CI artifact to the scratchpad, ready to be tried on Win11.
- Known, not claimed: preparing again *after* playing is not bit-identical (some other state remains); before playing it is.
- Next: Henrik tries the Windows build on Win11. A fresh CI run on `main` itself has not been done yet.
- Idea for later: make preset loudness robust to modulation phase (Tone level compensation).
