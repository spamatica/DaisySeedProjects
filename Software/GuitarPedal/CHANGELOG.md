# Changelog

- Support for recent gcc versions
- Block processing instead of per sample processing (this lowers cpu usage for most effects)
- Normalized volume levels for many effects

## Breaking or reset-worthy
- **Tap tempo needs three taps** (it was a double-tap) and takes the tempo from the last two.
- **Reverb Delay delay time** is 50-1500 ms (was 50-4000 ms) with a default of 400 ms; a saved delay time plays shorter.
- **Delay:** Delay Time is 50-2000 ms (was 50-4000 ms) with a default of 400 ms (was 2025 ms), and Delay Mix defaults to 35% (was 50%).
- **Balnea delay time** is 100-2000 ms (was up to 4000 ms) and its tap window is 2 s (was 4 s).
- **Crusher defaults** are 8 bits, 8000 Hz and a 6000 Hz cutoff 

## New effects
- **FunBox ports:** Venus, Earth (GPLv3 opt-in), Jupiter, Saturn, Neptune, Uranus, Balnea, Pluto, Cassini, Mars and Mercury (the newest five need testing on hardware).
- **Duck Compressor:** a side-chain compressor with a Key Input parameter and a gain-reduction readout.
- **Test Tone:** a signal generator (sine, square, triangle, noise) for calibrating levels.
- **Preamp:** a light amp-like tone (two gain stages, tone stack, Clean/Crunch/Lead), a cheaper alternative to a NAM model.
- **Screamer:** Tube Screamer-style overdrive (mid hump, clean low end) with Classic, Asymmetric and Hard modes.
- **Muff Fuzz:** Big Muff-style fuzz (booster and two clipping stages, scooped-mid tone control).
- **Wavefolder:** an anti-aliased sine wavefolder for metallic, synth-like harmonics.
- **Exciter:** a Chebyshev harmonic exciter (2nd to 5th harmonic, a level for each).
- **Bass Dist:** a distortion that keeps the lows clean (Linkwitz-Riley split, soft, hard or fuzz clip).
- **Octa Fuzz:** an Octavia-style octave-up fuzz.
- **Tape Sat:** tape and transformer saturation with warmth and high roll-off.
- **Spring Reverb:** a physical-modelling spring reverb with 2 to 6 springs.
- **Multi Phaser:** up to four phasers in one effect
- **Booster:** a gain stage with four types: Clean, Treble, Mid and Warm.
- **Chainsaw:** an HM-2-style heavy metal distortion, the "Swedish chainsaw"
- **Dist Bench:** a test bench with up to four distortion stages in series.
- **Auto Filter:** an envelope filter (auto wah) in five types: Dynamic (Boss FT-2), Mutron (Mu-Tron III), Q-Tron, Auto Wah and Bass; Sens, Range, Peak, Decay, Up or Down, and low, band or high pass.

## Settings and display
- **Effect chains: Switches** (a parameter of every chain of two or more effects): the two footswitches switch two of its effects on and off (any pair of them, for a chain of any size: 2+1, 3+1, 3+2, 4+1, ..., named left to right like the footswitches), each LED showing its effect; saved with the chain's presets.
- **New global settings:** In Gain and Out Gain (-24 to +24 dB), Trails, Lock Knobs (off by default); Show Debug is now saved.
- **Show Debug readouts:** CPU percentage, averaged input and output peak levels, heap use with an out-of-memory guard.
- **Menus:** long texts fit the display (auto-fit font, `~` cut, 8x13 bold font); parameters show units and decimals.
- **Harmonic Tremolo LED:** blinks once per LFO cycle (once per beat after a tempo tap) instead of dimming with the LFO.

## Fixed
- **Distortion:** two Distortions (in an effect chain or the Dist Bench) no longer share their filters; a single Distortion sounds exactly as before.
- **Venus:** Oct Up+Dn now also shifts down.
- **LEDs** work with GCC 13.
- **Build:** Jupiter plus Neptune builds; a changed CloudSeed pool size rebuilds everything.

## Performance
- **Block processing** for NAM (no added latency), IR, CloudSeed, Reverb, Dattorro, Tape Delay, SciFi, Reverb Delay (35% / 40% down to 26% / 33% CPU), Distortion (Hard Clip 18% down to 12% CPU), Overdrive (about 4%), Crusher, Chopper, Modulated Tremolo and Harmonic Tremolo; the output is identical to the per-sample paths.
- **Distortion pre-filter:** its cutoff (which follows the input level) is now updated every 8 samples instead of every sample, a large saving; the level and the sound are unchanged for practical purposes.
- **6 KB of SRAM saved** (GCC no longer clones `ProcessSamples`).
- **Default levels** are calibrated against a -37 dBFS reference; several effects apply their defaults at start-up.

## NAM
- **NAM A2 runtime computes four frames at a time** (the layers are accumulated in tiles of four frames of the 48-sample block, so each weight loaded serves four frames; after the A2-Lite kernel of squatch-dsp): the same output (bit for bit without fast-math), the same latency, a few more history columns per layer (+552 B). Measured on the pedal: the NAM load went from 69% to 65%; `NAM_A2_TILE4=0` selects the previous 2-frame kernels.
- **One file per model** in `Effect-Modules/Nam/models/`; adding or removing a model needs no other edit.
- **`update_models.sh`** can calibrate all models to the same level (default +12 dB).
- **Reference test:** the NAM effect matches NeuralAmpModelerCore to 106-133 dB signal/error.

## Build tool (`pedal_setup`)
- **Clear everything** (key `x`): deselects every effect, removes the chains and forbids the GPL effects again, to start a selection from scratch (asks first; nothing is written until saved, and an empty selection cannot be saved).
- **Effect selection** in an interactive full-screen tool (`effects.config`, presets in `configs/`); it asks before allowing a GPL effect.
- **Chains** can be made in the tool and are generated into the build.
- **Memory budget** report and a matrix of which effects fit together; boot results are recorded in `effects-memory.json`.
- **Only the chosen effects** are compiled.

## Documentation
- **`GENERATE_FIRMWARE.md`:** the tool, the memory limits, the GPL opt-in and chains.
- **`SECOND_FOOTSWITCH_AND_LED.md`:** how every effect uses the second footswitch and LED, and open points.
- **`PERFORMANCE_AND_MEMORY.md`** section 9: CPU load measured on the pedal and what it means for chains.
- **User manual:** no default firmware, all settings listed, three-tap tempo.
