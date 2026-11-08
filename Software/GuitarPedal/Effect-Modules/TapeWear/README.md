# Tape Wear

Tape Wear is the sound of worn tape. A modulated delay gives slow wow and fast flutter, Wear darkens the tape and adds saturation and hiss, Dropouts mutes the signal now and
then, and the second footswitch is a tape stop: while it is held the tape slows down (the pitch falls and the sound fades), and when it is released the tape runs up to speed again.

## Origin and licence

Adapted from **terrarium-tape** by **Aalenberg**: <https://github.com/Aalenberg/terrarium-tape> (`main.cpp`), Copyright (c) 2026 Aalenberg, **MIT License**
([`LICENSE-TerrariumTape-MIT.txt`](LICENSE-TerrariumTape-MIT.txt)); like the rest of this repository it is MIT, so there is no licence change to opt into. The original is a
Daisy Seed tape-wear firmware for the PedalPCB Terrarium platform.

What is the same as in the original: the delay line with wow, flutter and the optional second wow for the Unstable switch (the same depths and rates), the Wear mapping of
bandwidth, saturation and hiss, the random dropouts, and the tape stop with its speed and recovery behaviour.

What is different here:

- The original's switches (hiss, dark, unstable, extra saturation) are the menu items Hiss, Dark, Unstable and Extra Sat, and its tape stop footswitch is this pedal's second footswitch.
  Bypass is the pedal's normal bypass, not a mix of zero.
- The saturation has unity gain for small signals (the original is normalised to full scale, so it gets louder with Wear); Level is -24 to +6 dB like the other effects.
- The delay line holds 16384 samples (it was 32768) and is in SDRAM; the controls are smoothed per sample and the values that depend on them are updated every 16 samples.
- The signal is processed in blocks, and the result does not depend on how the samples are divided into blocks.

## Files

| File | |
|---|---|
| `../tapewear_module.{h,cpp}` | The effect module |
| `../../tests/host/test_tapewear.cpp` | Host tests |
