#!/usr/bin/env python3
"""Prepara la carpeta del proceso de trabajo C++ para el paquete: cdng-convert, los ficheros de model/ que necesita
y la tabla de traducciones.

La lista de ficheros es la que comprueba el ejecutable al arrancar (native/engine/worker/common.cpp, checkResources).

En macOS LibRaw y compañía van enlazadas de forma estática (CDNG_STATIC_DEPS) y se comprueba con otool que no
queda ninguna dependencia fuera del sistema. En Windows van como DLL junto al ejecutable (enlace dinámico, que es
el que pide la LGPL de LibRaw): se copian de la carpeta bin de vcpkg que se indique con --dll-dir y la prueba de
que el juego está completo es que `cdng-convert --self-test` arranque, porque si falta una DLL no arranca.
En Linux, como en macOS, va todo estático y se comprueba con readelf que solo pide glibc.
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESOURCES = [
    'model/cam/orig.braw', 'model/quant_steps.json', 'model/vlc-table.json', 'model/noise_synthesis.json',
    'model/video_assist_input_transform.json', 'model/raw_to_709.json',
    'model/pocket_og_cinemadng_signal.json', 'model/pocket_og_cinemadng_camera_support.json',
    'model/nr_normals_6000x64.f32', 'model/pocket_inverse_gen4.bin', 'model/pocket_forward_gen4.bin',
]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--dll-dir', type=Path, help='carpeta bin de vcpkg con las DLL a copiar (solo Windows)')
    a = ap.parse_args()
    windows = sys.platform == 'win32'
    if a.dll_dir and not windows:
        ap.error('--dll-dir es solo para Windows')
    if a.out.exists():
        shutil.rmtree(a.out)
    a.out.mkdir(parents=True)
    binary = a.out / ('cdng-convert.exe' if windows else 'cdng-convert')
    shutil.copy2(a.binary, binary)
    for name in RESOURCES:
        (a.out / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, a.out / name)
    (a.out / 'translations').mkdir()
    shutil.copy2(ROOT / 'app/i18n/es.json', a.out / 'translations/es.json')
    if windows:
        if not a.dll_dir:
            ap.error('en Windows hace falta --dll-dir con las DLL de vcpkg')
        if not a.dll_dir.is_dir():
            ap.error(f'no existe la carpeta de DLL: {a.dll_dir}')
        copied = sorted(dll.name for dll in a.dll_dir.glob('*.dll'))
        for dll in a.dll_dir.glob('*.dll'):
            shutil.copy2(dll, a.out / dll.name)
        if not copied:
            raise SystemExit(f'no hay ninguna DLL en {a.dll_dir}')
        print(f'DLL copiadas: {", ".join(copied)}')
    elif sys.platform == 'darwin':
        # Solo bibliotecas del sistema: LibRaw y compañía van enlazadas de forma estática.
        links = subprocess.run(['/usr/bin/otool', '-L', binary], capture_output=True, text=True, check=True).stdout
        external = [line.split()[0] for line in links.splitlines()[1:] if not line.split()[0].startswith(('/usr/lib/', '/System/'))]
        if external:
            raise SystemExit(f'cdng-convert depende de bibliotecas externas: {external}')
    else:
        # Linux: también estático (libstdc++ y libgomp incluidas), así que solo puede pedir glibc.
        dynamic = subprocess.run(['readelf', '-d', binary], capture_output=True, text=True, check=True).stdout
        needed = [line.split('[')[1].rstrip(']') for line in dynamic.splitlines() if '(NEEDED)' in line]
        glibc = ('libc.so', 'libm.so', 'libpthread.so', 'libdl.so', 'librt.so', 'ld-linux')
        external = [lib for lib in needed if not lib.startswith(glibc)]
        if external:
            raise SystemExit(f'cdng-convert depende de bibliotecas externas: {external}')
    # Arranca el ejecutable ya colocado: si falta una DLL o un fichero de model/, falla aquí.
    subprocess.run([binary, '--self-test'], check=True)
    print(f'Proceso de trabajo: {a.out}')


if __name__ == '__main__':
    main()
