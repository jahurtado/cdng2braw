# Construcción del conversor CinemaDNG -> BRAW en Windows x64 (MSVC 2022, Qt 6, dependencias de vcpkg).
# Equivalente del Makefile de macOS. El Makefile no sirve aquí: asume bash, Homebrew y /Applications.
#
#   .\build.ps1 native     compila el motor C++ y el proceso de trabajo (build\native)
#   .\build.ps1 app        compila además la app Qt (build\qt-app)
#   .\build.ps1 test       pasa las suites de CTest de los dos árboles
#   .\build.ps1 package    prepara la carpeta portable, su ZIP y el SHA256 en out\cdng2braw-win64
#   .\build.ps1 installer  genera además el instalador (Inno Setup, como eclipse-aligner y film-developer)
#   .\build.ps1 clean      borra los árboles de compilación
#
# Rutas ajustables por parámetro o variable de entorno:
#   -QtDir     C:\Qt\6.8.3\msvc2022_64        (CDNG_QT_DIR)
#   -VcpkgDir  C:\vcpkg                       (CDNG_VCPKG_DIR)
#   -Jobs      número de trabajos de compilación

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('native', 'app', 'test', 'package', 'installer', 'clean')]
    [string]$Target = 'app',
    [string]$QtDir = $(if ($env:CDNG_QT_DIR) { $env:CDNG_QT_DIR } else { 'C:\Qt\6.8.3\msvc2022_64' }),
    [string]$VcpkgDir = $(if ($env:CDNG_VCPKG_DIR) { $env:CDNG_VCPKG_DIR } else { 'C:\vcpkg' }),
    [int]$Jobs = [Environment]::ProcessorCount
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildApp = Join-Path $root 'build\qt-app'
$buildNative = Join-Path $root 'build\native'
$outDir = Join-Path $root 'out\cdng2braw-win64'
$version = '1.0.1'

function Assert-Tool($path, $message) {
    if (-not (Test-Path $path)) { throw $message }
}

if ($Target -eq 'clean') {
    foreach ($dir in @($buildApp, $buildNative)) {
        if (Test-Path $dir) { Remove-Item $dir -Recurse -Force; Write-Host "borrado $dir" }
    }
    return
}

$toolchain = Join-Path $VcpkgDir 'scripts\buildsystems\vcpkg.cmake'
Assert-Tool $toolchain "no existe el toolchain de vcpkg en $toolchain (ajusta -VcpkgDir)"
Assert-Tool $QtDir "no existe la instalación de Qt en $QtDir (ajusta -QtDir)"

# Opciones comunes. El árbol nativo no necesita Qt; el de la app sí, y además compila las pruebas.
$common = @(
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_TARGET_TRIPLET=x64-windows"
)

function Invoke-Cmake($arguments) {
    & cmake @arguments
    if ($LASTEXITCODE -ne 0) { throw "cmake falló: cmake $($arguments -join ' ')" }
}

function Build-Native {
    Invoke-Cmake (@('-S', $root, '-B', $buildNative) + $common)
    Invoke-Cmake @('--build', $buildNative, '--config', 'Release', '--parallel', $Jobs)
}

function Build-App {
    Invoke-Cmake (@('-S', $root, '-B', $buildApp) + $common + @(
        '-DCDNG_BUILD_APP=ON',
        "-DCMAKE_PREFIX_PATH=$QtDir"
    ))
    Invoke-Cmake @('--build', $buildApp, '--config', 'Release', '--parallel', $Jobs)
}

switch ($Target) {
    'native' { Build-Native }
    'app' { Build-Native; Build-App }
    'test' {
        Build-Native; Build-App
        # Los ejecutables de prueba de la app se enlazan con Qt dinámicamente y no están desplegados con
        # windeployqt: sin la carpeta bin de Qt en el PATH fallan al arrancar con 0xc0000135 (DLL no encontrada).
        $env:PATH = (Join-Path $QtDir 'bin') + ';' + $env:PATH
        & ctest --test-dir $buildNative --build-config Release --output-on-failure
        $nativeFailed = $LASTEXITCODE -ne 0
        & ctest --test-dir $buildApp --build-config Release --output-on-failure
        $appFailed = $LASTEXITCODE -ne 0
        if ($nativeFailed -or $appFailed) { throw 'hay pruebas que fallan' }
    }
    default {
        Build-Native; Build-App
        $worker = Join-Path $buildNative 'converter'
        $binary = Join-Path $buildNative 'bin\Release\cdng-convert.exe'
        if (-not (Test-Path $binary)) { $binary = Join-Path $buildNative 'bin\cdng-convert.exe' }
        Assert-Tool $binary "no se encontró cdng-convert.exe en $buildNative"
        # DLL de las dependencias: cuando el toolchain de vcpkg está integrado en CMake, las instala dentro del
        # árbol de compilación (build\native\vcpkg_installed), no en la raíz del repositorio. Se busca ahí primero,
        # y como alternativa un vcpkg_installed en la raíz (si se hizo `vcpkg install` a mano con el manifiesto).
        $dllDir = @(
            (Join-Path $buildNative 'vcpkg_installed\x64-windows\bin'),
            (Join-Path $root 'vcpkg_installed\x64-windows\bin')
        ) | Where-Object { Test-Path $_ } | Select-Object -First 1
        if (-not $dllDir) { throw "no se encontraron las DLL de vcpkg (x64-windows\bin) ni en el árbol de compilación ni en la raíz" }
        & python (Join-Path $root 'tools\stage_converter.py') --binary $binary --out $worker --dll-dir $dllDir
        if ($LASTEXITCODE -ne 0) { throw 'stage_converter.py falló' }

        if (Test-Path $outDir) { Remove-Item $outDir -Recurse -Force }

        # Runtime de MSVC: se localiza la carpeta Microsoft.VC*.CRT del redistribuible y se copia al paquete, para
        # que arranque en un equipo sin el redistribuible de Visual C++ instalado. windeployqt --compiler-runtime
        # no sirve aquí: con Build Tools no encuentra el redistribuible y deja el paquete sin runtime.
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        $redist = $null
        if (Test-Path $vswhere) {
            $vsPath = & $vswhere -products * -latest -property installationPath
            if ($vsPath) {
                # La versión del redistribuible no tiene que coincidir con la del compilador: se toma la más alta.
                $redist = Get-ChildItem (Join-Path $vsPath 'VC\Redist\MSVC') -Directory -ErrorAction SilentlyContinue |
                    ForEach-Object { Join-Path $_.FullName 'x64' } |
                    Where-Object { Test-Path $_ } |
                    ForEach-Object { Get-ChildItem $_ -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue } |
                    Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
            }
        }
        if (-not $redist) { Write-Warning 'no se encontró el redistribuible de MSVC: el paquete saldrá sin el runtime' }

        $packageArgs = @(
            '--build-dir', $buildApp, '--out', $outDir, '--converter', $worker,
            '--windeployqt', (Join-Path $QtDir 'bin\windeployqt.exe'), '--zip'
        )
        if ($redist) { $packageArgs += @('--vc-redist-dir', $redist) }
        & python (Join-Path $root 'tools\package_app_windows.py') @packageArgs
        if ($LASTEXITCODE -ne 0) { throw 'package_app_windows.py falló' }

        # La versión en un solo sitio, como en los otros proyectos: el .iss la lee de aquí.
        # Sin BOM: `Set-Content -Encoding utf8` en PowerShell 5.1 lo añade, y el FileRead del .iss se lo traga
        # dentro de la cadena, con lo que el instalador sale llamándose «cdng2braw-<BOM>0.7.0-x64-setup.exe».
        $versionFile = Join-Path $root 'out\version.txt'
        [System.IO.File]::WriteAllText($versionFile, $version, (New-Object System.Text.UTF8Encoding($false)))

        # SHA256 del ZIP, para poder publicar la suma junto al archivo.
        $zip = "$outDir.zip"
        if (Test-Path $zip) {
            $sum = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
            [System.IO.File]::WriteAllText("$zip.sha256", "$sum  $(Split-Path $zip -Leaf)`n",
                (New-Object System.Text.UTF8Encoding($false)))
            Write-Host "ZIP: $zip"
            Write-Host "SHA256: $sum"
        }
        Write-Host "Paquete: $outDir"

        if ($Target -eq 'installer') {
            # winget puede instalarlo por usuario (LOCALAPPDATA) o para todo el equipo: se prueban las tres rutas.
            $iscc = @(
                "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
                'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
                'C:\Program Files\Inno Setup 6\ISCC.exe'
            ) | Where-Object { Test-Path $_ } | Select-Object -First 1
            if (-not $iscc) {
                $found = Get-Command iscc -ErrorAction SilentlyContinue
                if ($found) { $iscc = $found.Source }
            }
            if (-not $iscc) { throw 'no se encontró ISCC.exe; instala Inno Setup (winget install JRSoftware.InnoSetup)' }
            & $iscc (Join-Path $root 'packaging\cdng2braw.iss')
            if ($LASTEXITCODE -ne 0) { throw 'iscc falló' }
            $setup = Join-Path $root "out\cdng2braw-$version-x64-setup.exe"
            if (-not (Test-Path $setup)) { throw "iscc no dejó el instalador en $setup" }
            $sum = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLower()
            [System.IO.File]::WriteAllText("$setup.sha256", "$sum  $(Split-Path $setup -Leaf)`n",
                (New-Object System.Text.UTF8Encoding($false)))
            Write-Host "Instalador: $setup"
            Write-Host "SHA256: $sum"
        }
    }
}
