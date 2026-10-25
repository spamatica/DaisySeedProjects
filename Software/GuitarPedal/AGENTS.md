# AGENTS.md

Firmware for a multi-effect guitar pedal on the Electro-smith Daisy Seed (STM32H750, ARM Cortex-M7), C++20. Hardware variants: 125B (default), 1590B, 1590B_SMD, Terrarium, FunBox.
User-facing docs: [README.md](README.md), [docs/](docs/) (SETUP, GENERATE_FIRMWARE, PERFORMANCE_AND_MEMORY, EFFECTS, USER_MANUAL_125B). Release history: [CHANGELOG.md](CHANGELOG.md).

## Build

Run from this directory (`Software/GuitarPedal`).

```bash
make -j8                      # build for 125B; add VARIANT=1590B | 1590B_SMD | TERRARIUM | FUNBOX
make program-dfu              # flash over USB DFU (waits for the device; press RESET on the pedal)
./ci/build_libs.sh            # build libDaisy, DaisySP, CloudSeed (once, and after dependency changes)
./ci/format.sh                # clang-format the project (.clang-format: LLVM-based, 135 columns)
```

- Needs `arm-none-eabi-gcc`; a `.gcc_path` file (optional) points the Makefile at a specific toolchain.
- `dependencies/` holds libDaisy, DaisySP, CloudSeed, q, gcem (git submodules / vendored). Third-party code is included with `-isystem`.
- `-Ofast`, `APP_TYPE = BOOT_SRAM` (needs the Daisy bootloader). `nam_a2_sections.lds` is an INSERT linker fragment that places the NAM A2 history buffer in RAM_D2/AXI SRAM.

## Choosing which effects are built (important)

Not every effect is compiled: memory is tight (see below). The selection is data-driven:

- `effects.json` — manifest of every effect (class, header, sources, `requires` components, license). **Its order is the menu order and the layout of saved settings.**
- `effects.config` — the active selection, `ID=y` lines. Presets in `configs/*.config`.
- `chains.json` — effect chains (several effects as one menu entry, implemented by `EffectChain`).
- `effects-memory.json` — per-effect memory costs (measured + estimates).
- `tools/effects.py` (wrapper `./pedal_setup`) — TUI and CLI: `list`, `enable`, `disable`, `preset`, `report`, `matrix`, `info`, `lint`, `measure`, `calibrate`, `observe`, `generate`.
- `generated/` (`effects.mk`, `defines.mk`, `effects_*.inc`) is **generated — do not edit by hand**. `make` regenerates it when the inputs change; `loaded_effects.h` includes the `.inc` files.
- Effects with `license: gpl` (Dattorro, Earth) need `ALLOW_GPL=y` in `effects.config`.
- To add an effect: write the module, add an entry to `effects.json` (sources, header, class, requirements), enable it, then `./pedal_setup measure`/`calibrate` after a build.
- NAM models: one header per model in `Effect-Modules/Nam/models/`; `Effect-Modules/Nam/update_models.sh` regenerates `models_list.inc` (the Makefile and test runner call it automatically).

## Code layout

- `guitar_pedal.cpp` — `main`, audio callback, footswitch/knob/MIDI handling, hardware variant selection.
- `guitar_pedal_storage.{h,cpp}` — persistent settings and presets (QSPI flash); autosave. Layout depends on effect order/parameter counts, so changing them resets saved settings on the next boot.
- `Hardware-Modules/` — `BaseHardwareModule` plus one class per variant (`guitar_pedal_125b`, `1590b`, `1590b-SMD`, `terrarium`, `funbox`): pins, knobs, switches, LEDs, display.
- `UI/` — OLED menu system (`guitar_pedal_ui`, effect selection/menu items, `fit_text`, font).
- `Effect-Modules/` — `BaseEffectModule` ([base_effect_module.h](Effect-Modules/base_effect_module.h)) and one `*_module.{h,cpp}` per effect (some with a subdirectory: Nam, ImpulseResponse, Dattorro, the FunBox ports Venus/Earth/Jupiter/Saturn/Neptune/Uranus/Balnea/Pluto/Cassini/Mars/Mercury, SimpleCompressor, Delays). `effect_chain.{h,cpp}` combines effects serially.
- `Util/` — shared DSP and helpers (pitch shifter, tap tempo, STFT, granular, dist_dsp, spring_dsp, `heap_guard`, `chain_switches`, ...).
- `tests/host/` — PC-side tests (see below). `docs/` — documentation. `tools/` — effect selection tool.

## Writing an effect module

Subclass `bkshepherd::BaseEffectModule`:
- Declare parameters as `ParameterMetaData` (name, type Float/Bool/Binned/Raw, default, knob mapping, MIDI CC, min/max, unit/`displayFn` for menu display) and call `InitParams(count)`; react to edits in `ParameterChanged(id)`.
- Process audio in `ProcessMonoBlock` / `ProcessStereoBlock` (block processing is the preferred path, it lowers CPU); `ProcessMono` / `ProcessStereo` are the per-sample fallbacks. Keep both equivalent.
- Optional overrides: `GetBrightnessForLED`, `GetMaxTrailSeconds` (trails when bypassed), `SetTempo`, alternate-footswitch hooks, `DrawUI`, `OnNoteOn/Off`, `GetDebugText`.
- Parameter names/choices must fit the display font (`./pedal_setup lint`).
- Many modules are single-instance only (static buffers) and cannot be used twice in a chain.

## Memory and performance constraints

- The heap lives in RAM_D2 (256 KB, shared with `.sram_d2_bss`) and **sbrk is unchecked** — over-allocation locks up at boot. Large buffers go in SDRAM (`DSY_SDRAM_BSS`, slow). Use `./pedal_setup report` to see whether a selection fits, and read [docs/PERFORMANCE_AND_MEMORY.md](docs/PERFORMANCE_AND_MEMORY.md) before adding buffers or effects.
- Global buffers in an object file stay in the firmware even if unused, which is why only selected effects' sources are compiled.
- Avoid heap allocation / blocking work in the audio callback.

## Tests

PC-only, no hardware or ARM toolchain (g++ C++20 + python3), in `tests/host/` with a stubbed libDaisy (`tests/host/stub`). See [tests/host/README.md](tests/host/README.md).

```bash
tests/host/run_tests.sh          # block-vs-per-sample equivalence and per-effect tests; BASE_REF=<ref> sets the baseline (default main)
tests/host/run_level_tests.sh    # output level tests
tests/host/run_heap_report.sh    # heap use per effect
tests/host/run_defaults_audit.sh # defaults audit
```

When changing an effect's DSP or adding block processing, add or extend a `test_*.cpp` there.

## Conventions

- Namespace `bkshepherd`; module files named `<name>_module.{h,cpp}`; format with clang-format before committing.
- Pre-release: effect order, parameters and presets may change freely (breaking changes are fine until the next stable release). Note user-visible changes in CHANGELOG.md (it has "Breaking or reset-worthy" and "New effects" sections).
- Some FunBox ports are GPLv3 (opt-in); keep license headers and the `origin` info in `effects.json`.
