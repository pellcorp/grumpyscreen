#!/bin/bash

RELEASES_DIR=./releases/guppyscreen
rm -rf $RELEASES_DIR
mkdir -p $RELEASES_DIR

ASSET_NAME=$1
GIT_SHA=$2
GIT_BRANCH=$3

TIMESTAMP=$(date +%s)

"$CROSS_COMPILE"strip ./build/bin/guppyscreen
cp ./build/bin/guppyscreen $RELEASES_DIR/guppyscreen
cp -r ./themes $RELEASES_DIR
cp grumpyscreen.cfg $RELEASES_DIR
echo "GIT_SHA=${GIT_SHA:0:7}" > $RELEASES_DIR/release.info
echo "GIT_BRANCH=$GIT_BRANCH" >> $RELEASES_DIR/release.info
echo "TIMESTAMP=$TIMESTAMP" >> $RELEASES_DIR/release.info

echo "Timestamp was $TIMESTAMP"

if [ "$ASSET_NAME" = "guppyscreen-smallscreen" ]; then
  sed -i 's/"display_rotate": 3/"display_rotate": 2/g' $RELEASES_DIR/grumpyscreen.cfg
elif [ "$ASSET_NAME" = "guppyscreen-rpi" ]; then
  sed -i 's/"display_rotate": 3/"display_rotate": 0/g' $RELEASES_DIR/grumpyscreen.cfg
  # rpi does not have factory reset
  sed -i 's:/etc/init.d/S58factoryreset::g' $RELEASES_DIR/grumpyscreen.cfg
  # rpi does not have switch to stock
  sed -i 's:/usr/data/pellcorp/k1/switch-to-stock.sh::g' $RELEASES_DIR/grumpyscreen.cfg
  # for now no support command for rpi either
  sed -i 's:/usr/data/pellcorp/tools/support.sh::g' $RELEASES_DIR/grumpyscreen.cfg
fi
tar czf $ASSET_NAME.tar.gz -C releases .
