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

WiFi scans and connections, VNC connections, and firmware updates show a
spinning activity indicator. Press Esc to cancel these operations.

## Brightness settings

Press **S** on either connection screen, choose **Brightness settings** on the
WiFi screen, or open **F2 > Brightness settings** during a VNC session.
Use Up/Down to select a field and Left/Right to choose a level, then select
**Apply & Save**. Cancel/Esc leaves brightness unchanged.

- Screen: **10-100%**, so the app cannot accidentally turn the display off.
- Keyboard: **Off or 10-100%**.
- Levels are approximate: the PicoCalc controller rounds to discrete PWM steps,
  so adjacent percentage options can produce the same physical brightness.
- Preferences are stored as `screen_brightness` and `keyboard_brightness` in the
  `[general]` section of `/picocalc-vnc.ini`, and restored at startup.
- Existing configurations without these settings retain the controller's current
  brightness. Without an SD card, changes apply only for the current session.

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
CI caches Arm GNU Toolchain 14.2.Rel1 and verifies its download against Arm's
published SHA-256 checksum. Only the small native build dependencies are installed
through apt, with bounded network waits and an installation timeout.

To initialise a fresh SDK checkout for builds, include its TLS dependency:

```sh
git -C "$PICO_SDK_PATH" submodule update --init lib/cyw43-driver lib/lwip lib/tinyusb lib/mbedtls
```

Local builds default to version `1.0.0`. To build a specific version:

```sh
cmake -S . -B build -DPICOCALC_VNC_VERSION=1.0.1
cmake --build build -j8
```

## Installing

1. Copy `build/picocalc_vnc.uf2` to the `pico2-apps/` folder on the PicoCalc's
   SD card.
2. Start the PicoCalc, choose **PicoCalc VNC** in UF2Loader, and launch it.

## Checking for updates

Press **U** on either connection screen, or select **Firmware updates** on the
WiFi screen. The update screen shows the running version and can check the latest
public GitHub release. When a newer version is found, select **Download update &
reboot** and confirm.

The app downloads the release checksum and UF2 using HTTPS with certificate,
hostname and validity-date verification. It obtains network time from
Cloudflare NTP (outbound UDP port 123 must be allowed). Trust anchors are
ISRG Root X1 and USERTrust ECC in `src/update-ca.pem`; certificate-chain changes
at GitHub may require a manually installed firmware with updated roots.
The SHA-256 checksum and RP2350 ARM secure UF2 blocks are checked by reading the
download back from the SD card. This is a checksum from the same GitHub release,
not an independent firmware signature.

Only a completely verified download is renamed to
`/pico2-apps/picocalc_vnc-vVERSION.uf2`. Existing app files and settings are not
replaced. After verification the app **automatically reboots into UF2Loader**.
Select the downloaded version there to install it. `BOOT2350.UF2` must be at the
SD-card root. Esc cancels an in-progress request without rebooting.

A first release must be published before the checker can find an update; private
repositories and prerelease version tags are not supported. The firmware does
not check in the background or write its own flash. The SD download and automatic
UF2Loader reboot still need verification on a PicoCalc.

Updater format tests can be run on the host:

```sh
cc -Isrc tests/update_format_test.c src/update_format.c -o /tmp/update-format-test
/tmp/update-format-test build/picocalc_vnc.uf2
```

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
