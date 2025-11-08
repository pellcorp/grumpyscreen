FROM ubuntu:22.04

RUN DEBIAN_FRONTEND=noninteractive apt-get update && \
    apt-get install -y --no-install-recommends wget build-essential cmake git zip ca-certificates && update-ca-certificates && \
    apt-get clean all && \
    apt-get -y autoremove

RUN mkdir /toolchains && \
    wget https://toolchains.bootlin.com/downloads/releases/toolchains/mips32el/tarballs/mips32el--musl--stable-2024.02-1.tar.bz2 -O /tmp/mips32el--musl--stable-2024.02-1.tar.bz2 && \
    wget https://github.com/tttapa/docker-arm-cross-toolchain/releases/latest/download/x-tools-armv8-rpi3-linux-gnueabihf-gcc12.tar.xz -O /tmp/x-tools-armv8-rpi3-linux-gnueabihf-gcc12.tar.xz && \
    tar -jxf /tmp/mips32el--musl--stable-2024.02-1.tar.bz2 -C /toolchains && \
    tar -xf /tmp/x-tools-armv8-rpi3-linux-gnueabihf-gcc12.tar.xz -C /toolchains && \
    rm /tmp/mips32el--musl--stable-2024.02-1.tar.bz2 && \
    rm /tmp/x-tools-armv8-rpi3-linux-gnueabihf-gcc12.tar.xz

RUN DEBIAN_FRONTEND=noninteractive apt-get update && \
    apt-get install -y --no-install-recommends sudo && \
    apt-get clean all && \
    apt-get -y autoremove

RUN adduser --disabled-password --gecos "" developer && \
  usermod -a -G sudo developer && \
  echo "%sudo ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/nopasswd

ENV PATH=/toolchains/mips32el--musl--stable-2024.02-1/bin:/toolchains/x-tools/armv8-rpi3-linux-gnueabihf/bin/:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

USER developer
WORKDIR /home/developer

CMD ["/bin/bash"]
