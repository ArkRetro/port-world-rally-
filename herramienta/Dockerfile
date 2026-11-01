# Entorno para construir las imagenes de PS1 de World Rally 1 y 2 desde tus ROM de MAME (herramienta/LEEME.md).
# Solo herramientas libres: el codigo de los ports va en el repositorio y las ROM y la BIOS las pones tu al lanzarlo.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential gcc-mipsel-linux-gnu binutils-mipsel-linux-gnu \
      python3 python3-numpy python3-scipy python3-pil \
      git ca-certificates cmake g++ zip unzip \
      mednafen xvfb ffmpeg \
    && rm -rf /var/lib/apt/lists/*
# mkpsxiso (imagenes de CD) y nugget (los .ld del enlazado de los EXE de PS1), en las versiones con que se probo
ARG MKPSXISO_REV=a6b11ea86e67c189137ac50a4066044b3fdb0525
ARG NUGGET_REV=8a129f0498131ecefb27f5eb5eee30d016d0fe45
RUN git clone https://github.com/Lameguy64/mkpsxiso /opt/mkpsxiso \
    && cd /opt/mkpsxiso && git checkout -q $MKPSXISO_REV && git submodule update -q --init --recursive \
    && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j"$(nproc)" >/dev/null \
    && cp build/mkpsxiso /usr/local/bin/ && rm -rf /opt/mkpsxiso
RUN git clone https://github.com/pcsx-redux/nugget /opt/nugget && cd /opt/nugget && git checkout -q $NUGGET_REV \
    && rm -rf .git
ENV NUGGET=/opt/nugget MKPSXISO=/usr/local/bin/mkpsxiso
COPY . /opt/port
WORKDIR /opt/port
ENTRYPOINT ["/opt/port/herramienta/crear_imagen.sh"]
