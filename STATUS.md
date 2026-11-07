# Status (2026-11-07)

- Release 1.0.0 (first stable release, no changes in sound or features since 0.9.x). Previous release 0.9.1: the CLAP editor is no longer cropped at Windows display scaling (verified on Win11), plus the Cloud Seed modulation-phase change (294/294 renders bit for bit identical).
- Linux build and all 8 tests green; Windows build from CI (all 8 tests green).
- Parameter IDs, Mode order and preset format are frozen from 0.9.0 and unchanged in 1.0.0.
- Known, not claimed: preparing again *after* playing is not bit-identical (some other state remains); before playing it is.
- Idea for later: make preset loudness robust to modulation phase (Tone level compensation).
