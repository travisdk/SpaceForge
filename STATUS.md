# Status (2026-10-28)

- Branch: `fix/lcg-mod-phase` (pushed; `main` untouched, its Windows CI is red on the `presets` test).
- Change: Cloud Seed modulation phases come from a per-engine generator (glibc's random() algorithm, seed 1) instead of global `std::rand()`.
- Waiting for: Windows CI run 40769406124 (workflow_dispatch on this branch, started 21:08 UTC, ~30 min).
- Local (Linux): all 8 tests pass (engines, space, texture, plugin, presets, modes, macros, echoduck); build has no warnings.
- Proof of unchanged sound: `sf2_regress` old vs new, 294/294 renders bit for bit identical.
- Known, not claimed: preparing again *after* playing is not bit-identical (some other state remains); before playing it is.
- Next: read the CI result. If all 8 pass on Windows, merge the branch into `main` and fetch the `SpaceForge-windows` artifact for Win11. If not, fix what fails.
- Idea for later: make preset loudness robust to modulation phase (Tone level compensation).
