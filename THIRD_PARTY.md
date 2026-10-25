# Third-party code

SpaceForge is licensed under the GNU AGPLv3 (see `LICENSE`). Everything below is compatible
with it. Licenses verified 2026-10-23 by reading every vendored file's header.

## Vendored engines (`third_party/`)

| Name | Source | Version | License | Purpose |
|---|---|---|---|---|
| CloudSeedCore | https://github.com/GhostNoteAudio/CloudSeedCore | `deb21ded9eb7dad9b3ff94ce1ba96a963716594e` (2024-09-22) | MIT, © 2024 Ghost Note Audio / Ghost Note Engineering Ltd | Rooms and echoes (Cloud Seed algorithm) |
| Clouds DSP (`clouds/dsp`, `clouds/resources.*`, `clouds/drivers/debug_pin.h`) | https://github.com/pichenettes/eurorack | `f4cfa7697095a28d292bff758beb51deb49b5a86` (2026-10-12) | MIT, © 2014–2015 Emilie Gillet | Granular texture |
| Cloud Seed v1 factory programs (`third_party/cloudseed-programs/`: 9 JSON files and `license.txt`) | https://github.com/ValdemarOrn/CloudSeed (branch `legacy-v1`) | `13a625e00ec47db98b158a17e309d2cdaebb57ed` (2024-05-26) | MIT, © 2018 Valdemar Erlingsson | Starting points for the Modes' rooms (`source/dsp/Modes.cpp`; read in real units with v1's own scaling by `tools/modes/cloudseed_v1_programs.py`) |
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
- **Changes to the engines**, each only what real-time use in a plug-in or full audio quality
  needs, marked "SpaceForge:" in the code. The algorithms are unchanged.
  - Cloud Seed, `DSP/ReverbChannel.h`: caches the per-line random values and regenerates them
    only when a seed changes. The original regenerated them (allocating a `std::vector`) on
    every decay, size or modulation change, which must not happen on the audio thread.
  - Cloud Seed, `DSP/ReverbChannel.h`, `DSP/Hp1.h`, `DSP/Lp1.h`: the input was zeroed sample by
    sample below -90 dBFS (against CPU spikes from tiny numbers), a dead zone around zero that
    distorted every quiet signal into hiss; and the input filters cut off any negative state at
    once. Now only values below -400 dB become zero (the plug-in flushes denormals anyway).
  - Clouds, full quality on floats (the module's codec and memory forced 16 bits): the sample
    buffer (`clouds/dsp/audio_buffer.h`, new `RESOLUTION_32_BIT_FLOAT`, the crossfade tail as
    floats), input and output (`GranularProcessor::Process` takes and gives `FloatFrame`, with
    the module's soft limiter kept), the reverb and the pitch shifter (`fx/reverb.h`,
    `fx/pitch_shifter.h`: `FORMAT_32_BIT` instead of 12 and 16 bits; the pitch shifter in its
    own memory), and the spectral mode's analysis/synthesis buffers (`pvoc/stft.*`,
    `pvoc/phase_vocoder.cc`: floats instead of 16-bit overlap-add with clipping). The 16-bit
    buffer truncated every sample towards zero. The low-fidelity mode (8-bit mu-law, half rate)
    is still in the code and never used; the spectral mode's magnitude quantization (its texture
    setting below 0.48) is not used by any Mode.
  - Clouds, `clouds/dsp/window.h`: `Window::Start()` now marks the window as playing. `Init()`
    marks windows done and nothing cleared it, so the stretch mode (WSOLA) stayed silent: -72 dB
    against its input, also in the original 16-bit code (measured).
  Together these were the white noise Henrik heard when playing softly: the sky's feedback loop
  kept the rounding alive (measured: 6-28 dB of hiss in the treble, tails lost below -90 dBFS;
  after: the same tail at any level, the engine's own noise at -140 dBFS).
  - Clouds, `clouds/dsp/correlator.cc`: the WSOLA search shifted a 32-bit word by 32 when a
    candidate fell on a word boundary, which is undefined in C++ (found by UBSan). On the
    module's ARM it gives 0; on x86 it gave the word itself and spoiled that comparison. Now
    the shift is skipped there, so it behaves as on the module.
  - Clouds, `clouds/dsp/grain.h`: the grain window lookup is clamped just below the end of
    `lut_window`. At a grain's peak it read one entry past the table (found by
    AddressSanitizer).
  - stmlib is unmodified.
- Known Clouds issues worked around without a code change (found by AddressSanitizer): with
  its dry/wet parameter at exactly 1.0 the output cross-fade reads one entry past
  `lut_xfade_in`/`lut_xfade_out`, and with size at exactly 1.0 grain scheduling reads one entry
  past `lut_grain_size`. SpaceForge never sends a setting above 0.999 (dry/wet 0.999: grains
  only, the dry signal 56 dB down) and mixes dry and grains itself.
- Clouds runs at its native 32 kHz; `source/dsp/TextureEngine.*` converts to and from the host
  rate with SpaceForge's own converter (`source/dsp/Resampler.h`).
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
| `../SubForge` (same author) | The settings menu (accent colours, interface size, tooltips, preset folder, About), opening a folder with xdg-open and the freedesktop trash (`source/presets/PresetFiles.h`), adapted | AGPLv3 |
| `../protoSpaceForge` (SpaceForge 1, same author) | CMake setup, `cmake/GitVersion.cmake`, test helpers in `tests/TestUtils.h`, bus layout and state handling patterns; the look and the interface (`source/plugin/ui/Theme.h`, `MacroKnob`, `CloudView`, the editor layout, `tools/screenshot`), adapted to the six controls; the preset file reading and writing (`source/presets/Preset.*`, from `presets/Preset.*`), the preset browser, dice with undo and shape buttons (`source/plugin/ui/PresetBrowser.*`, `source/presets/Variation.*`, the editor layout), adapted to the Modes | AGPLv3 |
