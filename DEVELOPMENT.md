# Development

This repository contains the Guppy Screen source code and all its external dependencies.

## Build Environment

### Install Docker

You can follow the instructions to get docker on Ubuntu:
https://docs.docker.com/engine/install/ubuntu/#install-using-the-repository

1. `sudo apt-get update && sudo apt-get install ca-certificates curl gnupg`
2. `sudo install -m 0755 -d /etc/apt/keyrings`
3. `curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg`
4. `sudo chmod a+r /etc/apt/keyrings/docker.gpg`
3. `echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null`
4. `sudo apt-get update`
5. `sudo apt-get install docker-ce docker-ce-cli containerd.io docker-buildx-plugin`

## Building

Clone the grumpyscreen repo (and submodules) and apply a couple of patches locally.

1. `git clone --recursive https://github.com/pellcorp/grumpyscreen grumpyscreen && cd grumpyscreen`

### Build for Creality OS (K1, K1M, Ender 3 V3 KE, etc) 

```
./build.sh mips [--small]
./build.sh mips [--small] --printer IP_ADDRESS_OF_PRINTER --password PRINTER_PASSWORD
```

The second form directly deploys it to your printer or dev box.

### Build for RPI

```
./build.sh rpi [--small]
./build.sh rpi [--small] --printer IP_ADDRESS_OF_PRINTER [--username USERNAME]
```

The second form directly deploys it to your rpi based printer or dev box.

### Build Locally (SDL2)

Build grumpyscreen for your desktop with SDL2:

```
./build.sh sdl [--small]
```

Add `--setup` before the target when you want to clean that target before building, for example `./build.sh --setup mips-small`.

## Desktop Simulator

`sim.sh` runs the SDL build against a simulated printer: a fake Moonraker and Klipper that print real gcode files, an AFC or Happy Hare filament changer, and a fake wpa_supplicant for the wifi panel. It needs Python 3 and nothing else.

```
./build.sh sdl --small
./sim.sh
SIM_BACKEND=hh SIM_LANES=8 ./sim.sh
```

Everything happens in `/tmp/grumpy-sim-7125`: deleting files, factory reset, shutdown and updates never touch your machine. See [docs/simulator.md](docs/simulator.md) for the options, test prints and commands.
