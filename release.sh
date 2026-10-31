#!/bin/bash

RELEASES_DIR=./releases/grumpyscreen
rm -rf $RELEASES_DIR
mkdir -p $RELEASES_DIR

ASSET_NAME=$1
BUILD_DIR=${2:-./build}

TIMESTAMP=$(date +%s)

cp "$BUILD_DIR/bin/grumpyscreen" $RELEASES_DIR/
cp grumpyscreen.cfg $RELEASES_DIR/
cp -r languages $RELEASES_DIR/

echo "Timestamp was $TIMESTAMP"

if [ "$ASSET_NAME" = "grumpyscreen-rpi" ]; then
  sed -i 's:/etc/init.d/S99grumpyscreen restart:sudo systemctl restart grumpyscreen:g' $RELEASES_DIR/grumpyscreen.cfg
  sed -i 's:/etc/init.d/S55klipper_service restart:sudo systemctl restart klipper:g' $RELEASES_DIR/grumpyscreen.cfg
  sed -i 's:/usr/data/pellcorp/tools/support.sh:$HOME/pellcorp/tools/support.sh:g' $RELEASES_DIR/grumpyscreen.cfg
  sed -i 's:/sbin/halt:sudo systemctl poweroff:g' $RELEASES_DIR/grumpyscreen.cfg
  # rpi does not have factory reset
  sed -i 's:/etc/init.d/S58factoryreset reset::g' $RELEASES_DIR/grumpyscreen.cfg
  # rpi does not have switch to stock
  sed -i 's:/usr/data/pellcorp/k1/switch-to-stock.sh::g' $RELEASES_DIR/grumpyscreen.cfg
  # rpi does not have a restart wifi tool
  sed -i 's:/usr/data/pellcorp/k1/tools/restart_wifi.sh::g' $RELEASES_DIR/grumpyscreen.cfg
fi
tar czf $ASSET_NAME.tar.gz -C releases .
