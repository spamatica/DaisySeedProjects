# Handover: building on Windows, to be tried on a Windows computer

Written 2026-11-01, at commit `d371e72` (Dirty Verb). Not part of the user manual: a note for whoever picks up the Windows build once a Windows computer is at hand.
**Nothing in it was tried on Windows.** It comes from reading this repository on Linux and from two pages of Electro-Smith's documentation. Every finding is tagged with how
sure it is:

* **[repo]**: checked in this repository's code or files.
* **[Daisy docs]**: stated on Electro-Smith's Windows pages (see section 6).
* **[assumed]**: general knowledge about Windows, Git for Windows, Python and GNU make, not verified. These are what the Windows session should confirm or refute.

The request that started it: add and document a `pedal_setup.bat`, and look through the rest of the documentation for anything that could be clarified for a Windows build. This
note is the review; the `.bat`, the code changes and the documentation have **not** been written yet (section 4 lists them, in the order to do them).

## 1. What a Windows user has to work with

* The firmware builds with `make` and `arm-none-eabi-gcc`; the effects are chosen with `./pedal_setup`, a bash script that starts `tools/effects.py` with `python3` [repo].
* The docs do not mention Windows at all: not in `README.md`, `docs/SETUP.md`, `docs/GENERATE_FIRMWARE.md` or `docs/USER_MANUAL_125B.md` [repo]. `docs/SETUP.md` step 1 only links to
  Electro-Smith's C++ development environment tutorial.
* Electro-Smith's own Windows route [Daisy docs]: install **Git for Windows** (it provides **Git BASH**), then the **Daisy Toolchain Installer** (`DaisyToolchain-1.1.0-win64.exe`),
  and use **Git BASH** as the terminal; a Python that is **not** the Microsoft Store one, started as `python` (their page says `py` may work but their scripts expect `python`);
  **Zadig** to reset the USB driver before flashing. Their tutorial says `mkdir build` fails ("CreateProcess ... failed") when the build is not run from Git BASH.

## 2. Findings

### 2.1 `python3` is hard-coded [repo]

It is in the `pedal_setup` script (`exec python3 tools/effects.py`) and in the Makefile's rule that regenerates `generated/effects.mk` and `generated/defines.mk`
(`python3 tools/effects.py generate`). A Windows Python provides `python` and `py`, not `python3` [assumed; Daisy's page agrees that the command is `python`].
Expected result: `./pedal_setup` fails in Git BASH, and `make` fails the first time it has to regenerate the effect files. Since the generated files are no longer in git (2026-11), that is the first `make` of every fresh clone, not just after `effects.config` changes.

### 2.2 Line endings: scripts checked out with CRLF [repo for the config, assumed for the effect]

The only rule in `.gitattributes` (at the repository root) is `* text=auto`. With Git for Windows' default `core.autocrlf=true`, text files are checked out with CRLF, including
`pedal_setup`, `ci/*.sh`, `tools/pedal_measure.sh` and `Effect-Modules/Nam/update_models.sh`. A bash script with CRLF stops at the first line (`env: 'bash\r': No such file or
directory`, or `$'\r': command not found`). `git ls-files --eol` shows these files as `i/lf` in the index on Linux, so the files are right in the repository; the conversion happens on
checkout.

### 2.3 The tools write text files with Windows line endings [repo for the code, assumed for the effect]

Eight `open(..., "w")` calls write in text mode, so on Windows each `\n` becomes `\r\n`:

* `tools/effects.py`: `write_config` (`effects.config`), `write_if_changed` (all of `generated/*`, including `effects.mk` and `defines.mk`, which `make` reads), `write_chains`
  (`chains.json`), and three that rewrite `effects-resource-usage.json` (`measure`, `calibrate`, `observe`).
* `tools/effects_tui.py`: the NAM model headers (`models/<stem>.h`) and `models.config`.

The generated files are no longer in git (so they are not checked out with CRLF either: only the tool writes them). The two to worry about are `generated/effects.mk` and `models.config`. A makefile with CRLF can leave a `\r` on the end of values ("No rule to make target
`Effect-Modules/foo.cpp\r`"; it depends on the build of `make`), and `update_models.sh` reads `models.config` line by line and compares `name=y`, which `name=y\r` does not match, so
NAM setup would silently drop every model. The JSON files would only give noisy diffs.

### 2.4 The full-screen tool and `make` want different terminals [repo for the code, assumed for the behaviour]

* `tools/effects_tui.py` does `import curses` at the top. It is only imported for the interactive tool (`cmd_tui`), so `list`, `enable`, `disable`, `preset`, `generate`, `report`,
  `info`, `lint`, `measure`, `calibrate`, `matrix` and `observe` never need `curses` [repo].
* Python on Windows has no `curses` module; the usual answer is `pip install windows-curses` [assumed]. With it missing, the tool ends with a Python traceback, not a hint.
* `main()` starts the interactive tool only when `sys.stdin.isatty() and sys.stdout.isatty()`, and otherwise prints the argument help [repo]. Git BASH normally runs in the mintty
  terminal, which is not a Windows console, so `isatty()` is probably false there and plain `python tools/effects.py` would print help instead of starting the tool; `curses` also
  tends not to work under mintty (people use `winpty python ...`) [assumed].
* So the plan is: **Git BASH for `make` and the `ci/*.sh` scripts, a real console (Windows Terminal, PowerShell or cmd) for the interactive tool**. That is the job of
  `pedal_setup.bat`.

### 2.5 `sh` and `cat` are needed [repo for the calls, assumed for the PATH]

* The Makefile runs `$(shell sh Effect-Modules/Nam/update_models.sh --no-calibrate)` and `$(shell cat .gcc_path)`.
* The tool runs `sh update_models.sh` when NAM setup converts models (`effects_tui.py`).
* In Git BASH both are on PATH. From cmd or PowerShell, Git for Windows normally puts only its `cmd` directory on PATH, not `usr\bin` where `sh.exe` is, so NAM setup in an interactive
  tool started from a plain console would say "update_models.sh could not be run". The `.bat` can put Git's `usr\bin` on PATH for its own session.

### 2.6 What the documentation lacks [repo]

* A Windows prerequisites list, in order (section 1 above), and which terminal to use for what (2.4).
* How to use a newer compiler than the one the installer ships: the Makefile reads `.gcc_path` (a file with the path of the toolchain's `bin` directory) before libDaisy's Makefile is
  included. Nothing documents it for users, and the Windows form of the path (forward slashes, no spaces) is not stated. `docs/SETUP.md` says gcc 15.3 should work; **it is not known which
  gcc the Daisy Toolchain Installer ships**, so whether a Windows user needs `.gcc_path` at all is open.
* DFU flashing on Windows: Zadig, `dfu-util` on PATH (the Makefile's `program-dfu` calls `dfu-util -w ...`), and that `make program-dfu` ends with `Error 74` that can be ignored while the
  LED blinks [Daisy docs].
* A troubleshooting list: `\r` errors (2.2, 2.3), `python3` not found (2.1), the tool printing help (2.4), `sh` not found (2.5).

### 2.7 Existing mistakes that also hit Windows readers [repo]

* `docs/SETUP.md` step 2, the manual build of the libraries, says `cd libDaisy` (and `DaisySP`, `CloudSeed`); they are in `dependencies/`. It also has the typo "directiory".
* `docs/SETUP.md` step 3 tells you to uncomment `#define VARIANT_...` in `guitar_pedal.cpp`; the Makefile also takes `make VARIANT=1590B` (and `AGENTS.md` says so), which is easier.
* `docs/GENERATE_FIRMWARE.md` links to `../README.md#4-build-and-deploy-the-code`, which does not exist; the build steps are in `docs/SETUP.md` step 4.
* `.vscode/tasks.json`: `build_cloudseed` runs in `dependencies/Cloudseed`; the directory is `CloudSeed` (works on Windows and macOS, not on Linux). The tasks run `make clean; make`, which
  assumes a shell where `;` chains commands: Daisy's tutorial sets Git BASH as the VS Code terminal for this.
* `tests/host/*.sh` and `ci/format.sh` are bash and need g++ or clang-format; they are not needed to build the firmware, and nothing says so. A Windows user who wants the host tests needs
  WSL or MSYS2.
* Not checked: Windows' 260-character path limit (`git config core.longpaths true`); the deepest paths under `dependencies/` were not measured.

## 3. How sure each finding is

| Finding | Evidence |
|---|---|
| 2.1 `python3` hard-coded | the code; `python` on Windows from Daisy's page; no `python3.exe` assumed |
| 2.2 CRLF checkout of scripts | `.gitattributes` and the default `autocrlf` of Git for Windows; the failure itself assumed |
| 2.3 text-mode writes | the code; the effect on make and `update_models.sh` assumed |
| 2.4 TUI vs Git BASH | the code (lazy `curses`, the `isatty` check); mintty and `windows-curses` assumed |
| 2.5 `sh` on PATH | the code; what Git for Windows puts on PATH assumed |
| 2.6, 2.7 documentation | read in the repository |

## 4. The work to do (in this order), once there is a Windows computer to test on

1. **Small code changes** (each also fine on Linux):
   * Makefile: `PYTHON ?= $(if $(shell command -v python3 2>/dev/null),python3,python)` or similar, used by the `generate` rule; `pedal_setup` tries `python3` and then `python`.
   * `newline="\n"` in the eight `open(..., "w")` calls of 2.3.
   * `.gitattributes`: `eol=lf` for `*.sh`, `pedal_setup`, `Makefile`, `*.mk`, `*.lds`; `eol=crlf` for `*.bat`.
   * A message instead of a traceback when `import curses` fails, naming `pip install windows-curses` and the other terminals.
2. **`pedal_setup.bat`**, next to `pedal_setup`: `cd /d "%~dp0"`; find `py -3`, `python` or `python3` (skip the Microsoft Store stub); find Git for Windows' `sh.exe`
   (`where git`, then `..\usr\bin`) and put it on PATH for the session; check that `curses` imports, else say how to install it; run `tools\effects.py %*`; say clearly if Python is missing.
3. **Documentation**: a "Building on Windows" section in `docs/SETUP.md` (prerequisites in order; which terminal for what; `./pedal_setup` against `pedal_setup.bat`; `.gcc_path` with a
   Windows example; Zadig and DFU; troubleshooting from 2.6), pointers to it from `README.md` and `docs/GENERATE_FIRMWARE.md`, and the corrections of 2.7.

## 5. What to try on the Windows computer (and what to write down)

In this order; note for each step what happened, with the exact error text.

1. Fresh clone with Git for Windows' defaults (`core.autocrlf=true`). `git submodule update --init --recursive`. Does `file ci/build_libs.sh` or `head -c 40 pedal_setup | od -c` show `\r`? (2.2)
2. In Git BASH: `python3 --version`, `python --version`, `py --version`. Which exist? Is the first `python` the Microsoft Store stub? (2.1)
3. In Git BASH: `./ci/build_libs.sh` (libDaisy, DaisySP, CloudSeed). Does it run? Which `arm-none-eabi-gcc --version` and `make --version` are on PATH, and where from (the Daisy installer or another)?
4. In Git BASH: `make -j8`. Does it stop at the regeneration rule (`python3: command not found`)? After `make PYTHON=python`-style workarounds, does it get through? Does a `generated/effects.mk`
   written by Windows Python (CRLF) work with this `make`? (2.3)
5. In Git BASH: `python tools/effects.py` with no arguments: does it print help (mintty, 2.4)? Does `winpty python tools/effects.py` start the tool?
6. In Windows Terminal, PowerShell and cmd: `py -3 -m pip install windows-curses`, then `py -3 tools\effects.py`. Does the tool draw correctly (colours, arrows, Esc, resizing)? Does NAM setup work
   (needs `sh`; 2.5), and does `models.config` come out with CRLF and break `update_models.sh`? (2.3)
7. `make program-dfu` after Zadig: does `dfu-util` run, and does the `Error 74` appear as Daisy's tutorial says?
8. Which gcc did the Daisy installer put on PATH, and does the project build with it, or is `.gcc_path` needed (2.6)?

## 6. Sources

* Electro-Smith, C++ development environment tutorial: <https://docs.daisy.audio/tutorials/cpp-dev-env/> (Git BASH as the terminal, the `mkdir build` failure, the Zadig reminder, the `task build_all` and
  `task build_and_program_dfu` tasks, `Error 74`).
* Electro-Smith, Windows toolchain page: <https://docs.daisy.audio/tutorials/toolchain-windows/> (Git for Windows and Git BASH, `DaisyToolchain-1.1.0-win64.exe`, Python not from the Microsoft Store,
  `python` and not `py`). It does not list the tools the installer contains; the page it refers to for that ("Understanding the Toolchain") was not read.
* The pages that the two above link to for Zadig and for the toolchain contents were not read.
