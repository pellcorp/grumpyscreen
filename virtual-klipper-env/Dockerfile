FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV HOME=/home/developer
ENV KLIPPER_DIR=/home/developer/klipper
ENV MOONRAKER_DIR=/home/developer/moonraker
ENV PRINTER_DATA=/home/developer/printer_data
ENV KLIPPY_ENV=/home/developer/klippy-env
ENV FLUIDD_DIR=/home/developer/fluidd
ENV SIMULAVR_DIR=/home/developer/simulavr

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash git sudo ca-certificates build-essential make gcc g++ cmake \
    python3 python3-dev python3-venv python3-pip \
    libffi-dev libssl-dev libncurses-dev pkg-config swig \
    avr-libc binutils-avr gcc-avr rst2pdf help2man texinfo \
    curl wget unzip iproute2 dbus nginx supervisor \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m -s /bin/bash developer \
    && usermod -aG dialout,tty,sudo developer \
    && echo 'developer ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/developer \
    && chmod 0440 /etc/sudoers.d/developer

RUN git clone --depth=1 https://github.com/pellcorp/klipper-rpi.git ${KLIPPER_DIR}

RUN git clone --depth=1 https://github.com/Arksine/moonraker.git ${MOONRAKER_DIR}

RUN  git clone https://git.savannah.nongnu.org/git/simulavr.git ${SIMULAVR_DIR}

RUN mkdir -p ${FLUIDD_DIR} \
    && wget -O /tmp/fluidd.zip https://github.com/fluidd-core/fluidd/releases/latest/download/fluidd.zip \
    && unzip /tmp/fluidd.zip -d ${FLUIDD_DIR} \
    && rm /tmp/fluidd.zip

RUN chown -R developer:developer ${HOME}

USER developer
WORKDIR ${MOONRAKER_DIR}

RUN ./scripts/install-moonraker.sh -z -x -d ${PRINTER_DATA}

WORKDIR ${KLIPPER_DIR}

COPY --chown=developer:developer simulavr.config ${KLIPPER_DIR}/.config

RUN python3 -m venv ${KLIPPY_ENV} \
    && ${KLIPPY_ENV}/bin/pip install --upgrade pip wheel \
    && ${KLIPPY_ENV}/bin/pip install -r ${KLIPPER_DIR}/scripts/klippy-requirements.txt \
    && make

WORKDIR ${SIMULAVR_DIR}

RUN make python \
    && make build \
    && cp /home/developer/klipper/out/klipper.elf /home/developer/simulavr.elf

COPY --chown=developer:developer linux.config ${KLIPPER_DIR}/.config

WORKDIR ${KLIPPER_DIR}

RUN make clean  \
    && make \
    && cp /home/developer/klipper/out/klipper.elf /home/developer/klipper_mcu.elf

USER root

RUN mkdir -p \
    ${PRINTER_DATA}/config \
    ${PRINTER_DATA}/logs \
    ${PRINTER_DATA}/database \
    ${PRINTER_DATA}/gcodes \
    ${PRINTER_DATA}/certs \
    /run/nginx \
    /var/log/supervisor

COPY --chown=developer:developer printer.cfg ${PRINTER_DATA}/config/printer.cfg
COPY --chown=developer:developer moonraker.conf ${PRINTER_DATA}/config/moonraker.conf
COPY --chown=developer:developer moonraker.asvc ${PRINTER_DATA}/
COPY --chown=root:root nginx.conf /etc/nginx/nginx.conf
COPY --chown=root:root supervisord.conf /etc/supervisor/conf.d/klipper-dev.conf

RUN chown -R developer:developer ${HOME}

WORKDIR ${HOME}

EXPOSE 80 7125

CMD ["/usr/bin/supervisord", "-n", "-c", "/etc/supervisor/conf.d/klipper-dev.conf"]
