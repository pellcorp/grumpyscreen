#!/bin/bash

CURRENT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd -P)"

PRINTER_IP=
SETUP=false
TARGET=mips

GIT_REVISION=$(git rev-parse HEAD)
GIT_BRANCH=$(git rev-parse --abbrev-ref HEAD)

function docker_make() {
    target_arg=""
    if [ "$GUPPY_SMALL_SCREEN" = "true" ]; then
        target_arg="GUPPY_SMALL_SCREEN=true GUPPY_CALIBRATE=true"
    elif [ "$TARGET" = "rpi" ]; then
        target_arg="GUPPY_CALIBRATE=true"
    fi

    # this is just an easy way for me to test shit
    if [ "$COSMOS" = "true" ]; then
      MISC_ARGS="UPDATE_CMD=cosmos_update_cmd"
      MISC_ARGS+=" UPDATE_TEXT='Update\nCOSMOS'"
      MISC_ARGS+=" UPDATE_PROMPT='Are you sure you want to update COSMOS?\n\nThis will download and update to the latest version of COSMOS!'"
      MISC_ARGS+=" UPDATE_SUCCESS='Your printer will restart shortly!'"
      MISC_ARGS+=" UPDATE_FAILURE='Failed to initiate update COSMOS!'"

      MISC_ARGS+=" SWITCH_TO_STOCK_TEXT='Switch to OC\nPatched'"
      MISC_ARGS+=" SWITCH_TO_STOCK_PROMPT='Are you sure you want to switch to OpenCentauri patched firmware?\n\nThis will take some time, **DO NOT TURN OFF YOUR PRINTER**, just wait for it to reboot.'"
      MISC_ARGS+=" SWITCH_TO_STOCK_FAILURE='Failed to initiate switch to OC Patched!'"
      MISC_ARGS+=" SWITCH_TO_STOCK_SUCCESS='Your printer will restart shortly!'"

      MISC_ARGS+=" FACTORY_RESET_TEXT='Factory\nReset'"
      MISC_ARGS+=" FACTORY_RESET_PROMPT='Are you sure you want factory reset?\n\nThis will reset all printer setting but it will stay using COSMOS, it will not switch back to stock.'"
      MISC_ARGS+=" FACTORY_RESET_FAILURE='Failed to factory reset!'"
      MISC_ARGS+=" FACTORY_RESET_SUCCESS='Your printer will restart shortly!'"

      echo "MISC_ARGS: $MISC_ARGS"
    fi

    echo "Target Arguments: $target_arg"

    # make sure we are building all the src changes
    #[ -d $CURRENT_DIR/build/obj/src ] && rm $CURRENT_DIR/build/obj/src/*

    docker run --name=grumpydev -ti --rm -v $PWD:$PWD pellcorp/guppydev /bin/bash -c "cd $PWD && $MISC_ARGS GUPPYSCREEN_VERSION=${GIT_REVISION} GUPPYSCREEN_BRANCH=$GIT_BRANCH $target_arg CROSS_COMPILE=$CROSS_COMPILE make $@"
}

TARGET=
GUPPY_SMALL_SCREEN=false
COSMOS=false
SETUP=false
PI_USERNAME=pi

while true; do
    if [ "$1" = "--setup" ]; then
        shift
        SETUP=true
        TARGET=$1
        if [ "$TARGET" != "mips" ] && [ "$TARGET" != "rpi" ]; then
          echo "ERROR: mips or rpi target must be specified"
          exit 1
        fi
        shift
    elif [ "$1" = "--small" ]; then
        export GUPPY_SMALL_SCREEN=true
        shift
    elif [ "$1" = "--cosmos" ]; then
        export COSMOS=true
        shift
    elif [ "$1" = "--username" ] && [ -n "$2" ]; then
        export PI_USERNAME=$2
        shift
        shift
    elif [ "$1" = "--printer" ]; then
        shift
        PRINTER_IP=$1
        shift
    else
        break
    fi
done

if [ "$SETUP" = "true" ]; then
  if [ "$TARGET" = "rpi" ]; then
    echo "rpi" > $CURRENT_DIR/.target.cfg
    echo "username=$PI_USERNAME" >> $CURRENT_DIR/.target.cfg
  else
    echo "mips" > $CURRENT_DIR/.target.cfg
  fi

  if [ "$GUPPY_SMALL_SCREEN" = "true" ]; then
    echo "small=true" >> $CURRENT_DIR/.target.cfg
  fi

  if [ "$COSMOS" = "true" ]; then
    echo "cosmos=true" >> $CURRENT_DIR/.target.cfg
  fi
fi

if [ -f $CURRENT_DIR/.target.cfg ]; then
  TARGET=$(cat $CURRENT_DIR/.target.cfg | head -1)
  if [ $(cat $CURRENT_DIR/.target.cfg | grep "small=true" | wc -l) -gt 0 ]; then
    export GUPPY_SMALL_SCREEN=true
  fi
  if [ $(cat $CURRENT_DIR/.target.cfg | grep "cosmos=true" | wc -l) -gt 0 ]; then
    export COSMOS=true
  fi
  if [ $(cat $CURRENT_DIR/.target.cfg | grep "username=" | wc -l) -gt 0 ]; then
    export PI_USERNAME=$(cat $CURRENT_DIR/.target.cfg | grep "username=" | awk -F '=' '{print $2}')
  fi
fi

if [ "$TARGET" = "rpi" ]; then
  export CROSS_COMPILE=armv8-rpi3-linux-gnueabihf-
else
  export CROSS_COMPILE=mipsel-buildroot-linux-musl-
fi

if [ "$SETUP" = "true" ]; then
    docker_make libhvclean || exit $?
    docker_make wpaclean || exit $?
    docker_make clean || exit $?

    docker_make libhv.a || exit $?
    docker_make wpaclient || exit $?
else
    docker_make $1 || exit $?

    if [ -n "$PRINTER_IP" ] && [ -f build/bin/guppyscreen ]; then
        if [ "$TARGET" = "mips" ]; then
          password=creality_2023
          if [ "$GUPPY_SMALL_SCREEN" = "true" ]; then
            password=Creality2023
          fi
          sshpass -p $password scp build/bin/guppyscreen root@$PRINTER_IP:
          sshpass -p $password ssh root@$PRINTER_IP "mv /root/guppyscreen /usr/data/grumpyscreen/grumpyscreen"

          cp grumpyscreen.cfg /tmp
          if [ "$GUPPY_SMALL_SCREEN" = "true" ]; then
            sed -i 's/display_rotate: 3/display_rotate: 1/g' /tmp/grumpyscreen.cfg
          fi

          if [ "$COSMOS" = "true" ]; then
            if ! grep -q 'cosmos_update_cmd' /tmp/grumpyscreen.cfg; then
                sed -i '/^\[commands\]/a cosmos_update_cmd: /bin/true' /tmp/grumpyscreen.cfg
            fi
          fi
          sshpass -p $password scp /tmp/grumpyscreen.cfg root@$PRINTER_IP:
          sshpass -p $password ssh root@$PRINTER_IP "mv /root/grumpyscreen.cfg /usr/data/grumpyscreen/grumpyscreen.cfg"
          sshpass -p $password ssh root@$PRINTER_IP "/etc/init.d/S99grumpyscreen restart"
        else # rpi
          echo "Uploading to ${PI_USERNAME}@$PRINTER_IP ..."
          cp grumpyscreen.cfg /tmp
          scp build/bin/guppyscreen $PI_USERNAME@$PRINTER_IP:/tmp/
          sed -i 's/display_rotate: 3/display_rotate: 0/g' /tmp/grumpyscreen.cfg
          sed -i '/S58factoryreset/d' /tmp/grumpyscreen.cfg
          # rpi does not have switch to stock
          sed -i 's:/usr/data/pellcorp/k1/switch-to-stock.sh::g' /tmp/grumpyscreen.cfg
          # for now no support command for rpi either
          sed -i 's:/usr/data/pellcorp/tools/support.sh::g' /tmp/grumpyscreen.cfg
          scp /tmp/grumpyscreen.cfg $PI_USERNAME@$PRINTER_IP:/tmp/
          ssh $PI_USERNAME@$PRINTER_IP "mv /tmp/guppyscreen /home/$PI_USERNAME/grumpyscreen/grumpyscreen"
          ssh $PI_USERNAME@$PRINTER_IP "mv /tmp/grumpyscreen.cfg /home/$PI_USERNAME/grumpyscreen/grumpyscreen.cfg"
          ssh $PI_USERNAME@$PRINTER_IP "sudo systemctl restart grumpyscreen"
        fi
    fi
fi
