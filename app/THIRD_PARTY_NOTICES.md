# Third-party components

CDNG to BRAW Converter (cdng2braw) is written by Jose A. Hurtado (www.elcacharrista.com). It includes the following third-party
components. Licence texts supplied by the local installations are retained in the package where available.

## Conversion engine (`Contents/Resources/converter/cdng-convert`)

Statically linked, unmodified:

- **LibRaw 0.22.1** — Copyright LibRaw LLC. Used under the Common Development and Distribution License (CDDL) 1.0,
  one of LibRaw's two licence options (LGPL 2.1 / CDDL 1.0). Source code: https://www.libraw.org/ and
  https://github.com/LibRaw/LibRaw (tag 0.22.1).
- **Little CMS 2** — Copyright Marti Maria Saguer. MIT licence. https://www.littlecms.com/
- **libjpeg-turbo** — Copyright the libjpeg-turbo project and the Independent JPEG Group. IJG licence and
  Modified (3-clause) BSD licence. "This software is based in part on the work of the Independent JPEG Group."
  https://libjpeg-turbo.org/
- **LLVM OpenMP runtime (libomp)** — Copyright the LLVM project. Apache License 2.0 with LLVM Exceptions.
  https://openmp.llvm.org/
- **simdjson** — Copyright the simdjson authors. Apache License 2.0 (also available under MIT). https://simdjson.org/
- **zlib** and **Accelerate** are system libraries of macOS.

## User interface

Qt Core, Gui, Widgets and Network with their platform plugins, dynamically linked. Qt is copyright The Qt Company Ltd.
and other contributors and is used under the GNU Lesser General Public License v3. Qt source and licence information:
https://www.qt.io/licensing/open-source-lgpl-obligations and https://code.qt.io/.

## Linux AppImage

The Linux package is built on Ubuntu 22.04 and differs from the macOS one in these components:

- The conversion engine (`usr/bin/converter/cdng-convert`) links statically, unmodified, **LibRaw 0.22.3** (CDDL 1.0,
  as above; https://github.com/LibRaw/LibRaw, tag 0.22.3), Little CMS 2, libjpeg-turbo, simdjson and **zlib**
  (zlib licence, https://zlib.net/), built with vcpkg, and the GCC runtime libraries **libstdc++, libgcc and
  libgomp** (GNU OpenMP), copyright the Free Software Foundation, under the GNU GPL v3 with the GCC Runtime Library
  Exception 3.1. It does not use LLVM OpenMP or Accelerate.
- **Qt 6.8.3** (Core, Gui, Widgets, Network, DBus, Svg and the XCB platform plugin), dynamically linked under the
  LGPL v3 as above, with the **ICU 73** libraries that come with it (Unicode licence, https://icu.unicode.org/). The
  texts of the LGPL v3 and the GPL v3 are in `usr/share/doc/cdng2braw/`. The Qt libraries are ordinary shared
  libraries in `usr/lib/` and can be replaced.
- The system libraries the AppImage carries so that it runs on other distributions (XCB, xkbcommon, GLib, D-Bus,
  libpng, zstd and others) come from Ubuntu 22.04 unmodified. Each one keeps its copyright and licence file in
  `usr/share/doc/<package>/copyright`.

## Not included

No source or binary from cDNG Raw Convert is included. The Blackmagic RAW SDK and DaVinci Resolve are not bundled.
Blackmagic RAW, DaVinci Resolve and Blackmagic Video Assist are trademarks of Blackmagic Design; SIGMA and fp are
trademarks of SIGMA Corporation. This application is not affiliated with or endorsed by either company.

## Source

CDNG to BRAW Converter is free software under the GNU General Public License, version 3 or later. Its source code,
including the scripts that build these packages, is at https://github.com/jahurtado/cdng2braw.
