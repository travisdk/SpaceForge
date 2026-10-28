# SpaceForge

A texture effect for minimal and hypnotic techno and ambient: huge, living rooms, echoes and
granular texture. The aim is long, soft, dark and flowing tails.

SpaceForge combines two open-source engines:

- **Cloud Seed** (rooms and echoes): algorithmic reverb with modulated delay lines.
- **Mutable Instruments Clouds** (granular texture): the grains take from the tail of the room and
  feed part of it back, so the cloud keeps itself alive.

Formats: VST3, CLAP and Standalone. Linux is the main platform; the code is kept platform
independent.

## Controls

| Control | What it does |
|---|---|
| **Mode** | Character: Sky, Hall, Plate, Shimmer, Smear, Spectral, Room |
| **Space** | Tight and short to huge and endless (decay and stereo width) |
| **Reflect** | Nothing to many repeats that melt into the room (ping-pong, dotted 1/8) |
| **Motion** | Still to living: modulation, grain activity, tempo-synced auto-pan |
| **Tone** | Dark to bright, without changing the loudness |
| **Mix** | Dry/wet |
| **Echo** | Sharp, countable stereo echoes in time with the host tempo (note value chosen by clicking the text) |
| **Duck** | Pulls the wet signal down while you play, up to 12 dB |

Every knob shows its value in real units, and the DAW shows the same texts. A preset stores only
the values of the visible controls.

## Build

Requires CMake, a C++20 compiler and the JUCE dependencies for your platform.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Command-line tools built alongside: `sf2_render` (renders test sounds to WAV and prints
measurements), `sf2_regress` (proves that a change leaves existing presets sounding the same) and
`sf2_candidates`.

## License

SpaceForge is licensed under the **GNU AGPLv3 or later** (see `LICENSE`); JUCE is used under
AGPLv3. Third-party code is listed in `THIRD_PARTY.md`.

## Credits

- Cloud Seed by Valdemar Erlingsson, and CloudSeedCore by Ghost Note Audio (MIT).
- Clouds and stmlib by Emilie Gillet / Mutable Instruments (MIT).
- SpaceForge is not a product of, or endorsed by, any of the above.
