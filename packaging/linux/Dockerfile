# Entorno de construcción del AppImage de Linux x86_64. Ubuntu 22.04 (glibc 2.35): el AppImage arranca en esa
# versión y en las posteriores (Debian 12, Fedora 36, Mint 21…). En un Mac con Apple Silicon corre con Rosetta
# (colima --vm-type vz --vz-rosetta y docker --platform linux/amd64). Se lanza con build-linux.sh.
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ninja-build cmake pkg-config git curl ca-certificates zip unzip tar file patchelf \
        python3 python3-pip python3-venv autoconf automake libtool \
        libgl1-mesa-dev libegl1 libfontconfig1 libfreetype6 libdbus-1-3 libglib2.0-0 \
        libxkbcommon0 libxkbcommon-x11-0 libx11-xcb1 libxcb1 libxcb-cursor0 libxcb-icccm4 libxcb-image0 \
        libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 \
        libxcb-xfixes0 libxcb-sync1 libxcb-shm0 libxcb-util1 libwayland-client0 libwayland-cursor0 libwayland-egl1 \
    && rm -rf /var/lib/apt/lists/*

# Qt 6.8.3, la misma versión que en Windows (build.ps1).
RUN python3 -m pip install --no-cache-dir aqtinstall==3.2.1 \
    && aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O /opt/Qt
ENV CDNG_QT_DIR=/opt/Qt/6.8.3/gcc_64

# vcpkg con la misma línea base que el manifiesto (vcpkg.json, builtin-baseline).
RUN git clone https://github.com/microsoft/vcpkg /opt/vcpkg \
    && git -C /opt/vcpkg checkout 8469b01206bff843500592f18dcab88b13331da6 \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/opt/vcpkg

# linuxdeploy y su plugin de Qt; sin FUSE en el contenedor, se ejecutan extraídos. Los bytes 8-10 de la cabecera
# ELF («AI\x02», la marca de AppImage) se ponen a cero: con ellos Rosetta y qemu-user rechazan el ejecutable
# («Exec format error»). Solo se toca a las herramientas; el AppImage que generan conserva su marca.
RUN mkdir -p /opt/linuxdeploy && cd /opt/linuxdeploy \
    && curl -fsSLO https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
    && curl -fsSLO https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage \
    && for f in *.AppImage; do printf '\0\0\0' | dd of="$f" bs=1 seek=8 count=3 conv=notrunc; done \
    && chmod +x *.AppImage
ENV APPIMAGE_EXTRACT_AND_RUN=1 PATH=/opt/linuxdeploy:$PATH LANG=C.UTF-8
