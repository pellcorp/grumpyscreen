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

    if [ "$TARGET" = "mips" ] || [ "$TARGET" = "rpi" ]; then
      MISC_ARGS+=" CROSS_COMPILE=$CROSS_COMPILE"

      if [ "$GUPPY_SMALL_SCREEN" = "true" ]; then
          MISC_ARGS+=" GUPPY_SMALL_SCREEN=true GUPPY_CALIBRATE=true"
      elif [ "$TARGET" = "rpi" ]; then
          MISC_ARGS+=" GUPPY_CALIBRATE=true"
      fi
    else
      MISC_ARGS+=" GUPPY_WAYLAND=true"

      if [ "$GUPPY_SMALL_SCREEN" = "true" ]; then
        MISC_ARGS+=" GUPPY_SMALL_SCREEN=true"
      fi
    fi

    if [ "$COSMOS" = "true" ]; then
      MISC_ARGS+=" COSMOS=true"
    fi

    echo "Args: $MISC_ARGS"
    docker run --name=grumpydev -ti --rm --entrypoint /bin/bash -v $PWD:$PWD pellcorp/grumpydev -c "cd $PWD && $MISC_ARGS GUPPYSCREEN_VERSION=${GIT_REVISION} GUPPYSCREEN_BRANCH=$GIT_BRANCH make $makefile_arg $@"
}

TARGET=
GUPPY_SMALL_SCREEN=false
COSMOS=false
SETUP=false
PI_USERNAME=pi
PASSWORD=Creality2023

while true; do
    if [ "$1" = "--setup" ]; then
        shift
        SETUP=true
        TARGET=$1
        if [ "$TARGET" != "mips" ] && [ "$TARGET" != "rpi" ] && [ "$TARGET" != "wayland" ]; then
          echo "ERROR: mips or rpi or wayland target must be specified"
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
    elif [ "$1" = "--password" ] && [ -n "$2" ]; then
        export PASSWORD=$2
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
  if [ "$TARGET" = "wayland" ]; then
    echo "wayland" > $CURRENT_DIR/.target.cfg
  elif [ "$TARGET" = "rpi" ]; then
    echo "rpi" > $CURRENT_DIR/.target.cfg
    echo "username=$PI_USERNAME" >> $CURRENT_DIR/.target.cfg
  else
    echo "mips" > $CURRENT_DIR/.target.cfg
    echo "password=$PASSWORD" >> $CURRENT_DIR/.target.cfg
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
  if [ $(cat $CURRENT_DIR/.target.cfg | grep "password=" | wc -l) -gt 0 ]; then
    export PASSWORD=$(cat $CURRENT_DIR/.target.cfg | grep "password=" | awk -F '=' '{print $2}')
  fi
fi

if [ "$TARGET" = "rpi" ]; then
  export CROSS_COMPILE=armv8-rpi3-linux-gnueabihf-
elif [ "$TARGET" = "mips" ]; then
  export CROSS_COMPILE=mipsel-buildroot-linux-musl-
fi

if [ "$SETUP" = "true" ]; then
    docker_make clean || exit $?
    docker_make libhvclean || exit $?
    docker_make wpaclean || exit $?
    #docker_make "bootstrap" clean || exit $?

    docker_make libhv.a || exit $?
    docker_make wpaclient || exit $?
fi

docker_make $1 || exit $?
#docker_make "bootstrap" $1 || exit $?

cp $CURRENT_DIR/grumpyscreen.cfg build/bin/

if [ -n "$PRINTER_IP" ] && [ -f build/bin/grumpyscreen ]; then
  if [ "$TARGET" = "mips" ]; then
    echo "Copying to root@$PRINTER_IP (Password is $PASSWORD) ..."
    sshpass -p $PASSWORD scp build/bin/grumpyscreen root@$PRINTER_IP:
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
    scp build/bin/grumpyscreen $PI_USERNAME@$PRINTER_IP:/tmp/
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
