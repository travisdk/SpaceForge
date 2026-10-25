# SpaceForge 2

Tekstur-effekt til minimal/hypnotisk techno og ambient: enorme, levende rum, ekkoer og granulær tekstur. Målet er **lange, bløde, mørke og flydende haler** til hypnotisk techno, i retning af den karakter SuperMassive og SOL er kendt for, og derfra prægning i Henriks egen retning.

## Roller (læs denne først)

- **Henrik har smagen.** Han vurderer kun: "kan jeg lide det?" og giver feedback i ord som "for lys", "for kort", "mere bevægelse". Du oversætter ordene til ændringer og viser med målinger, at ændringen er sket. Han er ikke DSP-ingeniør og skal aldrig bedes om tekniske vurderinger af reverb, delay eller filtre.
- **Claude Code har teknikken.** Al teknisk vurdering, måling og fejlfinding er dit ansvar. Når et valg skal træffes, så præsentér det som en lytte-sammenligning (A/B), ikke som et teknisk spørgsmål.
- Forklar altid i jævnt dansk. Henrik har stærk .NET/clean architecture-baggrund, men begrænset C++-erfaring.

## Lektier fra SpaceForge 1 (gentag dem ikke)

1. **Lyden først.** Version 1 byggede knapper, 54 presets og tests før nogen havde lyttet. Resultatet var flot, men lyden var ikke god nok. Ingen nye features, før motoren lyder godt.
2. **Kvalitet arves, den opfindes ikke.** Brug gennemprøvede open source-motorer i stedet for at skrive reverbs fra bunden.
3. **En test der er grøn, er ikke det samme som en knap der virker.** Tests skal kræve en hørbar minimumseffekt, ikke kun den rigtige retning.
4. **Mål selv, før Henrik lytter.** Vis tallene. Henriks ører bruges på smag, ikke på at finde ud af om noget virker.
5. **Erklær aldrig noget for løst uden bevis** i form af en tabel eller måling, som Henrik kan se.
6. **CC foreslår preset-kandidater, Henrik vælger dem med ørerne.** Kandidaterne skal bestå målingerne (lydstyrke ±1 dB, ingen sus med stille input, ingen der ligner hinanden for meget); kun dem Henrik beholder i sorteringstilstanden bliver factory-presets

## Motorerne vi bygger på

Begge licenser **skal verificeres som det allerførste** (se Licens). Er en af dem ikke AGPLv3-kompatibel: stop og spørg.

**Cloud Seed** (rum og ekkoer, SuperMassive-delen)
- Algoritmisk reverb til enorme, endeløse rum og modulerede ekkoer; bygget som specialeffekt til tykke pads ud fra simple lyde
- Kernen: https://github.com/GhostNoteAudio/CloudSeedCore
- Original (MIT): https://github.com/ValdemarOrn/CloudSeed
- Har sine egne delaylinjer, tidlige refleksioner og modulation. Brug dem til Reflect, før der overvejes et separat delay
- (Fase 4: Cloud Seeds delaylinjer giver kun tilfældigt spredte refleksioner, ikke rytmiske ekkoer synket til tempo. Reflect er derfor et lille ping-pong-delay, `source/dsp/EchoEngine.*`, foran rummet og ved siden af det. Den lille knap Echo er et eget, skarpt stereo-delay, `source/dsp/StereoDelay.*`, se Kontroller)

**Mutable Instruments Clouds** (granulær tekstur, SOL-delen)
- "Texture synthesizer": granulær motor med feedback, spredning og indbygget reverb
- Kode: https://github.com/pichenettes/eurorack (mappen `clouds`, evt. også efterfølgeren `beads`)
- Skrevet til en mikrocontroller med fast samplerate; skal tilpasses vilkårlige host-samplerater og bufferstørrelser
- Afhænger af `stmlib` fra samme forfatter

Motorernes indre ændres kun, hvor det er nødvendigt for at få dem til at køre i et plugin. Lydændringer sker via makro-mapping, ikke ved at omskrive algoritmerne.

## Signalkæde

```
Input -> Cloud Seed (rum og ekko) -> Tone/finish -> Mix
               ^            |
               +-- Clouds --+   (skyen: kornene tager fra rummets hale og sendes delvis tilbage)
```

Kornene tager fra rummets egen hale, ikke fra input: korn af input gav dobbelte anslag på korte bleeps (fase 4). En del af kornene går tilbage ind i rummet, så skyen holder sig selv i live (variant C, valgt af Henrik ved lytning), og en del går direkte ud ved siden af rummet. Hvor meget ejes af Space; en begrænser holder skyen fra at vokse ved høj Space. Skyens længde afhænger af, hvor kraftigt der spilles.

Kan Clouds' egen reverb gøre Cloud Seed overflødig i nogle presets, eller omvendt: fint. Det afgøres ved at lytte.

## Kontroller

Kun disse: **Mode, Space, Reflect, Motion, Tone, Mix**, plus de to små knapper **Echo** og **Duck** (se nedenfor). (Reflect hed tidligere Echo: kun navn og hover-tekst er ændret, parameter-ID og preset-nøgle er stadig `echo`, så gamle presets og DAW-projekter virker uændret. Freeze er taget ud indtil videre på Henriks ønske, som i SpaceForge 1; den ligger på branchen `feature/freeze` til evt. senere genindførelse, og motoren har den stadig.) Nye knapper tilføjes kun, når Henrik beder om dem efter at have lyttet. (Mode blev tilføjet på Henriks ønske: med kun fire makroer lød alle presets som den samme lyd.)

**Mode** er én synlig vælger med 6 karakterer (`source/dsp/Modes.cpp`): hver er en fast intern opsætning af Cloud Seed (fra Cloud Seeds factory-presets) og Clouds (dens fire tilstande). Henriks regler:
- Inden for én Mode er skalaen fast og ens for alle presets. Kun Mode må ændre en knaps spænd, aldrig et preset
- Space på 50 % er et mellemstort rum (ca. 12 s) i alle Modes, så knappen føles nogenlunde ens; yderpunkterne må variere mellem Modes
- Teksten under Space viser altid de målte sekunder for den valgte Mode, og det testes
- Terningen ændrer kun Mode, hvis brugeren har åbnet Mode-låsen (hængelåsen ved Mode-rækken, låst som standard) eller holder Shift; ellers er Mode brugerens valg (Henrik valgte det ved at prøve, 2026-10-25)
- Lo-fi-tilstande (fx Clouds' 8-bit eller spektrale kvantisering) bruges kun, hvis de bevidst er en del af en Mode

Makroerne har **faste roller og fast retning i alle Modes, og inden for en Mode én fælles absolut skala i alle presets.** En knapposition betyder altid det samme, uanset preset. Højre er altid "mere".

- **Space**: tæt og kort -> stort og uendeligt. Styrer decay og stereobredde (ikke delaylinjernes længde: at ændre den, mens der er lyd i linjerne, klikker; målt i fase 4)
- **Reflect**: intet -> mange gentagelser der smelter ind i rummet (ping-pong, 1/8 punkteret). Styrer mængde og feedback
- **Motion**: stille -> levende. Styrer modulation, granulær aktivitet og tempo-synket auto-pan
- **Echo** (lille knap, ID `delay`): intet -> mange skarpe, tydeligt tællelige gentagelser i takt med host-tempo. Ægte stereo-delay: venstre side på nodeværdien, højre på 4/3 af den, hver med sin egen feedback-sløjfe. Feedback filtreres (højpas 150 Hz, lavpas fra Tone), så det aldrig grumser. Går parallelt med Reflect: gentagelserne høres direkte og fodrer også rummet. Knappen styrer **kun mængde og antal gentagelser, aldrig nodeværdien**. Nodeværdien (1/16, 1/8, 1/8d, 1/4, 1/4d; standard 1/8d) er et fast valg (parameter `delayNote`), gemmes i presetet og vises som tekst ved knappen (klik på teksten skifter nodeværdi). Vist som "1/8d · 3x"; ved 0 "off · 1/8d". Niveau kompenseret, så Echo højst ændrer niveauet ca. 3 dB
- **Duck** (lille knap, ID `duck`): efterklangen trækker sig, mens der spilles, og kommer op i pauserne. Alt vådt (rum, ekko og sky) trækkes ned 0 til 12 dB (vist som "6 dB") af inputtets envelope (hurtigt angreb, 0,4 s release). Ændrer med vilje niveauet op til 12 dB, men kun mens der spilles
- **Tone**: mørk -> lys. Klangfarve uden at ændre lydstyrken (niveaukompensation). Dæmpning i rummet og ekkoet plus en blød diskant-"finish" efter rummet (rummets sløjfe må aldrig forstærke)

Regler:
- Echo og Duck står på 0 % som standard, og på 0 % er lyden **bit for bit** den samme som uden dem (bevist med `sf2_regress`: alle presets, forskel under -100 dB). Terningen rører dem ikke
- Hver intern parameter ejes af præcis én knap
- Ingen makro må ændre det samlede niveau mere end ca. 3 dB (Duck undtaget: den er netop en ændring af niveauet, mens der spilles)
- Alle makroer virker i alle presets
- Under hver knap vises værdien i rigtige enheder (fx "18 s", "1/8d · 3x", "6 dB", "dark ••••○ bright"), og DAW'en får de samme tekster
- **Al tekst i produktet er på engelsk, altid** (knaptekster, tooltips, menuer, dialoger, Mode-beskrivelser, DAW-tekster, sorteringstilstanden). Samtalen med Henrik er stadig på dansk
- **Intet skjult:** et preset gemmer kun værdierne af de synlige kontroller. Har et preset brug for et valg uden egen knap (fx nodeværdi eller ping-pong), vises det som tekst ved den relevante knap

## Stereo

- Det våde signal er mono under ca. 150 Hz; ingen "ekstra bred" (bredde over 1)
- Mono-sum må højst miste ca. 3 dB
- Auto-pan (Motion) synkes til host-tempo: 1/2, 1, 2, 4 eller 8 takter. Kører transporten ikke, kører pan frit, så den altid kan høres

## Presets

- Factory-presets er **Init** plus de kandidater Henrik har beholdt (K) i sorteringstilstanden (standalone startet med `--sort`). CC foreslår navne ud fra lyden; Henrik retter dem
- Ingen preset-navne fra SuperMassive, SOL eller Cloud Seed (og ingen stjerne-, galakse- eller stjernebilled-navne)
- JSON, én fil pr. preset, brugerpresets i brugerens dokumentmappe
- Visning som i SpaceForge 1: browser til venstre (søgning, favoritter, kategorierne er Modes), navnet med forrige/næste, favorit, terning, fortryd og Save
- Preset-skift er **rent**: lyden skifter straks med en kort crossfade (30 til 50 ms), ingen spillover. Knap-animationen er kun visuel og må aldrig få lyden til at glide. Delaytider crossfades i stedet for at glide (ingen pitch-sweep)
- Terningen flytter de fire makroer (Space, Reflect, Motion, Tone) synligt (ca. 250 ms animation), mindst to af dem mærkbart. Mix røres aldrig; Mode kun med åben Mode-lås eller Shift. Fortryd for de sidste 10 kast. Ændret preset markeres med *
- **Alt der ændrer lyden, skal kunne ses i UI'et**

## Lydmål og måling

Der er **ingen referenceoptagelser**. Henrik optager ikke noget gennem SuperMassive eller SOL. Retningen kommer fra viden om algoritmisk reverb og delay (feedback delay networks, diffusion, modulerede delaylinjer, dæmpning i feedback-loopet) plus Henriks ord-feedback.

- **Testlyde** genereres af koden selv, deterministisk: tørt klik/impuls, kort støjburst, pad-akkord, kort synth-figur, hi-hat-mønster. Ingen eksterne lydfiler
- **Offline render**: et værktøj renderer hver testlyd gennem SpaceForge 2 til WAV i `renders/` (er i `.gitignore`), så Henrik kan lytte uden DAW (`sf2_render --echo-duck`: korte bleep-renders af Echo og Duck ved 0, 50 og 100 %; `sf2_regress dump/compare`: beviser, at nye knapper på 0 ikke ændrer eksisterende presets)
- **Målinger** pr. render, vist som tabel: halelængde (T30/RT60, også pr. frekvensbånd), lysstyrke (spektralt tyngdepunkt, og hvor meget hurtigere diskanten dør end mellemtonen), stereobredde (korrelation over og under 150 Hz, tab ved mono-sum), niveau ind/ud, og glathed (ingen metallisk ringen, flutter eller tydelige enkelt-ekkoer i en hale der skal være glat)
- **Startmål for karakteren** (justeres efter Henriks feedback): halen er mørk, dvs. diskanten dør klart hurtigere end mellemtonen; halen er tæt og glat uden ringen; stereo er bred over 150 Hz og mono under; bevægelsen er langsom og blød, uden hørbar pitch-vibrato
- Henriks ord oversættes til målbare ændringer ("for lys" -> lavere tyngdepunkt / hurtigere diskant-dæmpning) og vises som før/efter-tabel
- Valg mellem varianter præsenteres som to renders af samme testlyd (A og B), ikke som et teknisk spørgsmål


## Genbrug fra søsterprojekter

Ved siden af ligger `../SpaceForge` (version 1), `../SubForge` og `../BleepForge`. De er **skrivebeskyttede**: læs og kopiér, ændr aldrig.

Fra `../SpaceForge` hentes især: det visuelle design og animationerne, preset-browseren (søgning, favoritter, kategorier), terning med fortryd, sikkerheds-hærdningen, build-opsætningen (CMake, JUCE, CLAP, pluginval) og visning af værdier i rigtige enheder.

Fra `../SpaceForge` hentes **ikke**: CloudReverb, Grains, Delay, den gamle MacroMapper, de 54 presets og tests kalibreret til den gamle motor.

Kopieret kode tilpasses det nye projekt; kopiér ikke hele filer blindt. Notér oprindelsen i `THIRD_PARTY.md`.

## Faser

1. **Licenser og skelet**: verificér licenserne for CloudSeedCore og Clouds/stmlib og rapportér. Derefter projektskelet med build-opsætning fra `../SpaceForge`, `LICENSE`, `THIRD_PARTY.md`
2. **Cloud Seed alene** + Mix + Freeze + testlyde, offline render og målinger (se Lydmål og måling). Justér selv mod startmålene. **Stop: Henrik lytter.** Ingen videre før han siger god
3. **Clouds** ind foran Cloud Seed. **Stop: Henrik lytter**
4. **Makro-mapping** med fælles skala og kontrakt-tests (retning + hørbar minimumseffekt). Vis tabel med værdier pr. knapposition
5. **UI** overført fra `../SpaceForge`, tilpasset de seks kontroller
6. **Presets**: CC genererer målte kandidater, Henrik sorterer dem med K/S, CC foreslår navne
7. Polering, pluginval, release

Afslut hver fase med: build uden warnings, tests grønne, målinger vist, og en kort beskrivelse på jævnt dansk af hvad Henrik skal lytte efter.

## Licens

- SpaceForge 2 udgives under **AGPLv3**; JUCE bruges under AGPLv3
- `LICENSE` i roden; hver kildefil starter med `// SPDX-License-Identifier: AGPL-3.0-or-later`
- Kun AGPLv3-kompatible afhængigheder (fx MIT, BSD, ISC, Zlib, Apache 2.0, LGPL, GPLv3). Ukendt eller uklar licens: brug den ikke, spørg
- Aldrig kode fra repos uden licens, fra lukkede produkter eller dekompileret materiale
- SuperMassive og SOL bruges kun som ord for den ønskede karakter. Ingen optagelser af dem, ingen reverse engineering, ingen kopierede presets, værdier eller grafik, og deres navne bruges ikke i presets eller markedsføring
- Al tredjepartskode registreres i `THIRD_PARTY.md` (navn, URL, licens, formål)

## Sikkerhed

- Preset- og state-indlæsning skal tåle ødelagte, enorme og ondsindede filer: begræns filstørrelse, valider alle værdier
- Tjek altid `std::isfinite` før clamping (`jlimit` og `std::clamp` lader NaN slippe igennem); ugyldige værdier erstattes af default
- Preset-navne saniteres, så de aldrig kan pege uden for preset-mappen
- Åbn aldrig mapper eller filer via `File::startAsProcess()` på Linux; brug `ChildProcess` med `xdg-open` og stien som separat argument
- Ingen netværk, aldrig
- Afhængigheder låses til præcise versioner
- Fuzz-test af preset-loaderen; tests køres også med AddressSanitizer og UBSan

## Real-time

- Ingen allokeringer, locks, fil-I/O eller logning i `processBlock`; alle buffere allokeres i `prepareToPlay`
- Beskyt mod NaN og denormals; feedback-loops må aldrig eksplodere
- 44,1 / 48 / 96 kHz og vilkårlige bufferstørrelser
- Ingen klik ved preset-skift eller parameterspring

## Arbejdsform på Henriks maskine

- Builds og tests køres med `nice -n 19` og højst halvdelen af CPU-kernerne (Henrik lytter til musik og bruger Bitwig samtidig)
- Byg kun det der er ændret; slet ikke build-mappen uden grund
- Fuzz-tests har en tidsgrænse (fx 60 sekunder)
- Formater: VST3, CLAP, Standalone. Platform: Linux primært, koden holdes platformsuafhængig
