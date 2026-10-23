# Third-party code

SpaceForge 2 is licensed under the GNU AGPLv3 (see `LICENSE`). Everything below is compatible
with it. Licenses verified 2026-10-23 by reading every vendored file's header.

## Vendored engines (`third_party/`, copied unmodified)

| Name | Source | Version | License | Purpose |
|---|---|---|---|---|
| CloudSeedCore | https://github.com/GhostNoteAudio/CloudSeedCore | `deb21ded9eb7dad9b3ff94ce1ba96a963716594e` (2024-09-22) | MIT, © 2024 Ghost Note Audio / Ghost Note Engineering Ltd | Rooms and echoes (Cloud Seed algorithm) |
| Clouds DSP (`clouds/dsp`, `clouds/resources.*`, `clouds/drivers/debug_pin.h`) | https://github.com/pichenettes/eurorack | `f4cfa7697095a28d292bff758beb51deb49b5a86` (2026-10-12) | MIT, © 2014–2015 Emilie Gillet | Granular texture |
| stmlib (11 files used by the Clouds DSP) | https://github.com/pichenettes/stmlib | `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` (2023-05-30, the commit eurorack pins) | MIT, © 2012–2014 Emilie Gillet | DSP helpers for Clouds |

Notes:

- `third_party/cloudseedcore/`: the `DSP/` folder, `Parameters.*` and `Programs.h` (factory
  programs, used only by the tests). Not taken: the demo `PluginProcessor.cpp` and the Visual
  Studio project files. The original Cloud Seed by Valdemar Erlingsson
  (https://github.com/ValdemarOrn/CloudSeed, MIT) is its predecessor and is not used.
- Ghost Note Audio asks to be credited as the original author of the Cloud Seed algorithm, and
  that "Cloud Seed" is only named in a way that makes clear SpaceForge is not their product.
- "Mutable Instruments" is a registered trademark. Its author asks that neither that name nor
  the module name is used as the name of derivative works. They are named here only as source.
- The parts of stmlib with other licenses (`ui/event_queue.h` GPLv3, `utils/crc32.h`,
  `utils/murmurhash3.h`, `third_party/STM`, `programming/serial`) are hardware/UI code that the
  Clouds DSP does not use, and are not copied.
- Only build glue lives outside `third_party/`: `source/compat/CloudSeedCompat.h` supplies
  `strcpy_s` and `std::sinf`/`sqrtf`/`expf`, which Cloud Seed expects from MSVC, to other
  compilers; Clouds is built with its author's desktop define `TEST`.

## Build dependencies (not in this repository)

| Name | Source | Version | License | Purpose |
|---|---|---|---|---|
| JUCE | https://github.com/juce-framework/JUCE | 8.0.15 | AGPLv3 (dual-licensed with a commercial license) | Plug-in framework, VST3, Standalone |
| clap-juce-extensions | https://github.com/free-audio/clap-juce-extensions | `7adee3a1bd4684d4caa5601100e364abccff4b4d` | MIT | CLAP format |
| CLAP SDK and clap-helpers | https://github.com/free-audio/clap | 1.2.7, the submodules pinned by clap-juce-extensions | MIT | CLAP format |

## Code reused from sister projects

| From | What | License |
|---|---|---|
| `../SpaceForge` (SpaceForge 1, same author) | CMake setup, `cmake/GitVersion.cmake`, test helpers in `tests/TestUtils.h`, bus layout and state handling patterns | AGPLv3 |
