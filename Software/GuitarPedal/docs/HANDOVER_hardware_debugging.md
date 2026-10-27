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
