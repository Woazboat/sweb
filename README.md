# SWEB[^1]
[![Build Status](https://travis-ci.org/IAIK/sweb.svg?branch=master)](https://travis-ci.org/IAIK/sweb)

SWEB Educational OS

Please have a look at https://www.iaik.tugraz.at/os

## How to use SWEB
See [here](https://www.iaik.tugraz.at/teaching/materials/os/tutorials/prerequisites-installation/) for detailed instructions. The following information is an abbreviated version:

### Prerequisites
To be able to compile and run SWEB, you need to have these packages installed:
- `cmake`
- `qemu` (`qemu-system-x86`)
- `gcc` (9 or newer)
- `g++` (9 or newer)
- `python`

Required for debugging:
- `gdb`
- `cgdb` or`ddd` (optional gdb frontends)

### Setup
Create the build directory and configure cmake by running `./setup_cmake.sh` in the repository directory. This will create the `/tmp/sweb` folder which will be used for all following operations.

### Compilation
Change to the build folder `/tmp/sweb` and run `make -j` to compile SWEB.

### Running SWEB
Run `make qemu` to start your SWEB in the QEMU emulator.

### [Debugging SWEB](https://www.iaik.tugraz.at/teaching/materials/os/tutorials/sweb-kernel-debuggen-mit-cgdb/)
1. Run `make debug` to turn on debug information for following builds, then re-compile SWEB using `make -j`.  
2. Run `make qemugdb` to start QEMU with the integrated gdb server listening on `localhost:1234`.  
3. In a separate terminal run `make runcgdb` to start debugging your SWEB using cgdb.

### Reconfiguring cmake after adding new files
When adding new source files, cmake needs to be reconfigured to consider these new files for compilation. To do so, run `make mrproper` in the build folder. (This will delete and recreate the build folder. Please ensure the folder to be deleted is actually the one you expect.)

### Changing to a different architecture
Switch to a different architecture via the following commands:
- `make x86_64` (default)
- `make x86_32`
- `make x86_32_pae`
- `make arm_icp`
- `make arm_rpi2`
- `make armv8_rpi3`

[^1]: Schon Wieder Ein Betriebssystem
