FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV HOME=/home/developer
ENV KLIPPER_DIR=/home/developer/klipper
ENV MOONRAKER_DIR=/home/developer/moonraker
ENV PRINTER_DATA=/home/developer/printer_data
ENV KLIPPY_ENV=/home/developer/klippy-env
ENV FLUIDD_DIR=/home/developer/fluidd

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash git sudo ca-certificates build-essential make gcc \
    python3 python3-dev python3-venv python3-pip \
    libffi-dev libssl-dev libncurses-dev pkg-config \
    curl wget unzip iproute2 dbus nginx supervisor \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m -s /bin/bash developer \
    && echo 'developer ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/developer \
    && chmod 0440 /etc/sudoers.d/developer

RUN git clone --depth=1 https://github.com/Klipper3d/klipper.git ${KLIPPER_DIR} \
    && git clone --depth=1 https://github.com/Arksine/moonraker.git ${MOONRAKER_DIR} \
    && mkdir -p ${FLUIDD_DIR} \
    && wget -O /tmp/fluidd.zip https://github.com/fluidd-core/fluidd/releases/latest/download/fluidd.zip \
    && unzip /tmp/fluidd.zip -d ${FLUIDD_DIR} \
    && rm /tmp/fluidd.zip \
    && chown -R developer:developer ${HOME}

USER developer
WORKDIR ${MOONRAKER_DIR}

RUN ./scripts/install-moonraker.sh -z -x -d ${PRINTER_DATA}

WORKDIR ${KLIPPER_DIR}

RUN python3 -m venv ${KLIPPY_ENV} \
    && ${KLIPPY_ENV}/bin/pip install --upgrade pip wheel \
    && ${KLIPPY_ENV}/bin/pip install -r ${KLIPPER_DIR}/scripts/klippy-requirements.txt \
    && printf '%s\n' 'CONFIG_MACH_LINUX=y' > .config \
    && make olddefconfig \
    && make

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

EXPOSE 80 7125

CMD ["/usr/bin/supervisord", "-n", "-c", "/etc/supervisor/conf.d/klipper-dev.conf"]
