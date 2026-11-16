# Entorno para construir las imagenes de PS1 de World Rally 1 y 2 desde tus ROM de MAME (herramienta/LEEME.md).
# Solo herramientas libres: el codigo de los ports va en el repositorio y las ROM y la BIOS las pones tu al lanzarlo.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential gcc-mipsel-linux-gnu binutils-mipsel-linux-gnu \
      python3 python3-numpy python3-scipy python3-pil \
      git ca-certificates curl cmake g++ zip unzip \
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
# fuentes de MAME (BSD-3-Clause) de donde wrally_ps1/tools/gaelcrpt_extrae.py saca el descifrado de la VRAM de Gaelco
# al construir (el repositorio no lo lleva): commit fijo y SHA-256 de cada uno
ARG MAME_REV=f34f02505e32c1993c6a782b6814232cbfc74e36
RUN mkdir -p /opt/mame && cd /opt/mame && for f in gaelcrpt.cpp gaelcrpt.h wrally.cpp; do \
      curl -fsSL -o $f https://raw.githubusercontent.com/mamedev/mame/$MAME_REV/src/mame/gaelco/$f || exit 1; done \
    && printf '%s  %s\n' bc1970463732e3e565c0f5c0c53638c6d2a014d495956eae43816b5272fff11a gaelcrpt.cpp \
         b8ac115c7c667390c5cde43ee3d5aa3c3722310e25691c80d9e2f6c285d2f931 gaelcrpt.h \
         ac7ccd985c226c706e4a63e3319fd2e1cdf93ca2f2cc18ac09bae60147ebd883 wrally.cpp | sha256sum -c -
ENV NUGGET=/opt/nugget MKPSXISO=/usr/local/bin/mkpsxiso WR_MAME=/opt/mame
COPY . /opt/port
WORKDIR /opt/port
ENTRYPOINT ["/opt/port/herramienta/crear_imagen.sh"]
