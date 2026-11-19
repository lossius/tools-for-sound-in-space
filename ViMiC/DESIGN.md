# ViMiC (Virtual Microphone Control) — design note

Status: **milestones 1 (direct sound), 2 (source directivity) and 3 (early reflections) are implemented**; the rest is still a plan. Decisions already taken: positions stay in the family's 2D plane with global
source and microphone heights (for the room); the first version renders direct sound only, early reflections come second; the late reverb
tail is left to an external reverb.

## 1. What the plugin is

ViMiC (Braasch, Peters) places *virtual microphones* in a virtual room with the sound sources. For every source–microphone pair it
computes a gain and a delay from their geometry; the output channels are the microphone signals, which are then played on loudspeakers.
Compared with TriPan, DistPan and VecPan it is different in four ways:

| | TriPan / DistPan / VecPan | ViMiC |
|---|---|---|
| outputs | speakers | virtual microphones: position **and orientation and directivity** |
| per source–output path | a gain | a gain **and a delay** (and, for reflections, a filter) |
| room | none | a shoebox room: size, wall absorption, early reflections (image sources) |
| sources | position, spread | position, **orientation and directivity** (later); spread is not used |

The plugin should still look and behave like the other three: the same pad, tables, groups, trim/mute/solo/exclude, import/export,
OSC and undo. In the code and in the files the outputs remain "speakers" (so layouts can be exchanged between plugins); in the user
interface they are called **microphones** (see 6).

## 2. References, credit and licence

- Concept and implementation: Jonas Braasch, Nils Peters; implementation also Tristan Matthews. The repositories
  [jamoma/JamomaMax](https://github.com/jamoma/JamomaMax) (`source/j.vimic~`) and
  [Nilson/ViMiC-and-friends](https://github.com/Nilson/ViMiC-and-friends) (`ViMiC_manual.pdf`, `shared/VimicSurfaceProperties.txt`) are
  read-only references. **They are not cloned, vendored or added as submodules.** The manual (draft of 21 September 2013) is the
  main specification; the C++ was read to check details the manual leaves open.
- Both are stated to be under the New BSD (3-clause) licence, which is compatible with this project's AGPL. Anything taken over keeps the
  BSD copyright notice and licence text (a `THIRD_PARTY_NOTICES.md` and the file header) and the authors are credited in the README.
- **Plan: implement from the equations, not from the code.** The model is published (Peters, Braasch & McAdams 2011; Peters, Matthews,
  Braasch & McAdams 2008; Braasch, Peters & Valente 2008) and the Max code is tied to Max and Jamoma. What we do take from the references:
  the model, the parameter meanings and ranges, the pattern table, the reflection counts, and the small surface-absorption table
  (with attribution).
- **Not to be ported:** `HiMidLow` and `LowPass` say "based on hml_shelf~.c by Thomas Musil, IEM KUG Graz" (and STK's `Biquad.h`). That is
  not covered by the BSD statement as far as I can tell. We write our own biquads (shelving and Butterworth low-pass from the standard
  audio-EQ formulas).
- Further reading listed in the manual: Allen & Berkley 1979 (image method); Pellegrini 2002 (virtual rooms);
  Lossius, Baltazar & de la Hogue 2009 (DBAP, which the manual notes is what ViMiC's "Panning" mode gives with omni microphones and
  normalised gains).

## 3. The model

### 3.1 Coordinates and conventions

The family's convention is kept: x to the right, y to the front, azimuth counter-clockwise from the front (`x = -d sin az`, `y = d cos az`).
j.vimic~ measures yaw the other way (its direction vector is `(sin yaw, cos yaw)`, so +90 points right). Conversions happen only in OSC and
files that follow ViMiC's conventions.

The listener/room origin is (0, 0). The room is a box centred on the origin in the plane, `x ∈ [−W/2, W/2]`, `y ∈ [−D/2, D/2]`, with the
floor at z = 0 and the ceiling at z = H. Every source is at height `h_s` and every microphone at height `h_m` (global parameters, default
1.5 m; the room height H defaults to 3 m). So positions are 3D `S = (x_s, y_s, h_s)` and `M_i = (x_i, y_i, h_m)`.

### 3.2 One path

A path from source `S` to microphone `i` is either the direct path or a reflected path, described by an *image source* `S'` (the source mirrored
in the walls it hits). With `d = |S' − M_i|`:

- **Delay** `τ = d / c`, with the speed of sound `c = 331.3 + 0.606 · T` m/s (T = room temperature in °C, default 20 °C, so 343.4 m/s).
- **Distance gain**, two laws (global choice):
  - inverse: `g_d = 1 / max(d, d_min)^q`, exponent `q` 0..9 (default 1; 1 is the free field), `d_min` default 1 m;
  - exponential: `g_d = 10^(−k·d / 20)`, `k` 0..60 dB per metre.
- **Microphone directivity**: with `δ` the angle between the microphone's axis and the direction to `S'`,
  `Γ = (a + (1 − a) cos δ)^w`, `a` 0..1 per microphone (1 omni, 0.7 subcardioid, 0.5 cardioid, 1/3 supercardioid, 0.3 hypercardioid, 0 figure-8),
  `w` an integer 0..9 (global, default 1). Optionally the polarity is restricted: negative `Γ` (figure-8 rear lobe) is set to 0 (global, default on).
  The axis is `u_i = (−sin ψ_i, cos ψ_i, 0)` for the yaw `ψ_i`; pitch is not in the first version (all heights are equal by default).
- **Source directivity** (later, see 7): a gain `Γ_s(φ)` of the angle `φ` between the source's axis and the direction from the source to the microphone
  (for a reflection, to the image of the microphone across the same walls). Omni by default.
- **Total gain** `g = g_direct_or_order · g_d · Γ · Γ_s`, times the per-microphone and per-source trim, mute and solo of the shared code. The gain is
  limited to `[0 or −1, 1]` as in j.vimic~ (so the distance gain can never boost beyond 1 and the polarity limit holds).

The direct path has order 0. A global gain per order (direct, 1st, 2nd order reflections, in dB) sets their balance; this is how j.vimic~ lets
the user trade early reflections against the direct sound. A reflection order of 0, 1 or 2 is chosen with a menu (j.vimic~ later derived it
from the gains; an explicit menu is simpler to understand).

### 3.3 How delay is rendered (three modes, global choice)

- **Relative delays** (default): per source only the *differences* between its delays to the microphones are rendered
  (`τ − min_i τ`), as j.vimic~'s "Minimize Delay". No latency, no big pitch changes, and the inter-channel time differences (which carry the
  spatial impression) are intact.
- **Absolute delays with Doppler**: the true time of flight. A moving source gives a Doppler shift. Each path reads its source's delay line
  with a **4-point interpolation** (j.vimic~'s "4-pole interpolated delay line"; Hermite or Lagrange) and the delay changes linearly across a block.
- **Cross-fade** (later): the delay of a path stays fixed until it has moved more than a threshold (default 1 ms), then the old and new paths
  are cross-faded over a fade time (default 50 ms; envelope cosine/linear/...). No Doppler. The manual itself lists "X fade XL" as
  producing clicks, so this mode is last and has to be done carefully.

The delay buffer holds the longest path that can occur (positions are limited to ±100 m, a path can then reach about 0.8 s at second order;
the buffer is 1 s and longer paths are clamped). A gain that changes is ramped linearly across the block, as in the other plugins.

### 3.4 Early reflections (milestone 2)

- **Image sources** in the box. Order 1: 6 images (one per wall). Order 2: 18 images (two reflections between two parallel walls: 6; one
  reflection on each of two different axes: 12). With the direct path: 1, 7 or 25 paths per source–microphone pair. (j.vimic~ renders only the 12 cross-axis images of the second order, 19 paths in all; see section 7d.)
- **Wall absorption** per wall (left, right, front, rear, floor, ceiling) in three bands, low, mid and high, `α` in percent (0.1..99.9), with two
  shared crossover frequencies (default about 450 Hz and 3.5 kHz in j.vimic~). The pressure factor of a wall in a band is `β = sqrt (1 − α/100)` (the amplitude that is left of the energy; j.vimic~'s `percentToGain` does the same). A path that hits several walls multiplies their factors per band.
  Presets (zero, full, acoustic tiles, audience, brick, carpet, drywall, metal, ...) come from a small table of absorption coefficients
  based on `VimicSurfaceProperties.txt` (12 entries, with the crossover frequencies) and textbook values for other surfaces such as glass and
  wood, with attribution.
- **Filtering a path**: the path's colour is `gl low + gm mid + gh high` of three band signals of the source (the bands add up to the input exactly: a 2nd-order Butterworth low-pass at the lower crossover, the difference between that and
  the low-pass at the upper crossover, and the rest), where `gl, gm, gh` are the band gains of the walls the path hits. The band signals are computed **once per source**, and each wall sequence is only three gains, written into a delay line of its own
  (**per source and wall sequence**, before the delay of every microphone): 22 classes (1 + 6 + 15) for the second order. This replaced the plan with shelving biquads per class, which cost ten times as much.
- **Air absorption**: a 2nd-order Butterworth low-pass on all reflections with one adjustable cut-off (500..19000 Hz), as in j.vimic~; applied in the same
  per-source chain.
- A source or microphone outside the room is allowed (positions go to ±100 m) but not physical: its reflections are faded out over
  0.5 m outside the wall, so there is no sudden change.
- **Render threshold**: paths whose gain is below a threshold (default off; −60 dB when on) are skipped, as in j.vimic~'s "renderthreshold".

### 3.5 Output

`y_i = G_out · trim_i · Σ_s Σ_paths g_{s,i,p} · x_{s,p}(t − τ_{s,i,p})`, where `x_{s,p}` is the source signal after the path's filter, plus
exclude/mute/solo as in the shared code. There is no late reverb (an external reverb on the outputs). j.vimic~ also normalises gains to unit
power optionally; this is left out (an "intensity normalisation" option would make omni microphones equal DBAP and could be added later).

## 4. Fit with the family

### 4.1 Pad and tables

- Microphones are the "speakers" of the pad. Each shows its **pickup lobe**: a small polar plot of `Γ` (pointing along its yaw, in the colour
  language of the other plugins) so patterns and orientations can be seen and dragged. Dragging a lobe's tip could set the yaw (later).
- The **room** is drawn as a rectangle with the same blue dashed outline and faint tint as the falloff regions of the other plugins; its size
  follows the room parameters. (The shared "room" of the pad, the display extent, stays a separate display setting.)
- Sources: marker plus, later, an arrow for the orientation. The spread circle, spread drags and the spread column are not used and are
  hidden (see 6).
- Tables: Microphones: x, y (or azimuth, distance), **Yaw**, **Pattern a**, trim, mute, solo, exclude. Sources: x, y, **Yaw**, **Pattern** (later), trim,
  mute, solo, group.
- The default layout is a ring of microphones at the usual 5 m, each pointing outwards (yaw = its azimuth), cardioid, which already gives a
  sensible surround microphone array.
- **Microphone setup presets** (like the speaker layouts): stereo AB, XY, ORTF, NOS, Decca tree, quad, IRT cross, 5.0 and 7.1 arrays, a ring of
  8 cardioids; files with `yaw` and `pattern` attributes.

### 4.2 Host parameters (IDs are part of saved sessions once released)

Global (after the shared counts, before everything per source), all automatable:

| ID | Name | Range / default |
|---|---|---|
| `roomWidth`, `roomDepth`, `roomHeight` | Room Width / Depth / Height | 1..40 m, 10 / 10 / 3 |
| `sourceHeight`, `micHeight` | Source / Microphone Height | 0..H, 1.5 / 1.5 |
| `temperature` | Temperature | −20..40 °C, 20 |
| `distanceLaw`, `distancePower`, `distanceDb`, `minDistance` | Distance law, exponent q, dB per m k, minimum distance | inverse; 1.0; 6 dB/m; 1 m |
| `micPower`, `micPolarity` | Microphone exponent w, restrict polarity | 1; on |
| `delayMode` | Delay | Relative / Absolute (Doppler) / Cross-fade |
| `directGain`, `refl1Gain`, `refl2Gain`, `outputGain` | Direct, 1st, 2nd order, output gain | dB |
| `reflOrder` | Reflections | Off / 1st / 2nd order |
| `absorb<wall><band>` (6 × 3) | Wall absorption | % |
| `absorbLowMid`, `absorbMidHigh` | Crossover frequencies | Hz |
| `airCutoff` | Air absorption cut-off | 500..19000 Hz |
| `renderThreshold` | Render threshold | dB, off |

Per source (new shared mechanism, see 6): `src<N>yaw`, later `src<N>pattern`. Per microphone (a "speaker column", extended to several):
`spk<N>yaw` (−180..180°), `spk<N>pattern` (a, 0..1, default 0.5). The shared `outsideRolloff` and the sources' `spread` have no meaning
here (see 6).

OSC (the family's `getOscParameters`, at most 4 today, to be raised): `/room/size`, `/vimic/...` for the globals, `/microphone/N/yaw|pattern`,
`/source/N/yaw`; ViMiC's own namespace (`/microphone.N/orientation`, `/room/size/xyz`, ...) could be accepted as aliases later.

## 5. Architecture

- **`ViMiC/core` (`ViMiCCore`, namespace `vimic`, JUCE-free):**
  - `Geometry`: positions, image sources, path enumeration for a room and an order; azimuth/yaw conventions.
  - `Directivity`: the pattern `Γ`, the preset table, source directivity.
  - `PathLaw`: distance laws, delay, the combined `PathGain {gain, delaySeconds}` for one source–microphone–image triple. This is the ViMiC
    counterpart of `tlos::SourceGains`; it lives in the plugin's core (and moves to `shared/core` only if a second plugin needs delays).
  - `DelayLine` (circular buffer, 4-point interpolated read with a linearly changing delay), `Biquad` and the shelving / Butterworth designs,
    `WallFilter` (three-band, from absorption percentages), `CrossFade` (later).
- **`ViMiC/Source`:** `PluginProcessor` (derives from `tlos::SpatialProcessor`; mic layout and room handed over with `tlos::Exchange`, per-source delay
  buffers and filtered-copy buffers preallocated in `prepareToPlay`, a per-block path table with gains and delays, gain and delay ramps), `ViMiCField`
  (lobes, room, links), `PluginEditor` (settings rows: room, distance, delay mode, reflections, absorption presets; a small absorption editor).
- **Real time:** no allocation or locks in `processBlock`. Path tables are fixed arrays sized for 64 sources × 64 microphones × 19 paths (about
  80 000 entries). The path table is rebuilt only for sources whose position, orientation or room parameters changed, and for microphones that changed.
- **Cost** (to be measured, as for DistPan): direct sound only costs one interpolated read per source and microphone and sample (64×64 = 4096 per
  sample, a few ms per 512-sample block); the room multiplies that by up to 19. The manual's table for the Max version shows 8 microphones with
  first-order reflections at 15–23 % of a 2007 Mac mini core. Mitigations: the filter-once-per-source structure above, the render threshold, a constant
  gain taking the cheaper path, and skipping silent sources.

## 6. Changes to the shared code (small, each with tests in the existing plugins)

1. **Several per-speaker columns.** `SpeakerColumn` is one column today (DistPan's weight); make it a list, in the table, the speaker files
   (attributes), OSC and the parameter layout (`PluginParameters::perSpeaker` already holds k parameters per speaker).
2. **Per-source parameters and columns** (the same, for sources): `PluginParameters::perSource`, `SourceColumn`s in the sources table, files, OSC.
3. **Wording.** A hook for the noun used for outputs ("Speakers" / "Microphones") in the editor, table windows, import/export and messages.
4. **Opt-outs.** The plugin can say that the sources' spread and the shared outside rolloff are not used: the spread column, circle and Option-drag
   are hidden, and the outside-rolloff slider is not shown. (The host parameters stay, for stable parameter lists and IDs.)
5. **Pad.** A per-speaker and per-source glyph hook (the lobe, the orientation arrow) next to `paintOverlay`/`paintBackdrop`, and a room rectangle
   helper built on the falloff-region drawing.
6. **Position-file attributes** `yaw` and `pattern` (and a rule like trim's: if any entry has one, it defines it for all).
7. `OscParameter` limit raised from 4.

## 7. Milestones and how each is tested

Each milestone is shippable and, as with the other plugins, tested numerically (Claude cannot listen); listening and host checks are yours.

1. **Direct sound** (the first version).
   - Core: `Γ` for the preset table and any `a`, `w`; both distance laws; delay for given positions and temperature; relative-delay normalisation;
     the 4-point interpolator against an ideal fractional delay of sine waves (error bound) and its frequency response; a moving source against the
     Doppler formula (measured pitch ratio `c / (c − v)` within a tolerance); gain and delay ramps with no clicks (maximum sample-to-sample step).
   - Processor: pairwise geometry checks (a source on a microphone's axis, off axis, behind a cardioid); parameter order; state; 64×64 stress;
     Release cost test printed like DistPan's.
   - Editor: the lobes and tables; polar/Cartesian as in the other plugins.
   - The delay mode "Cross-fade" is not in this milestone.
2. **Source directivity**: pattern and yaw per source, orientation modes (static now; "center" and "follow" later), arrow on the pad.
3. **Early reflections**: image positions and counts (1, 7, 19), checked against an independent brute-force ray enumeration; arrival times; wall
   filters against analytic magnitude responses; per-wall absorption; order-2 filter combinations; the room on the pad; surface presets; render threshold.
4. **Refinements**: the cross-fade mode, OSC aliases in ViMiC's own namespace, pitch for microphones (3D axis), absorption editor, performance work,
   optional intensity normalisation.

## 7b. What milestone 1 turned out like

Implemented as designed in 3.2 and 3.3 (direct path, both distance laws, microphone pattern and polarity, yaw, relative and absolute delays with the 4-point interpolated, smoothly
changing delay), 4.1 (lobes on the pad; microphone setups; polar coordinates by default) and 4.2 (the global parameters of the table, `spk<N>yaw`, `spk<N>pattern`) and 6 (shared changes 1, 3,
4, 5 in part, 6 and 7; the per-source parameters of change 2 wait for milestone 2). Differences and additions:
- The shared "Speakers" wording became a hook (`getOutputNoun`); the host parameters say "Microphone N ...".
- No room on the pad and no room parameters yet (milestone 3): the room size was not needed for the direct sound.
- The plugin reports a latency of 2 samples in both modes: the interpolation needs two samples after the one it reads, so every delay gets `+ 2` samples.
- The cross-fade mode is not there (as planned: it is last). Switching between Relative and Absolute restarts the ramps and may click.
- A path that is silent starts at its new delay, so unmuting a microphone or bringing a source close does not sweep the delay.
- Cost, Release, 64 x 64, 512-sample blocks at 48 kHz: 0.6 ms static, about 6 ms with every source moving (a scalar interpolation per path and sample). Milestone 4 has the performance work.
- The settings window has twelve ViMiC rows, which makes it tall; a better arrangement (sections or tabs) can wait for the room rows.

## 7c. What milestone 2 turned out like

- Per-source parameters, which the shared code did not have, were added (change 2 of section 6): `PluginParameters::perSource`, `getSourceColumns()`, source table columns, file attributes and OSC for sources.
- The source pattern is the same `(a + (1 - a) cos φ)^w` as the microphones', with `a` per source (`src<N>pattern`, default 1 = omni), the yaw per source (`src<N>yaw`) and one global exponent
  (`sourcePower`). It is never negative. A free-hand 1-degree directivity curve (j.vimic~'s editor) was not done; the formula covers omni to figure-8.
- **Orientation modes**: only static (the yaw parameter). "Center" (always facing the origin) and "follow" (facing the direction of movement; not implemented in j.vimic~ either) are left out. The default
  layout, and "Reset layout", turn every source to face the centre.
- On the pad: the radiation lobe and an arrow along the yaw for sources with a pattern below 1 (omni sources have no direction).
- Reflections (milestone 3) will use the direction from the real source to the image of each microphone, as planned in 3.2.

## 7d. What milestone 3 turned out like

- All of 3.4 as designed: images and the filter classes (the image table is checked against a brute-force enumeration by reflecting the source in the walls), per-wall absorption in three bands with presets, air absorption, the order gains, the render threshold, the fade outside the walls, the
  room on the pad. The reflection bank (the memory for the filtered copies) is made on the message thread, sized for the room, and handed over with `tlos::Exchange`.
- **Second order has 18 images**, not j.vimic~'s 12: the six images between two parallel walls (flutter echoes) are included. The total of reflections is 24 for the second order (j.vimic~: 18).
- **Absorption**: `β = sqrt (1 − α/100)`, as in j.vimic~. The crossovers are two shared settings, not one pair per wall. The surface presets are j.vimic~'s twelve (with attribution) and four textbook ones (concrete, glass, wood, plaster).
- **Source directivity of a reflection** uses the direction from the real source towards the image of the microphone in the same walls in the reverse order.
- The settings window got a scroll bar (the window is as tall as fits on the screen), headings and whole-width rows; it is long (about 1400 pixels) and wants a better arrangement.
- Cost (Release, 512-sample blocks at 48 kHz): 8 x 8 with the second order 0.44 ms static / 2.6 ms moving; 16 x 16: 1.4 ms / 10 ms. Moving sources cost most; milestone 4.

## 8. Open points for you

- **Plugin code**: `ViMc` (and bundle ID `no.trondlossius.vimic`, state tag `ViMiC`); folder `ViMiC/`.
- **`d_min`**: the manual says 1 m, the code clamps at 0.1 m. I propose a parameter, default 1 m.
- **Pitch** of microphones and sources (vertical orientation): not in the first version; with all heights equal it matters little.
- **Spread** for sources is unused. A possible use later is the apparent source size (blending the directivity); not planned.
- **Outside rolloff** is hidden for ViMiC. Positions beyond the room are allowed but unphysical (reflections fade out).
- Whether a **DBAP-equivalent "Panning" mode** (no delays, optional intensity normalisation) is worth having as a fourth delay mode;
  it is cheap, but DistPan already covers that use.

## 9. References

- J. Braasch, N. Peters, D. L. Valente, "A loudspeaker-based projection technique for spatial music applications using virtual microphone control", Computer Music Journal 32(3), 2008.
- N. Peters, J. Braasch, S. McAdams, "Sound spatialization across disciplines using virtual microphone control (ViMiC)", Journal of Interdisciplinary Music Studies 5(2), 2011.
- N. Peters, T. Matthews, J. Braasch, S. McAdams, "Spatial sound rendering in Max/MSP with ViMiC", Proc. ICMC, Belfast, 2008.
- J. B. Allen, D. A. Berkley, "Image method for efficiently simulating small-room acoustics", J. Acoust. Soc. Am. 65(4), 1979.
- R. Pellegrini, "Perception-based design of virtual rooms for sound reproduction", 22nd AES Conference, 2002.
- T. Lossius, P. Baltazar, T. de la Hogue, "DBAP – Distance-Based Amplitude Panning", Proc. ICMC, Montréal, 2009.
- N. Peters, "ViMiC – Virtual Microphone Control for Jamoma and Max/MSP", user manual (draft 2013), in github.com/Nilson/ViMiC-and-friends.
