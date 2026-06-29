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
./build.sh --setup mips [--small] --printer IP_ADDRESS_OF_PRINTER
```

This will directly deploy it to your printer or dev box!

### Build for RPI

```
./build.sh --setup rpi [--small] --printer IP_ADDRESS_OF_PRINTER
```

This will directly deploy it to your rpi based printer or dev box!

### Build Locally (Wayland)

Mostly this is about getting good screenshots for the moment but you can build grumpyscreen locally for wayland

```
./build.sh --setup wayland [--small]
build/bin/grumpyscreen
```

#### Local Virtual Klipper

Based on the https://github.com/mainsail-crew/virtual-klipper-printer the `virtual-klipper-env` docker fires up
a very basic klipper, moonraker and fluidd environment.

```
docker build . --tag virtual-klipper-env
docker run --rm -it -p 8080:80 -p 127.0.0.1:7125:7125 virtual-klipper-env
```

You can access fluidd via http://localhost:8080

Some may ask, why not just use the virtual-klipper-printer directly, to be honest I found it overly complicated to
setup and its not very well documented.  I need a basic printer setup to do basic testing for GrumpyScreen so this
works for me.