# SETUP

This document goes into detail on how to get everything up and running.

## Getting Started

This fork of the bkshepherd pedal currently does not supply any pre-compiled binaries, there are now too many options and effects to provide a meaningful generic default.

Instead it is recommended to setup your local development environment for building your own firmware.

### 1. Setup your Development Environment

The code in this project is supplied with a VS Code project. Detailed instructions on setting up your dev environment can be found here:

https://docs.daisy.audio/tutorials/cpp-dev-env/

Note that previously an older version of the arm-gcc compiler was need, this is no longer the case and the current (at the time of writing) version 15.3 should work fine.

### 2. Setup dependencies

Check to make sure that the following directories have files in them:

1. `Software/GuitarPedal/dependencies/libDaisy`
1. `Software/GuitarPedal/dependencies/DaisySP`
1. `Software/GuitarPedal/dependencies/q/q`
1. `Software/GuitarPedal/dependencies/q/infra`
1. `Software/GuitarPedal/dependencies/gcem`

If they do not have anything in them, you may need to do a `git submodule update --init --recursive`.

Once those directories exist and have files in them, you need to build libDaisy, DaisySP, and CloudSeed. This can be done with the following command:

1. From `Software/GuitarPedal` directiory: `./ci/build_libs.sh`

If this doesn't work, you can do it manually:

1. `cd libDaisy`
1. `make clean && make -j4`
1. `cd ..`
1. `cd DaisySP`
1. `make clean && make -j4`
1. `cd ..`
1. `cd CloudSeed`
1. `make clean && make -j4`

#### Additional information for this step:

- Note - You may not need to do all parts of this step if the project pulled down from GitHub with the dependencies already included as sub-modules. The desktop GUI client always should pull down submodules. If it still doesn't pull down and you have libDaisy and DaisySP installed somehwere else, you can follow the steps below, (note that you will still need to setup cycfi/q and cycfi/infra dependencies separately so the submodule way should be preferred):

  - You'll need to update the paths in the **Makefile**.
  - You'll also need to update the paths in the **c_cpp_properties.json** file in the **.vscode/** folder.
  - You'll also need to update the paths in the **task.json** file in the **.vscode/** folder.

### 3. Configure your specific Target Hardware

You'll need to open the guitar_pedal.cpp file and uncomment a line to configure which hardware you are targetting. If you don't do this, it will build for the 125B variant.

_It should be noted that the 125B version is highly recommended as a lot of functionality depend on the display._

In guitar_pedal.cpp:

```cpp
// Uncomment the version you are trying to use, by default (and if nothing is
// uncommented), the 125B with 2 footswitch variant will be used

// #define VARIANT_125B
// #define VARIANT_1590B
// #define VARIANT_1590B_SMD
// #define VARIANT_TERRARIUM
// #define VARIANT_FUNBOX
```

If you want to target the GuitarPedal125B hardware change the lines to:

```cpp
#define VARIANT_125B
// #define VARIANT_1590B
// #define VARIANT_1590B_SMD
// #define VARIANT_TERRARIUM
// #define VARIANT_FUNBOX
```

If you want to target the GuitarPedal1590B hardware change the lines to:

```cpp
// #define VARIANT_125B
#define VARIANT_1590B
// #define VARIANT_1590B_SMD
// #define VARIANT_TERRARIUM
// #define VARIANT_FUNBOX
```

and so on for the other possible options...

### 4. Build and Deploy a firmware

See [GENERATE_FIRMWARE.md](GENERATE_FIRMWARE.md) for how to generate the firmware configuration and flash it onto your hardware. 

### 5. Connect your Guitar and Amp

Plug your guitar into the Input and connect the Output to your amp.

- Right button.
  Turn effect on/off
- 
### 6. Enjoy!!!
