# Generating the firmware

Which effects go into the firmware, is chosen with the effect selection tool **`./pedal_setup`**. The result is stored in `effects.config`, and the tool generates the files the build needs from it. This document describes the tool, the limits a selection has to stay within, and how to combine effects into chains.

The basic steps:

1. Run `./pedal_setup` and select the effects (arrows to move, space to select, `s` to save, `h` for help), or start from a preset (`./pedal_setup preset <name>`).
2. Build the firmware. `make` regenerates the generated files by itself when `effects.config` or `effects.json` has changed.
3. Flash the firmware. Enabling, disabling or reordering effects changes the layout of the saved settings, which are reset on the next boot.

In more detail, se below.

## The effect selection tool

Running `./pedal_setup` without arguments brings up a full-screen tool in the terminal where you can select effects and configure which NAM modules to include (if NAM is one of the configured effects).

This is the effect selection menu:
![The effect selection tool](effects_config.png)

The selection is kept in **`effects.config`**. The tool also generates what depends on the choice:
`generated/effects.mk` (the sources, and the sources and defines of what the chosen effects need, which the `Makefile` includes) and the effect list that `loaded_effects.h` includes.
`make` regenerates them by itself when `effects.config` changes, so after editing the file (or running one of these) just build. 

`./pedal_setup` also has a number of command line arguments for instance to load predefined effect configurations.

The order of the enabled effects (the order in `effects.json`) also becomes the order of effects in the pedal.

## NAM models

The name effect support NAM A2 Lite models. To include a model, place the `.nam` or `.zip` file in `Effect-Modules/Nam/models_nam/` and run the NAM configuration step in ./pedal_setup`.

NAM setup lists all found models and allows to select which should be included and what name to give it. The display is small so usually the name has to be edited down.

When saving (`s`) the NAM setup you will be asked if the models should be volume normalized, this is an optional step that tries to scale the models to have approximately the same volume, it requires access to a native g++ compiler and will probably not work automatically under Windows.

There is also a script `Effect-Modules/Nam/update_models.sh --calibrate` that performs the same procedure outside `pedal_setup`.

## GPLv3 effects

Some effects are licensed under GPLv3 and require opt-in as these effects are not allowed to be distributed in binary form without the sources.

## Combining Effects (EffectChain)

There is now support in`./pedal_setup` for creating effect chains. Press `c` in the Configure effects to begin the process. 

A chain has a **Switches** parameter in its menu, it allows to use both the left and right switch to turn on/off effects in the chain.  

The options are named left to right, as the footswitches are on the pedal: 3+1 is slot 3 on the left footswitch and slot 1 on the right one.

### Build and Deploy firmware

#### Flash Boot loader

By default this project is configured to use the custom Boot Loader. To get up and running you'll need to flash the boot loader to the Daisy Seed. This only needs to be done once.

1. Build the software with `make -j4`, confirm that `build/guitarpedal.bin` exists
1. Put your Daisy Seed into DFU mode.
1. From the terminal, in the GuitarPedal folder run "make program-boot"

#### Flash firmware

This step is redone every time you change the firmware configuration or change the source code.

1. Build the software with `make -j4`, confirm that `build/guitarpedal.bin` exists
1. Press the Reset button. The led will temporary blink for about 3 second (this is the bootloader waiting for a programming instruction).
1. While the LED is blinking run "make program-dfu"
1. That's it!

If you run into trouble with the bootloader. Electro-Smith has better documentation on how to get it working here: https://github.com/daisyaudio/libDaisy/blob/master/doc/md/_a7_Getting-Started-Daisy-Bootloader.md

##### Flash to built in flash memory

If you want to use built in flash memory only, you _can_, but it severely limits which effects you can use and how many you can have installed at once. I'd recommend choosing just 1 or 2 effects (see [GENERATE_FIRMWARE.md](GENERATE_FIRMWARE.md)). Then do the following to get running on internal flash:

1. Remove the "APP_TYPE = BOOT_SRAM" line from the Make File:
2. Put your Daisy Seed into DFU mode.
3. make build-and-program-dfu
