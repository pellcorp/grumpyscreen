#!/bin/bash

CURRENT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd -P)"

if [ "$TARGET" = "rpi" ]; then
  export CROSS_COMPILE=armv8-rpi3-linux-gnueabihf-
else
  export CROSS_COMPILE=mipsel-buildroot-linux-musl-
fi

cd $CURRENT_DIR

found=true
if [ ! -f debug/guppyscreen ]; then
  echo "ERROR: Missing guppyscreen"
  found=false
fi

if [ ! -f debug/guppyscreen.debug ]; then
  echo "ERROR: Missing guppyscreen.debug"
  found=false
fi

if [ ! -f debug/guppyscreen.core ]; then
  echo "ERROR: Missing guppyscreen.core"
  found=false
fi

if [ "$found" != "true" ]; then
  exit 1
else
  readelf -wk debug/guppyscreen > /dev/null
  if [ $? -ne 0 ]; then
    exit 1
  fi
fi

docker run -ti -v $PWD:$PWD pellcorp/guppydev /bin/bash -c "cd $PWD && ${CROSS_COMPILE}gdb debug/guppyscreen -c debug/guppyscreen.core"
