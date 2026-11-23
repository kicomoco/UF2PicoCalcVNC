# PicoCalc VNC

A VNC (RFB 3.3/3.7/3.8) client firmware for the **ClockworkPi PicoCalc** fitted
with a **Raspberry Pi Pico 2 W** (RP2350), packaged as a UF2 that can be
launched from [UF2Loader](https://github.com/pelrun/uf2loader).

## Features

- WiFi client that remembers up to 16 networks (most recently used first).
  On start-up it scans and automatically joins the strongest saved network in
  range (hidden saved networks are tried afterwards). Esc skips auto-connect.
- Network list with signal strength, password entry, and a *Hidden network*
  option for SSIDs that are not broadcast.
- Up to 16 saved VNC connections, with *New Connection* at the bottom of the
  list. Connections can be added, edited and deleted while offline; connecting
  needs WiFi.
- Encodings: Raw, CopyRect, RRE, Hextile and ZRLE, plus the LastRect
  pseudo-encoding. Colour depth is 16-bit (RGB565) or 8-bit (BGR233).
- Zoom modes: *Fit* (whole desktop scaled to 320 px), *50%* and *100%*. In 50%
  and 100% the view pans automatically when the pointer nears an edge.
- Security types *None* and *VNC password* (DES).

## Controls

| Key | Action |
|-----|--------|
| F1 | Help for the current screen (works anywhere) |
| F2 | Not connected: toggle between the WiFi and VNC screens. Connected: special keys menu |
| F3 | Disconnect from the VNC server |
| F4 | Left mouse button (hold to drag) |
| F5 | Right mouse button |
| Arrows | Move the mouse pointer (hold to accelerate) |
| F6 (Shift+F1) | Switch the arrow keys between mouse and cursor keys |
| F7 (Shift+F2) | Cycle zoom: Fit / 50% / 100% |
| F8 (Shift+F3) | Refresh the screen |
| F9 / F10 (Shift+F4/F5) | Scroll wheel up / down |

The F2 menu sends Ctrl+Alt+Del, Alt+Tab, Alt+F4, Esc, the Windows key, F1–F12,
middle click and similar keys. Ctrl and Alt combine with the next key typed.

In lists: Up/Down select and Enter acts on the selection. On the WiFi screen,
R rescans and D/Del forgets a saved network. On the VNC screen, E edits and
D/Del deletes a connection.

## Building

You need the Pico SDK 2.2.0 and the ARM toolchain. The defaults expect the VS
Code Pico extension layout under `~/.pico-sdk`; set `PICO_SDK_PATH` and
`PICO_TOOLCHAIN_PATH` to override them.

```sh
./build.sh
```

The build writes `build/picocalc_vnc.uf2`. It targets `pico2_w` with the
`rp2350-arm-s` platform.

## Automated releases

The [release workflow](.github/workflows/release.yml) builds on pushes to
`main` and publishes the UF2 plus a SHA-256 checksum in
[GitHub Releases](https://github.com/kicomoco/UF2PicoCalcVNC/releases).
The first release is `v1.0.0`; later releases increment the highest version
tag's patch number. The same version is embedded in the firmware.
Retrying a released commit reuses its tag instead of incrementing again.
No version-bump commits are created.

The workflow uses Pico SDK 2.2.0 and the repository's built-in `GITHUB_TOKEN`;
no additional secrets are needed. GitHub Actions must be enabled and repository
rules must permit the workflow to create tags and releases.

Local builds default to version `1.0.0`. To build a specific version:

```sh
cmake -S . -B build -DPICOCALC_VNC_VERSION=1.0.1
cmake --build build -j8
```

## Installing

1. Copy `build/picocalc_vnc.uf2` to the `pico2-apps/` folder on the PicoCalc's
   SD card.
2. Start the PicoCalc, choose **PicoCalc VNC** in UF2Loader, and launch it.

## Configuration

Settings are saved on the SD card in `/picocalc-vnc.ini`. Without an SD card
the app still runs, but nothing is remembered.

**WiFi and VNC passwords are stored in plain text.**

## Limitations

- Only *None* and *VNC password* authentication are supported, with no
  encryption. Apple ARD, VeNCrypt/TLS and RealVNC RA2 are not supported, so
  configure the server to allow standard VNC authentication. For example, on
  macOS Screen Sharing set a "VNC viewers may control screen with password".
- The largest supported remote desktop is 4096×4096.
- Clipboard transfer and the remote cursor shape are not supported; a local
  arrow cursor is drawn instead.
- This firmware has not been tested on real hardware. The decoders, scaling
  and DES authentication were verified on a host against a test server. The
  hardware-specific parts may need adjustment: the keyboard I2C protocol,
  battery reading, LCD and SD SPI timings, and WiFi auth modes.

## Third-party code

- [FatFs](http://elm-chan.org/fsw/ff/) R0.16 (BSD-style licence)
- [miniz](https://github.com/richgel999/miniz) tinfl inflater (MIT)
- [Spleen](https://github.com/fcambus/spleen) fonts (BSD 2-Clause, see
  `third_party/SPLEEN-LICENSE`)
