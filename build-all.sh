#!/bin/bash

function do_release() {
  RELEASES_DIR=./releases/build/grumpyscreen
  rm -rf $RELEASES_DIR
  mkdir -p $RELEASES_DIR

  ASSET_NAME=$1

  cp ./build/bin/guppyscreen $RELEASES_DIR/grumpyscreen
  cp grumpyscreen.cfg $RELEASES_DIR/

  if [ "$ASSET_NAME" = "grumpyscreen-smallscreen" ]; then
    sed -i 's/display_rotate: 3/display_rotate: 2/g' $RELEASES_DIR/grumpyscreen.cfg
  elif [ "$ASSET_NAME" = "grumpyscreen-rpi" ]; then
    sed -i 's/display_rotate: 3/display_rotate: 0/g' $RELEASES_DIR/grumpyscreen.cfg
    # rpi does not have factory reset
    sed -i 's:/etc/init.d/S58factoryreset reset::g' $RELEASES_DIR/grumpyscreen.cfg
    sed -i 's:/etc/init.d/S99guppyscreen restart:sudo systemctl restart grumpyscreen:g' $RELEASES_DIR/grumpyscreen.cfg
    sed -i 's:/etc/init.d/S55klipper_service restart:sudo systemctl restart klipper:g' $RELEASES_DIR/grumpyscreen.cfg
    # rpi does not have switch to stock
    sed -i 's:/usr/data/pellcorp/k1/switch-to-stock.sh::g' $RELEASES_DIR/grumpyscreen.cfg
  fi
  tar cf - -C releases/build . | pigz -9 > releases/${ASSET_NAME}.tar.gz
}

[ -d ./releases/ ] && rm -rf ./releases

echo "Building grumpyscreen ..."
./build.sh > /dev/null --setup mips || exit $?
./build.sh > /dev/null || exit $?
do_release grumpyscreen

echo "Building grumpyscreen-smallscreen ..."
./build.sh --setup mips --small > /dev/null  || exit $?
./build.sh  > /dev/null  || exit $?
do_release grumpyscreen-smallscreen

echo "Building grumpyscreen-rpi ..."
./build.sh --setup rpi > /dev/null  || exit $?
./build.sh > /dev/null  || exit $?
do_release grumpyscreen-rpi
