#!/bin/bash

CURRENT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd -P)"

PRINTER_IP=
SETUP=false
TARGET=mips

GIT_REVISION=$(git rev-parse HEAD)
GIT_BRANCH=$(git rev-parse --abbrev-ref HEAD)

function docker_make() {
    local makefile_type=$1

    makefile_arg=""
    if [ -n "$makefile_type" ] && [ -f "Makefile.${makefile_type}" ]; then
      makefile_arg="-f Makefile.${makefile_type}"
      shift
    fi

    MISC_ARGS=""

    MISC_ARGS+=" BUILD_DIR=$BUILD_DIR"

    if [ "$TARGET_BASE" = "mips" ] || [ "$TARGET_BASE" = "rpi" ]; then
      MISC_ARGS+=" CROSS_COMPILE=$CROSS_COMPILE"

      if [ "$GUPPY_SMALL_SCREEN" = "true" ]; then
          MISC_ARGS+=" GUPPY_SMALL_SCREEN=true GUPPY_CALIBRATE=true"
      elif [ "$TARGET_BASE" = "rpi" ]; then
          MISC_ARGS+=" GUPPY_CALIBRATE=true"
      fi
    else
      MISC_ARGS+=" GUPPY_SDL=true"

      if [ "$GUPPY_SMALL_SCREEN" = "true" ]; then
        MISC_ARGS+=" GUPPY_SMALL_SCREEN=true"
      fi
    fi

    if [ "$COSMOS" = "true" ]; then
      MISC_ARGS+=" COSMOS=true"
    fi

    echo "Args: $MISC_ARGS"
    docker run --name=grumpydev --rm --entrypoint /bin/bash -v $PWD:$PWD pellcorp/grumpydev -c "cd $PWD && $MISC_ARGS GUPPYSCREEN_VERSION=${GIT_REVISION} GUPPYSCREEN_BRANCH=$GIT_BRANCH make -j $makefile_arg $@"
}

TARGET=
GUPPY_SMALL_SCREEN=false
COSMOS=false
SETUP=false
PI_USERNAME=pi
PASSWORD=
MAKE_ARGS=()

function target_base() {
    local target="${1%-small}"
    if [ "$target" = "wayland" ]; then
      echo "sdl"
    else
      echo "$target"
    fi
}

function target_is_small() {
    [ "${1%-small}" != "$1" ]
}

function normalize_build_target() {
    local target=$1
    if [ "${target%-small}" = "wayland" ]; then
      target="sdl${target#wayland}"
    fi
    if [ "$GUPPY_SMALL_SCREEN" = "true" ] && [ "${target%-small}" = "$target" ]; then
      echo "${target}-small"
    else
      echo "$target"
    fi
}

function is_build_target() {
    local base
    base=$(target_base "$1")
    [ "$base" = "mips" ] || [ "$base" = "rpi" ] || [ "$base" = "sdl" ]
}

while [ $# -gt 0 ]; do
    if [ "$1" = "--setup" ]; then
        shift
        if [ -z "$1" ]; then
          echo "ERROR: --setup requires a target"
          exit 1
        fi
        SETUP=true
        TARGET=$1
        TARGET_BASE=$(target_base "$TARGET")
        if [ "$TARGET_BASE" != "mips" ] && [ "$TARGET_BASE" != "rpi" ] && [ "$TARGET_BASE" != "sdl" ]; then
          echo "ERROR: mips, mips-small, rpi, rpi-small, sdl or sdl-small target must be specified"
          exit 1
        fi
        if target_is_small "$TARGET"; then
          export GUPPY_SMALL_SCREEN=true
        fi
        shift
    elif [ "$1" = "--small" ]; then
        export GUPPY_SMALL_SCREEN=true
        shift
    elif [ "$1" = "--cosmos" ]; then
        export COSMOS=true
        shift
    elif [ "$1" = "--username" ]; then
        if [ -z "$2" ]; then
          echo "ERROR: --username requires a value"
          exit 1
        fi
        export PI_USERNAME=$2
        shift
        shift
    elif [ "$1" = "--password" ]; then
        if [ -z "$2" ]; then
          echo "ERROR: --password requires a value"
          exit 1
        fi
        export PASSWORD=$2
        shift
        shift
    elif [ "$1" = "--printer" ]; then
        if [ -z "$2" ]; then
          echo "ERROR: --printer requires a value"
          exit 1
        fi
        shift
        PRINTER_IP=$1
        shift
    elif is_build_target "$1"; then
        TARGET=$1
        if target_is_small "$TARGET"; then
          export GUPPY_SMALL_SCREEN=true
        fi
        shift
    else
        MAKE_ARGS+=("$1")
        shift
    fi
done

if [ -n "$TARGET" ] && ! is_build_target "$TARGET"; then
  echo "ERROR: mips, mips-small, rpi, rpi-small, sdl or sdl-small target must be specified"
  exit 1
fi

if [ -z "$TARGET" ]; then
  TARGET=sdl
fi

TARGET=$(normalize_build_target "$TARGET")
TARGET_BASE=$(target_base "$TARGET")
BUILD_DIR=build/$TARGET

if [ "$TARGET_BASE" = "rpi" ]; then
  export CROSS_COMPILE=armv8-rpi3-linux-gnueabihf-
elif [ "$TARGET_BASE" = "mips" ]; then
  export CROSS_COMPILE=mipsel-buildroot-linux-musl-
fi

if [ -n "$PRINTER_IP" ] && [ "$TARGET_BASE" = "mips" ] && [ -z "$PASSWORD" ]; then
  echo "ERROR: --password is required when deploying to a mips printer"
  exit 1
fi

if [ "$SETUP" = "true" ]; then
    docker_make clean || exit $?
    #docker_make "bootstrap" clean || exit $?

    docker_make libhv.a || exit $?
    docker_make wpaclient || exit $?
fi

docker_make "${MAKE_ARGS[@]}" || exit $?
#docker_make "bootstrap" $1 || exit $?

cp $CURRENT_DIR/grumpyscreen.cfg "$BUILD_DIR/bin/"

if [ -n "$PRINTER_IP" ] && [ -f "$BUILD_DIR/bin/grumpyscreen" ] && { [ "$TARGET_BASE" = "mips" ] || [ "$TARGET_BASE" = "rpi" ]; }; then
  if [ "$TARGET_BASE" = "mips" ]; then
    echo "Copying to root@$PRINTER_IP (Password is $PASSWORD) ..."
    sshpass -p $PASSWORD scp "$BUILD_DIR/bin/grumpyscreen" root@$PRINTER_IP:
    sshpass -p $PASSWORD ssh root@$PRINTER_IP "mv /root/grumpyscreen /usr/data/grumpyscreen/grumpyscreen"

    cp grumpyscreen.cfg /tmp
    # this assumes a Ender 3 V3 KE Nebula pad configuration
    if [ "$GUPPY_SMALL_SCREEN" = "true" ]; then
      sed -i 's/display_rotate: 3/display_rotate: 0/g' /tmp/grumpyscreen.cfg
    fi

    if [ "$COSMOS" = "true" ]; then
      if ! grep -q 'cosmos_update_cmd' /tmp/grumpyscreen.cfg; then
          sed -i '/^\[commands\]/a cosmos_update_cmd: /bin/true' /tmp/grumpyscreen.cfg
      fi
    fi

    sshpass -p $PASSWORD scp /tmp/grumpyscreen.cfg root@$PRINTER_IP:
    sshpass -p $PASSWORD ssh root@$PRINTER_IP "mv /root/grumpyscreen.cfg /usr/data/grumpyscreen/grumpyscreen.cfg"
    sshpass -p $PASSWORD ssh root@$PRINTER_IP "/etc/init.d/S99grumpyscreen restart"
  else # rpi - assumes passwordless ssh i guess oops
    echo "Uploading to ${PI_USERNAME}@$PRINTER_IP ..."
    cp grumpyscreen.cfg /tmp
    scp "$BUILD_DIR/bin/grumpyscreen" $PI_USERNAME@$PRINTER_IP:/tmp/
    sed -i 's/display_rotate: 3/display_rotate: 0/g' /tmp/grumpyscreen.cfg
    sed -i 's:/etc/init.d/S99grumpyscreen restart:sudo systemctl restart grumpyscreen:g' /tmp/grumpyscreen.cfg
    sed -i 's:/etc/init.d/S55klipper_service restart:sudo systemctl restart klipper:g' /tmp/grumpyscreen.cfg
    sed -i 's:/usr/data/pellcorp/tools/support.sh:/home/$PI_USERNAME/pellcorp/tools/support.sh:g' /tmp/grumpyscreen.cfg
    sed -i 's:/sbin/halt:sudo systemctl poweroff:g' /tmp/grumpyscreen.cfg

    # rpi does not have factory reset
    sed -i 's:/etc/init.d/S58factoryreset reset::g' /tmp/grumpyscreen.cfg
    # rpi does not have switch to stock
    sed -i 's:/usr/data/pellcorp/k1/switch-to-stock.sh::g' /tmp/grumpyscreen.cfg

    scp /tmp/grumpyscreen.cfg $PI_USERNAME@$PRINTER_IP:/tmp/
    ssh $PI_USERNAME@$PRINTER_IP "mv /tmp/grumpyscreen /home/$PI_USERNAME/grumpyscreen/grumpyscreen"
    ssh $PI_USERNAME@$PRINTER_IP "mv /tmp/grumpyscreen.cfg /home/$PI_USERNAME/grumpyscreen/grumpyscreen.cfg"
    ssh $PI_USERNAME@$PRINTER_IP "sudo systemctl restart grumpyscreen"
  fi
fi
