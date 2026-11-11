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
    apt-get install -y --no-install-recommends sudo pkg-config libsdl2-dev && \
    apt-get clean all && \
    apt-get -y autoremove

# The Android app (./build.sh android): a JDK for Gradle, and the SDK packages
# android/app/build.gradle.kts pins, so nothing is fetched into the SDK at
# build time. Gradle itself comes from the project's wrapper.
ENV ANDROID_HOME=/opt/android-sdk
RUN DEBIAN_FRONTEND=noninteractive apt-get update && \
    apt-get install -y --no-install-recommends openjdk-17-jdk-headless unzip && \
    apt-get clean all && \
    apt-get -y autoremove && \
    wget https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip -O /tmp/cmdline-tools.zip && \
    echo "2d2d50857e4eb553af5a6dc3ad507a17adf43d115264b1afc116f95c92e5e258  /tmp/cmdline-tools.zip" | sha256sum -c - && \
    mkdir -p $ANDROID_HOME/cmdline-tools && \
    unzip -q /tmp/cmdline-tools.zip -d $ANDROID_HOME/cmdline-tools && \
    mv $ANDROID_HOME/cmdline-tools/cmdline-tools $ANDROID_HOME/cmdline-tools/latest && \
    rm /tmp/cmdline-tools.zip && \
    (yes | $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager --licenses > /dev/null) && \
    $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager --install \
        "platforms;android-35" "build-tools;35.0.0" "ndk;27.2.12479018" "cmake;3.22.1" > /dev/null && \
    chmod -R a+rX $ANDROID_HOME

RUN adduser --disabled-password --gecos "" developer && \
  usermod -a -G sudo developer && \
  echo "%sudo ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/nopasswd

ENV PATH=/toolchains/mips32el--musl--stable-2024.02-1/bin:/toolchains/x-tools/armv8-rpi3-linux-gnueabihf/bin/:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

USER developer
WORKDIR /home/developer

CMD ["/bin/bash"]
