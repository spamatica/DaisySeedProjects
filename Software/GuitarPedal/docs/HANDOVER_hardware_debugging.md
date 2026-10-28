# Handover: memory footprint investigation, to be continued with a hardware debugger

Written 2026-10-27, at commit `e23e6cc` plus the commit "Re-measure the memory estimates for all effects" (the one after it). Not part of the user manual: a note for whoever picks the
investigation up once an SWD debugger (ST-Link or J-Link) is connected to the Daisy Seed. Nothing in it was checked on the pedal, everything was measured on the PC or read from a build.

## 1. How it started

A build of "the default configuration of the original project" in this repo failed to link:

```
build/guitarpedal.elf section `.text' will not fit in region `SRAM'
region `SRAM' overflowed by 8344 bytes
```

(The long list of `_close is not implemented` and `memory region RAM_D2 not declared` lines before it are warnings and harmless.) The question was whether recent changes
had grown the firmware past the limit.

## 2. What was established (all with arm-none-eabi-gcc 15.3.1, `-Ofast`, `APP_TYPE = BOOT_SRAM`)

| Build | SRAM (code) | DTCM | SDRAM |
|---|---|---|---|
| Original project, `main` 8e9f468, its own submodule commits, GCC 10.2.1 | 417,384 B (84.9%) | 52,928 B | 48,156,052 B |
| Original project, same, GCC 15.3 (Makefile version check overridden) | 422,212 B (85.9%) | 53,856 B | 48,156,052 B |
| This repo, `configs/default.config` (the original's effect set) | 472,376 B (96.1%) | 64,608 B | 48,156,052 B |
| This repo, default set + Dattorro + Reverb Delay (the selection that failed) | 499,864 B (101.7%, over by 8,344 B) | 64,704 B | 55,764,092 B |

* **The failing selection was not the original's default.** Dattorro (GPL, off upstream) and Reverb Delay (commented out in the upstream Makefile) cost about 27.5 KB of SRAM together.
* **The footprint did grow:** same effects, +50 KB (about 12%) compared with the original under the same compiler. Biggest contributors, by object size (code + read-only data):
  Distortion +9 KB, CloudSeed +8.3 KB, `guitar_pedal` +4.7 KB, NAM +4.5 KB, Crusher +3.6 KB, the menu font and fit-text code about +5 KB, Harmonic Tremolo, Tape Delay,
  the base effect module and Delay +1 to 2 KB each.
* **The original repository builds with both compilers.** Its Makefile insists on GCC 10 (`GCC_VERSION_MAJOR_REQUIRED := 10`), which no longer applies; override it with
  `make GCC_VERSION_MAJOR_REQUIRED=15` (and `make clean GCC_VERSION_MAJOR_REQUIRED=15`: `clean` runs the check too). The user's checkout of the original had `libDaisy` and `q`
  at older commits than `main` records and libraries built in March: run `git submodule update --init --recursive` and `./ci/build_libs.sh`.
* **`pedal_setup report` used to underestimate by about 50 KB** (its SRAM base of 187 KB was calibrated on a FunBox-only build). It was re-measured: `measure` on a build of all 67
  effects, `calibrate` on a build of the default set (base SRAM now 229 KB), and heap counts on the PC for all effects. After that the report matches the linker for the default
  set (458 K estimated against 461 K linked) and for the failing selection (488 K, "does not fit").

## 3. What is NOT known: the points for the debugger

1. **The real heap use** of the default set and of other selections. The estimate is 131 KB of the 180 KB free (the heap starts after 75 KB of static data in RAM_D2), but the per-effect
   figures are PC counts with 8-byte pointers. The biggest items are suspicious: Crusher 50 KB, Spring Reverb 37 KB, PluckEcho 35 KB, Earth 25 KB, Chorus 19 KB. Only Jupiter, Neptune (CloudSeed
   engine: 7 to 9 KB) and the framework (18 KB) were ever measured on the pedal.
2. **Stack use.** The stack is in DTCM (`_estack = 0x20020000`, growing down); nothing in the project measures how deep it gets (NAM, the CloudSeed engine and the FFT effects are the
   suspects). DTCM static data is already 49% (63 KB of 128 KB) with a 16 KB reserve assumed for the stack.
3. **What the CPU spends the block on**, per effect and per part (the Show Debug figure is the average of the whole callback only).
4. **Whether a selection that the tool calls "tight" boots and runs** (SRAM 95% for the default set).
5. **Where the 50 KB of code growth is really coming from** (section 2 is by object file; the linked sizes of functions can be read from the map file: see `build/guitarpedal.map`).

## 4. Setting the debugger up

* `.vscode/launch.json` already has a Cortex-Debug configuration: OpenOCD with `interface/stlink.cfg` and `target/stm32h7x.cfg`, SWD, the SVD file `.vscode/STM32H750x.svd`, Live Watch at
  4 samples/s, `runToEntryPoint: main`. `openocd` is installed on this machine. Its `preLaunchTask` is `build_all_debug` (see `.vscode/tasks.json`).
* **Caveat: `APP_TYPE = BOOT_SRAM`.** The Daisy bootloader copies the program from QSPI flash into AXI SRAM (`0x24000000`) and runs it there. The launch config's `load` and
  `reset init` are meant for a flash build; for this one it is simplest to first attach to the running pedal (`monitor halt` after boot) and only then try loading the ELF into SRAM
  with the debugger. Hardware breakpoints are limited to 8 (`gdb_breakpoint_override hard` is set). libDaisy's Makefile has a `DEBUG` option "for debugging bootloaded applications".
* **Check that the ELF you debug has the heap guard.** `Util/heap_guard.cpp` replaces `_sbrk` (stops at the end of RAM_D2 and shows OUT OF MEMORY on the display). In the ELF,
  `arm-none-eabi-nm -C build/guitarpedal.elf | grep -E "_sbrk|heap_end"`: a `heap_end.0` symbol means libnosys's `_sbrk` is the one linked and the guard is not active, and then Show Debug
  has no heap figure. (When this note was written the ELF in `build/` was such a build, made by the user with a new `Util/syscall_stubs.cpp`, and `build/heap_guard.o` was missing:
  check what happened there before trusting the readout.)

## 5. Measurements to take, with the commands

Addresses are for the default-set build; take new ones from `arm-none-eabi-nm` / `readelf -S` of the ELF you flash. Regions: AXI SRAM `0x24000000` (480 KB, the program), DTCM
`0x20000000` (128 KB, `.data`, `.bss`, the stack), RAM_D2 `0x30000000` (the 32 KB DMA part first, then `.sram_d2_bss` from `0x30008000`, then the heap from `end` to `0x30048000`),
SDRAM `0xC0000000` (64 MB).

* **Heap used, exactly.** With the guard linked: the file-scope variable `s_break` in `heap_guard.cpp` (an anonymous-namespace static; find it with `info variables s_break` in gdb) minus the
  linker symbol `end` (`p &end`); the project's own `heapguard::Used()` returns that. Without the guard: libnosys's `heap_end.0` in `.bss`. Read it after boot, after switching through all the
  effects, and after loading chains (the heap only grows, so the last value is also the most it ever needed). Compare with the Show Debug figure (whole KB).
* **Record what you find in the tool**, it is meant for this:
  `./pedal_setup observe "boots and runs" --heap-used <KB> --heap-free <KB> --note "default set, debugger"` and, for the framework's own heap (the base, 18 KB now, measured on a FunBox build):
  add `--set-base-heap` while the selection built is the one running. Both write into `effects-memory.json` (`observed`, `base`).
* **Per-effect heap on the pedal:** the heap did not change when effects were selected in turn (see the `observed` entries), because every effect is created at startup. To get a per-effect figure,
  read `s_break` before and after each `new` in the startup code (`loaded_effects.h` / `guitar_pedal.cpp`; the guard's `SetStage()` names the stage), or build selections with and
  without one effect and take the difference.
* **Stack high-water mark.** Fill the free DTCM below the stack with a pattern before the program runs (or from gdb right after `halt`), run the heavy cases, halt and look for the first overwritten word.
  The free part is from the end of `.bss` (`arm-none-eabi-nm build/guitarpedal.elf | grep -E " _?e?bss| end"`, `.bss` ends near `0x2000a2xx`) up to `_estack` (`0x20020000`) minus the reserve.
  gdb example: `monitor mww 0x2000b000 0xDEADBEEF 0x...` (count in words), run, `monitor halt`, `x/64wx 0x2000b000` and scan upwards.
* **Hard fault after an out-of-memory or a bad pointer:** halt in the fault handler and read `CFSR 0xE000ED28`, `HFSR 0xE000ED2C`, `MMFAR 0xE000ED34`, `BFAR 0xE000ED38`
  (`x/wx 0xE000ED28`), then `bt` / the stacked PC in the frame the handler gives (`info registers`, then `x/8wx $sp`).
* **CPU cost per piece.** Enable the cycle counter: `set *(unsigned*)0xE000EDFC |= 1<<24` (DEMCR.TRCENA), `set *(unsigned*)0xE0001000 |= 1` (DWT_CTRL.CYCCNTENA), read `DWT_CYCCNT` at `0xE0001004`.
  At 480 MHz one 48-sample block is 48 / 48000 s = 1 ms = 480,000 cycles. Read the counter around `activeEffect->ProcessStereoBlock` (or put the reads into the firmware and print them in Show
  Debug) for each effect and for the default screen draw. The project's own `CpuLoadMeter` in `guitar_pedal.cpp` gives only the average and peak of the whole callback.
* **Code size per function** needs no debugger: `arm-none-eabi-nm -C --size-sort -S build/guitarpedal.elf | tail -60`, or the map file (`-Wl,-Map` is already on).

## 6. How the figures were produced (to repeat them after changes)

All from `Software/GuitarPedal`, with `arm-none-eabi-gcc` 15.3 first on the PATH (the original project needs `GCC_VERSION_MAJOR_REQUIRED=15` on the `make` line, this repo does not).

```bash
./pedal_setup preset everything && make -k -j8     # builds every effect (the final link fails: too big, that is expected)
./pedal_setup measure                              # static figures of all effects, from the object files in build/
./pedal_setup preset default && make -j8           # a build that links
./pedal_setup calibrate                            # the base (framework) from that build's ELF size
./pedal_setup report --config configs/default.config   # compare with the linker's "SRAM:" line
```

`preset` rewrites `effects.config` and `generated/`: save your own selection first (`cp -r effects.config chains.json generated /somewhere`) and put it back afterwards. `measure` and `calibrate`
use the object files and the ELF in `build/`, so build with the same compiler that you want the figures for.

Heap counts on the PC: `tests/host/run_heap_report.sh` only covers the FunBox effects. For all of them, a harness built from `effects.json` was used (not kept in the repo):

```bash
python3 - <<'EOF'
import json
m = json.load(open('effects.json'))
head = open('tests/host/heap_report.cpp').read()
pre, meas = head[:head.index('#include "balnea_module.h"')], head[head.index('template <class M> static void measure'):head.index('int main()')]
incs = ['#include "%s"' % e['header'] for e in m['effects']]
calls = ['    measure<bkshepherd::%s>("%s");' % (e['class'], e['id']) for e in m['effects']]
open('/tmp/heap_all.cpp', 'w').write(pre + '\n'.join(incs) + '\n\n' + meas + 'int main() {\n' + '\n'.join(calls) + '\n}\n')
srcs = [s for e in m['effects'] for s in e['sources']] + [s for c in m['components'].values() for s in c.get('sources', [])]
open('/tmp/heap_all.srcs', 'w').write('\n'.join(dict.fromkeys(s for s in srcs if s.endswith('.cpp'))))
EOF
T=tests/host; DSP=dependencies/DaisySP/Source; LGPL=dependencies/DaisySP/DaisySP-LGPL/Source; CS=dependencies/CloudSeed
g++ -std=c++20 -O1 -w -I$T/stub -I$T/stub_cmsis -I. -IEffect-Modules -IUtil -I$DSP -I$DSP/Utility -I$LGPL -include daisysp-lgpl.h -Idependencies/gcem/include \
  -Idependencies/q/q/q_lib/include -Idependencies/q/infra/include -I$CS /tmp/heap_all.cpp $(cat /tmp/heap_all.srcs) Effect-Modules/base_effect_module.cpp Util/*.cpp \
  $DSP/*/*.cpp $LGPL/*/*.cpp $CS/AudioLib/Biquad2.cpp $CS/AudioLib/ShaRandom.cpp $CS/AudioLib/ValueTables.cpp $CS/FastSin.cpp $CS/Utils/Sha256.cpp -o /tmp/heap_all && /tmp/heap_all
```

The column "heap chunks" (newlib's rounding: a multiple of 8 plus a 4-byte header, minimum 16) is what went into `effects-memory.json`. Figures kept as measured on the pedal: Jupiter, Neptune, CloudSeed
(the host count for the CloudSeed engine, 164 KB, is far too high). The host has 8-byte pointers, so pointer-heavy objects are overestimated: **that is the first thing the debugger can
correct** (section 3, point 1).

## 7. Loose ends

* `pedal_setup report` is only as good as `effects-memory.json`: re-run `measure` (and `calibrate`) after changing an effect's code, and `report` against the real linker output.
* The memory estimates and the heap guard's startup check (`generated/effects_heap.inc`) use the same heap figures: if the PC counts turn out too high, the HEAP WARNING at startup will cry wolf.
* The LED fix (`Led` zero-initialisation, commit 055707c) was backported to a checkout of the original project as branch `fix-led-init` (rebased on its `main`, pushed to the user's fork
  twice, once force-pushed, and no PR is open); the user wants nothing pushed to any remote without being given the exact command to run first.
* A refactoring of `guitar_pedal.cpp` (commit 545dd84 in this repo: `processPots`, `processFootswitches`, `updateLeds`) was applied to the original checkout and later dropped from its working tree. It can be
  recreated with `git diff 545dd84~1 545dd84 -- Software/GuitarPedal/guitar_pedal.cpp` and applied with `git apply` (it applied cleanly on the old base and with offsets on the new one).

## 8. Update 2026-10-28: debugger working, toolchain comparison

* **Debugger:** an ST-Link V3 (`0483:3754`, firmware V3J8M3) is passed through to the VM and OpenOCD 0.12 attaches over SWD (STM32H750, Cortex-M7 r1p1, 8 breakpoints, 4 watchpoints). It needed `openocd` installed and the udev rule for the ST-Link V3 (`49-stlinkv3.rules`) in `/etc/udev/rules.d`. Halting and resuming the pedal is clean. Note that `mdw` prints nothing through `-c` on the command line here: use `echo [read_memory <addr> 32 <count>]`.
* **A stale firmware on the pedal:** the ELF in `build/` at the time did not match what was running (reset vector `0x24009e25` on the pedal, `0x2400a31d` expected), so compare the vector table before trusting any readout.
* **Clean builds of the same selection (`effects.config`, 34 effects, `make -j4`, libDaisy/DaisySP/CloudSeed rebuilt with `ci/build_libs.sh`):**

| Region | GCC 13.3.rel1 | GCC 15.3.rel2 (15.3.1) |
|---|---|---|
| SRAM (code) | 471,576 B (95.94%) | 477,756 B (97.20%) |
| DTCM | 53,344 B | 53,344 B |
| RAM_D2 (static) | 77,184 B | 77,184 B |
| SDRAM | 50,498,016 B | 50,498,016 B |

  GCC 15.3 makes the code about 6 KB (1.3%) larger; the static data is identical. `s_break` is at `0x20005fd0` and `end` at `0x3001ad80` in both. All the figures in section 2 and in `effects-memory.json` are from 15.3, so use that compiler (first on the PATH, for example `export PATH=$HOME/bin/arm-gnu-toolchain-15.3.rel2-x86_64-arm-none-eabi/bin:$PATH`) when comparing.

## 9. Results 2026-10-28: all configurations run on the pedal (GCC 15.3.1, `-j4`)

Measured with `tools/pedal_measure.sh` (new): it lets the Daisy bootloader start the app that is in QSPI, takes over at that app's entry point, loads the new ELF into AXI SRAM
over SWD and starts it. Nothing is written to flash. Why it is done this way: `DaisySeed::Init` skips the clock set-up and the SDRAM init when it finds a bootloader older than v6
and the program in SRAM (it assumes the bootloader did them), so a plain `reset halt` + load faults at the first SDRAM access (and the FPU is off too). It needs one working app in
QSPI. `-s` traces every `_sbrk` call with its call stack, to attribute the heap to the effect (or the global object) that allocated it. Each run: 15 s after boot at the default
effect, nothing touched. No run had a fault (CFSR = HFSR = 0), except `funbox-all`, which stopped in the heap guard as predicted.

### Per configuration (linked figures from the build, heap read from the running pedal)

| Configuration | effects | SRAM | DTCM | RAM_D2 static | heap used | heap free | estimate (effects + 18 K base) |
|---|---|---|---|---|---|---|---|
| current | 33 | 467 K | 52 K | 75 K | 152.4 K | 28.2 K | 150 K |
| default | 31 | 461 K | 63 K | 75 K | 152.1 K | 28.5 K | 149 K |
| distortions | 14 | 273 K | 37 K | 0 K | 64.3 K | 191.7 K | 99 K |
| funbox-jupiter-neptune | 7 | 331 K | 74 K | 94 K | 60.4 K | 101.9 K | 78 K |
| funbox-newest | 6 | 288 K | 73 K | 94 K | 45.3 K | 116.9 K | 62 K |
| funbox-working | 3 | 311 K | 108 K | 122 K | 29.0 K | 105.3 K | 63 K |
| uranus-only | 1 | 207 K | 39 K | 94 K | 15.4 K | 146.9 K | 48 K |
| group1 | 13 | 285 K | 38 K | 0 K | 97.9 K | 158.1 K | 143 K |
| funbox-all | 5 | 333 K | 111 K | 216 K | 40.3 K | 0.2 K | 83 K |

`funbox-all` (Saturn 121 K + Uranus 93 K of static RAM_D2 leave 41 K for the heap) runs out of heap in Jupiter's `Init` ("OUT OF MEMORY"): the report's "does not fit" is right.
`current` is the selection in `effects.config` at the time (34 effects); `group1` is the 12 effects that no preset has (auto_filter, dattorro, duck_compressor, fm_keys, funbox-earth,
midi_keys, modal_keys, pluckecho, spring_reverb, string_keys, test_tone, vowel); the other names are `configs/*.config`.

### What the heap is spent on (bytes, from the `_sbrk` trace; the same in every configuration that has the effect)

| Effect / object | measured | estimate in effects-memory.json | |
|---|---|---|---|
| Crusher (the Q pitch detector's `zero_crossing_collector`, in `Init`) | 49,160 | 50,480 | ok |
| Spring Reverb | 36,688 | 36,776 | ok |
| PluckEcho | 34,560 | 35,000 | ok |
| Earth | 21,864 | 24,584 | ok |
| Chorus | 19,380 | 19,456 | ok |
| Venus | 16,836 | 17,280 | ok |
| Uranus | 12,840 | 12,992 | ok |
| Jupiter, Neptune (the CloudSeed engine) | 5.7 to 8.8 K / 6.5 K | 9,216 / 7,373 | a bit high |
| NAM, IR, Mars, Granular Delay, FM Keys, Dist Bench | within 1 to 2 K of the estimates (Dist Bench 9.6 K against 11.9 K) | | ok |
| **PolyOctave: a global object (`polyoctave_module.cpp:21`)** | **16,144** | 200 | **missing** |
| **SciFi: a global object (`scifi_module.cpp:22`)** | **10,760** | 264 | **missing** |
| **IR: three global objects (`ir_data.cpp:120, 237, 239`)** | **16,448** (on top of the module's 9.2 K) | (9,248) | **missing** |
| Saturn | 980 | 1,808 | high |
| Dattorro | 88 | 2,280 | high |
| Framework: hardware module and `GuitarPedalUI` pages | about 1.5 to 4.5 K (grows with the number of effects) | 18 K base | high |

The estimates match the pedal for everything that is allocated in a constructor or in `Init()`, and **miss everything allocated by global constructors** (they run before `main`
and are not in the host harness): PolyOctave, SciFi and the IR data, 43 K of the 152 K used by the 34-effect selection. Two errors in the other direction hide that in the
totals: the 18 K framework base is about 3 K in practice (Uranus alone: 15.4 K used, 12.8 K of it Uranus), and some estimates are high (Saturn, Dattorro, Dist Bench). So `report`
happens to be right for `default`/`current` (149 K against 152 K) and 2 to 3 times too high for small selections (`uranus-only` 48 K against 15 K, `funbox-working` 63 K against 29 K,
`distortions` 99 K against 64 K). The first version of the script (one reading per `SetStage`) shows 0 for most effects because they allocate in their constructors, which all run in
`load_effects`: use `-s`.

### Stack and the rest
* Stack high-water mark after boot and 15 s idle: **9,544 bytes** (lowest word touched `0x2001dab8`) in every configuration: the deepest use is in the common start-up code. This
  says nothing about NAM, the CloudSeed engine or the FFT effects while they run: nothing was selected or played, that needs the second phase (select an effect from gdb, paint,
  run, read).
* CPU cost per effect: not measured.
* `generated/effects_heap.inc` in git was stale (zeros where `effects-memory.json` has the heap estimates); regenerating it (`make` or `python3 tools/effects.py generate`) changes it.


### What was updated from these results (2026-10-28)
* `effects-memory.json`: the per-effect `heap` of the 18 effects whose figure was off or missing (Spring Reverb, PluckEcho, Earth, Chorus, Venus, Uranus, Dist Bench, NAM, IR, Mars, Granular Delay, FM Keys, Crusher, Saturn, Dattorro, CloudSeed, and PolyOctave and SciFi, which include their global objects), each with a `heap_source`; the new component heap of `impulse_response` (16,448 B: the IR data, built with IR, NAM or Mars); the base heap 18 K -> 3 K; nine entries in `observed`.
  Jupiter and Neptune were left as they were (5.7 to 8.8 K on the trace, 9.2 K and 7.4 K in the file: they vary with what else is built in); the small effects (under 0.5 KB) were left at their host counts.
* `tools/effects.py`: `budget` adds a component's `heap` figure (it ignored it before).
* With these, `pedal_setup report` is within 7% of the pedal for the eight complete runs (-3% to +7%); `funbox-all` stopped at the heap guard, so its figure is a lower bound.
* Stack depth and CPU cost per effect: measured since, section 10.

## 10. Results 2026-10-28: stack and CPU per effect, on the pedal

Measured with `tools/pedal_profile.py` (new), on the same 8 builds as section 9 (GCC 15.3.1, `-Ofast`): the effect is selected from the debugger (it writes `effectOn` and
`pendingEffectIDFromAudioCallback`, so the firmware's own `SetActiveEffect` runs), left alone for 1.5 s, then the audio callback is measured for 4 s. All 67 effects were selected
correctly. The target is only halted at the entry of the audio callback, so no block is cut in the middle.

* **CPU** is libDaisy's `CpuLoadMeter` (average, smoothed) and its maximum, for the whole audio callback: 100% means a block (48 samples, 1 ms at 48 kHz) takes as long as it lasts, more is
  an overrun. It **includes the callback without an effect: 2.3% (1.5% on the FunBox builds)**, subtract that for the effect's own cost.
* **Callback stack** is what the audio callback and everything under it needs: at its entry the stack below is painted, the callback is run to its return address (so the main loop cannot
  interleave), the lowest overwritten word is the depth; the deepest of 12 blocks. **Whole stack** is the high-water mark of everything (the main loop with the menus plus the callback) over the
  switch and 4 s of running; it is dominated by the display/menu code in the main loop (4.7 to 6.9 KB, whatever the effect), so only the larger values mean anything. At boot the stack went
  to 9,544 bytes in every build (start-up code). The stack is 128 KB DTCM minus 63 KB of static data: nothing here comes near the limit.
* **No audio input**: the pedal has nothing connected, so effects process silence. Effects that do less without a signal (gates, detectors, the synths with no notes held) can cost more
  with one: treat their figures as lower bounds.

| Effect | CPU avg | CPU peak | callback stack | whole stack |
|---|---|---|---|---|
| nam | 64.8% | 65.1% ** | 560 B | 6848 B |
| funbox-neptune | 53.2% | 59.8% | 544 B | 6896 B |
| spectral_delay | 53.1% | 71.4% ** | 320 B | 6608 B |
| cloudseed | 50.8% | 59.0% | 360 B | 6688 B |
| midi_keys | 50.4% | 50.5% | 260 B | 6548 B |
| scifi | 49.8% | 54.7% | 520 B | 6812 B |
| funbox-mars | 43.9% | 44.2% | 1588 B | 7876 B |
| funbox-jupiter | 43.2% | 49.2% | 352 B | 6640 B |
| modal_keys | 40.1% | 40.5% | 356 B | 6644 B |
| tape_delay | 38.5% | 44.4% | 524 B | 6812 B |
| funbox-mercury | 37.7% | 37.8% | 220 B | 6508 B |
| polyoctave | 35.5% | 36.1% | 392 B | 6680 B |
| funbox-saturn | 31.7% | 87.9% ** | 496 B | 6784 B |
| funbox-earth | 31.4% | 35.3% | 516 B | 6804 B |
| dattorro | 30.4% | 33.5% | 300 B | 6588 B |
| spring_reverb | 25.9% | 26.2% | 272 B | 6560 B |
| reverb_delay | 25.8% | 33.2% | 388 B | 6676 B |
| ir | 24.5% | 24.9% | 276 B | 6564 B |
| drum | 23.2% | 78.8% ** | 332 B | 6700 B |
| dual_echo | 21.7% | 22.1% | 340 B | 6628 B |
| funbox-venus | 20.8% | 76.6% ** | 592 B | 6880 B |
| funbox-uranus | 20.6% | 21.0% | 348 B | 6636 B |
| pluckecho | 20.2% | 20.5% | 280 B | 6568 B |
| multi_delay | 19.4% | 25.8% | 344 B | 6632 B |
| delay | 19.1% | 23.2% | 308 B | 6596 B |
| string_keys | 16.4% | 17.3% | 308 B | 6596 B |
| funbox-cassini | 15.0% | 16.7% | 364 B | 6652 B |
| granular_delay | 14.8% | 15.3% | 296 B | 6584 B |
| phaser | 13.9% | 14.3% | 236 B | 6524 B |
| reverb | 13.0% | 14.9% | 272 B | 6560 B |
| looper | 10.4% | 10.8% | 256 B | 6544 B |
| pitch_shifter | 10.0% | 11.3% | 276 B | 6564 B |
| chorus | 9.0% | 9.3% | 268 B | 6556 B |
| flanger | 8.9% | 9.2% | 252 B | 6540 B |
| tuner | 8.7% | 9.0% | 268 B | 6556 B |
| autopan | 8.6% | 9.1% | 288 B | 4836 B |
| funbox-balnea | 8.3% | 9.0% | 356 B | 6036 B |
| multi_phaser | 8.1% | 8.2% | 312 B | 6600 B |
| distortion | 7.1% | 7.3% | 568 B | 6864 B |
| harmonic_tremolo | 6.9% | 7.3% | 248 B | 6536 B |
| modulated_tremolo | 6.5% | 7.1% | 248 B | 6536 B |
| noise_gate | 6.4% | 6.6% | 212 B | 6500 B |
| chainsaw | 6.0% | 6.1% | 212 B | 6500 B |
| vowel | 5.8% | 6.0% | 280 B | 6568 B |
| muff_fuzz | 5.4% | 5.7% | 212 B | 6500 B |
| metro | 5.1% | 5.5% | 236 B | 6524 B |
| preamp | 5.0% | 5.1% | 212 B | 6500 B |
| compressor | 4.7% | 5.0% | 236 B | 6524 B |
| duck_compressor | 4.6% | 4.7% | 236 B | 6524 B |
| auto_filter | 4.3% | 4.3% | 216 B | 4836 B |
| dist_bench | 4.2% | 4.2% | 536 B | 6824 B |
| bass_dist | 4.1% | 4.1% | 212 B | 4664 B |
| crusher | 3.9% | 4.2% | 268 B | 6556 B |
| tape_sat | 3.9% | 3.9% | 212 B | 6500 B |
| funbox-pluto | 3.8% | 4.0% | 300 B | 6628 B |
| test_tone | 3.8% | 3.8% | 240 B | 6528 B |
| geq | 3.7% | 3.9% | 212 B | 6500 B |
| fm_keys | 3.6% | 3.8% | 224 B | 6512 B |
| octave_fuzz | 3.6% | 3.6% | 212 B | 6500 B |
| screamer | 3.5% | 3.6% | 212 B | 6500 B |
| wavefolder | 3.5% | 3.7% | 224 B | 6512 B |
| overdrive | 3.5% | 3.7% | 212 B | 6500 B |
| exciter | 3.4% | 3.5% | 212 B | 6500 B |
| booster | 3.4% | 3.6% | 212 B | 6500 B |
| chopper | 3.1% | 3.4% | 212 B | 6500 B |
| peq | 3.1% | 3.3% | 212 B | 6500 B |
| scope | 2.7% | 2.8% | 212 B | 6500 B |

Things the table shows:
* **Heaviest: NAM 65%, Neptune 53%, Spectral Delay 53%, CloudSeed 51%, MIDI Keys 50% (idle voices, no notes), SciFi 50%, Mars 44%, Jupiter 43%.** Everything under about 15% is a single filter,
  distortion, modulation or small delay.
* **Peaks far above the average (bold): Saturn 88% (avg 32%), Drum 79% (avg 23%), Venus 77% (avg 21%).** They are the effects that do their heavy work in one block out of several (FFT hops,
  stage changes): the average hides that a block gets close to overrunning (100%), and any other load in the same block (the display, MIDI) would tip it. Worth looking at before adding anything to those.
* **Stack: 212 to 592 bytes for the callback, except Mars at 1,588 bytes** (the largest; its whole-stack figure is 7.9 KB, the largest too). The effects keep their buffers in static memory, not
  on the stack.
* **The heap grew slightly while the effects were selected one after the other:** 156,048 -> 157,912 bytes over the 33 entries of the 34-effect build (the menus are rebuilt with new/delete on
  each switch), 46,432 -> 46,672 over the six of `funbox-newest`: tens of bytes per switch, but not zero (section 9 said it did not change).
* Effect chains (`chains.json`) were not measured: none of the builds above had one in its list, only single effects.

