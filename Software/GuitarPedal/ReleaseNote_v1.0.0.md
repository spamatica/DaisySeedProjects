# Release Note DaisySeedProjects Spamatica fork v1.0.0

This is the release note for the first version of the Spamatica fork of DaisySeedProjects by 
bkshepherd. It contains AI assisted refinements and additions to the firmware with the
direct purpose of being more end-user friendly and add more effects.

Not everything has been tested as it is a huge undertaking and not all necessary hardware has 
been available, for instance midi is untested (though not touched) in this release.

## General changes
- Block processing instead of per sample processing for many effects (to lower cpu usage)
- Normalized volume levels for many effects
- `pedal_setup` tool for configuring what to include in a firmware
- Parameters have been changed to have units (e.g. ms for delay time)
- Display text printing with auto resize of font
- Support for recent gcc versions

## New effects
- **FunBox ports:** Venus, Earth (GPLv3 opt-in), Jupiter, Saturn, Neptune, Uranus, Balnea, Pluto, 
  Cassini, Mars and Mercury.
- **Duck Compressor:** a side-chain compressor with a Key Input parameter and a gain-reduction readout.
- **Test Tone:** a test effect with signal generator and latency measurement.
- **Preamp:** a light amp-like tone with different voicings.
- **Screamer:** Tube Screamer-style overdrive.
- **Muff Fuzz:** Big Muff-style fuzz.
- **Wavefolder:** an anti-aliased sine wavefolder for metallic, synth-like harmonics.
- **Exciter:** a Chebyshev harmonic exciter.
- **Bass Dist:** a distortion that keeps the lows clean.
- **Octa Fuzz:** an Octavia-style octave-up fuzz.
- **Tape Sat:** tape and transformer saturation with warmth and high roll-off.
- **Spring Reverb:** a physical-modelling spring reverb with 2 to 6 springs.
- **Multi Phaser:** up to four phasers in one effect.
- **Booster:** a gain stage with four types: Clean, Treble, Mid and Warm.
- **Chainsaw:** an HM-2-style heavy metal distortion, the "Swedish chainsaw".
- **Dist Bench:** a test bench with up to four distortion stages in series.
- **Auto Filter:** an envelope filter (auto wah) with several different types.
- **Vowel:** a vowel (formant) filter with a lot of settings.
- **Tape Wear:** worn tape (wow and flutter, wear, hiss, dropouts) with a tape stop on the second 
  footswitch; adapted from [terrarium-tape](https://github.com/Aalenberg/terrarium-tape) by Aalenberg.
- **Dirty Verb:** a dark ambient reverb with multiple distortions in the process loop and tail.

## Effect-chains
- Configuration of pedal-chains can now be done in `pedal_setup` which should make using it easier 
  without needing to understand the details.

## NAM
- Some optimizations, block processing etc.
- Reworked inclusion of models to make it easier to add models from e.g. tone3000. Models can now be
  dropped (even zip files) in the folder `Effect-Modules/Nam/models_nam/` and be included with `pedal_setup`
- Automatic level calibration if a c++ compiler is available for `pedal_setup` (might not be the 
  case under Windows).

## Build/configuration tool (`pedal_setup`)
- UI tool for selecting effects and NAM models.
- **Chains** can be made in the tool and are generated into the build.

## Settings and display
- **Global: In Gain and Out Gain:** Globally adjust gain -24 to +24 dB.
- **Global: Show Debug readouts:** CPU percentage, averaged input and output peak levels, heap use and 
  out-of-memory guard.
- **Global: Trails:** Allows for effects to ring out it's tail when effect is switched off.
- **Global: Lock knobs:** Disables all potentiometers so accidentally adjusting them causes no harm. 
  All parameters can still be reached from the rotary encoder.
- **Effect chains: Switches** The two footswitches switch two of its effects on and off (configurable).
