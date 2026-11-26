#!/usr/bin/env bash
# Construye el AppImage de Linux x86_64 dentro del contenedor de packaging/linux/Dockerfile.
# Desde el Mac: ./build-linux.sh (lo lanza con docker). Deja build/linux/ y out/cdng2braw-<versión>-x86_64.AppImage.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$PWD
QT=${CDNG_QT_DIR:?falta CDNG_QT_DIR}
JOBS=${JOBS:-$(nproc)}
VERSION=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' vcpkg.json | head -1)
BUILD=$ROOT/build/linux
COMMON=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
        -DVCPKG_TARGET_TRIPLET=x64-linux -DVCPKG_INSTALLED_DIR=$BUILD/vcpkg_installed -DCDNG_STATIC_DEPS=ON)

# 1. Motor y app, con sus pruebas.
cmake -S . -B "$BUILD/app" "${COMMON[@]}" -DCDNG_BUILD_APP=ON -DCMAKE_PREFIX_PATH="$QT"
cmake --build "$BUILD/app" --parallel "$JOBS"
ctest --test-dir "$BUILD/app" --output-on-failure

# 2. Proceso de trabajo con sus ficheros de model/ (la misma lista que en macOS y Windows).
python3 tools/stage_converter.py --binary "$BUILD/app/bin/cdng-convert" --out "$BUILD/converter"

# 3. AppDir: la app en usr/bin, el motor en usr/bin/converter (app/conversion_job.cpp lo busca ahí).
# Dentro del contenedor y no en el volumen del Mac: linuxdeploy copia conservando permisos y el volumen lo rechaza.
APPDIR=/tmp/AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/512x512/apps" "$APPDIR/usr/share/doc/cdng2braw"
cp "$BUILD/app/app/cdng2braw" "$BUILD/app/app/cdng-profile-helper" "$APPDIR/usr/bin/"
cp -R "$BUILD/converter" "$APPDIR/usr/bin/converter"
cp app/THIRD_PARTY_NOTICES.md "$APPDIR/usr/bin/THIRD_PARTY_NOTICES.md"
# El Qt de aqtinstall no trae carpeta de licencias: los textos de la LGPL v3 (Qt) y la GPL v3 (la app, y la de GCC con
# su excepción) salen de las del sistema. Sin ellos el paquete no se publica.
cp /usr/share/common-licenses/LGPL-3 /usr/share/common-licenses/GPL-3 "$APPDIR/usr/share/doc/cdng2braw/"
cp LICENSE "$APPDIR/usr/share/doc/cdng2braw/LICENSE"
cp packaging/linux/cdng2braw.desktop "$APPDIR/usr/share/applications/"
cp packaging/linux/cdng2braw.png "$APPDIR/usr/share/icons/hicolor/512x512/apps/cdng2braw.png"

export QMAKE=$QT/bin/qmake LD_LIBRARY_PATH=$QT/lib EXTRA_QT_MODULES="" QML_SOURCES_PATHS=""
OUT=$ROOT/out
mkdir -p "$OUT"
cd "$OUT"
rm -f cdng2braw-*-x86_64.AppImage*
# Primero solo el AppDir (bibliotecas y plugins de Qt); el AppImage se hace después de comprobarlo.
linuxdeploy-x86_64.AppImage --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/cdng2braw" --executable "$APPDIR/usr/bin/cdng-profile-helper" \
    --desktop-file "$APPDIR/usr/share/applications/cdng2braw.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/512x512/apps/cdng2braw.png" \
    --plugin qt

# 4. Lo que PUBLICAR.md pide de cada paquete: que no viaja la ruta de build ni del usuario, y que van las licencias.
# Se permite /home/qt/work/: es la máquina de compilación de The Qt Company, en las rutas de sus asserts.
found=$(grep -raoh -e '/Users/[^[:cntrl:]]*' -e '/home/[^[:cntrl:]]*' "$APPDIR" | grep -v '^/home/qt/work/' || true)
if [ -n "$found" ]; then
    echo "el AppDir contiene rutas de usuario:"; echo "$found" | sort -u | head -20; exit 1
fi
for f in usr/bin/THIRD_PARTY_NOTICES.md usr/share/doc/cdng2braw/LGPL-3 usr/share/doc/cdng2braw/GPL-3; do
    test -s "$APPDIR/$f" || { echo "falta $f en el AppDir"; exit 1; }
done

# 5. La app empaquetada arranca sin pantalla con solo lo que lleva el AppDir. Se arranca el AppRun y no el AppImage
# porque en el Mac (Rosetta) el AppImage no se puede ejecutar por su marca en la cabecera ELF.
env -u LD_LIBRARY_PATH QT_QPA_PLATFORM=offscreen "$APPDIR/AppRun" --profile-status

# 6. El AppImage, su suma y la lista de lo que lleva dentro (para revisarlo sin poder montarlo en el Mac).
LDAI_OUTPUT=cdng2braw-$VERSION-x86_64.AppImage linuxdeploy-x86_64.AppImage --appdir "$APPDIR" --output appimage
find "$APPDIR" -type f | sed "s|^$APPDIR/||" | sort > "cdng2braw-$VERSION-x86_64.AppImage.files"
sha256sum "cdng2braw-$VERSION-x86_64.AppImage" | tee "cdng2braw-$VERSION-x86_64.AppImage.sha256"
