# Changelog

## 1.0.1

Released only as an **experimental** Linux x64 AppImage. The macOS and Windows
packages stay at 1.0.0, which has the same features.

- Linux AppImage, built in Docker on Ubuntu 22.04 with Qt 6.8.3. Not yet tried
  with DaVinci Resolve on Linux. Tested on Xubuntu 24.04, where Sigma fp takes
  convert into the same image as with the macOS app.
- Takes from the original Pocket Cinema Camera can't be converted on Linux yet:
  the folder where Resolve for Linux reads camera profiles is not known, so the
  app can't install the profile.
- Fix: the hidden `._` files that macOS leaves next to each frame when it copies
  to an exFAT or FAT card are ignored. Before, they made the take "Not
  supported".

## 1.0.0

The first public release.

- Converts the CinemaDNG takes of the Sigma fp into Blackmagic RAW, one BRAW per
  take, with the take's WAV inside. Also the original Blackmagic Pocket Cinema
  Camera (2013), through an experimental camera profile the app installs, and,
  through a generic route with the white balance fixed, other cameras.
- The camera of each Sigma fp take is written automatically ("SIGMA fp" or
  "SIGMA fp L", as in the DNG).
- Original Pocket Cinema Camera: the BRAW matches the DNG as Resolve develops it
  without colour management, white balance included, and opens in P3-D65
  linear. With the profile, the app installs a LUT for DaVinci Resolve, measured
  on its DNG decoder, from DaVinci Wide Gamut / Intermediate to the DNG's default
  "Blackmagic Design" look, and a second one that gives Sigma fp takes the same
  look.
- Compression at a target ratio, 3:1 to 12:1, or at constant quality, Q0, Q1,
  Q3 and Q5, or any quantiser from 0 to 24.
- Resolution: the original or a narrower width, keeping the aspect ratio of each
  take, open gate included.
- Noise reduction before encoding: Soft, Medium, Strong or custom percentages
  for luma and colour, with a window that compares the original and the result.
- Gyro data from fpSup's Gyro2 module, written into the BRAW as motion tracks and
  in a Gyroflow project with a lens profile from the DNGs; optionally also as
  `.gcsv` and `.fsg2` files.
- Each BRAW gets its own camera ID, the take's name as clip number and the
  recording date of the DNGs.
- Desktop builds for Apple Silicon (macOS 26 or later) and x64 Windows, each
  with the conversion engine inside.
