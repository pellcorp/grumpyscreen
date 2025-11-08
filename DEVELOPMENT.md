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

Clone the guppyscreen repo (and submodules) and apply a couple of patches locally.

1. `git clone --recursive https://github.com/pellcorp/grumpyscreen grumpyscreen && cd grumpyscreen`

### Build for Creality OS (K1, K1M, Ender 3 V3 KE, etc) 

1. `./build.sh --setup mips [--small]`
2. `./build.sh --printer IP_ADDRESS_OF_PRINTER`

This will directly deploy it to your printer or dev box!

### Build for RPI

1. `./build.sh --setup rpi [--small]`
2. `./build.sh --printer IP_ADDRESS_OF_PRINTER`

This will directly deploy it to your rpi based printer or dev box!
