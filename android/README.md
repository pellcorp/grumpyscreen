# GrumpyScreen for Android

An Android app that runs the grumpyscreen UI on a phone or tablet and talks to
a Klipper printer's Moonraker over the network. It is the same C++/LVGL code as
the printer build; only `src/main.cpp` is replaced by
`app/src/main/cpp/grumpy_android.cpp`, which renders into an Android
`SurfaceView` and feeds it touches.

Based on the approach of Guppy Screen's `android` branch
(github.com/ballaswag/guppyscreen), reworked for grumpyscreen.

## Building

Requirements: JDK 17 or newer and the Android SDK. Gradle installs the NDK
(27.2) and CMake (3.22.1) on first use if the SDK licences are accepted.

```
export ANDROID_HOME=~/Android/Sdk
./build.sh android                    # release APK, all ABIs
cd android && ./gradlew assembleDebug  # debug APK
./gradlew assembleDebug -PgrumpyAbis=arm64-v8a      # one ABI, faster
./gradlew assembleDebug -PgrumpySmallScreen=true    # material_46 assets
```

The release APK is signed with `KEYSTORE`, `KEYSTORE_PASSWORD`, `KEY_ALIAS` and
`KEY_PASSWORD` from the environment; without them it is signed with the debug
key so it still installs.

## Using it

* First start searches the network and lists the printers it finds: tap one
  and Connect. It probes every address of the device's /24 on port 7125, asks
  mDNS for `_moonraker._tcp` (Moonraker's optional `[zeroconf]`) and checks
  the device itself, and lists only hosts that answer like Moonraker, by
  hostname. A printer elsewhere can still be typed in (`host`, `host:port` or
  a pasted `http://host:7125/` all work).
* **Settings → Tools → Switch Printer** (or Back) opens the printer list:
  every printer you have connected to is remembered by name, the search runs
  again, and tapping one switches to it (the app restarts itself).
  Saved printers show whether they are online (each is asked for
  Moonraker's `/server/info`); only the current one is connected.
  Long-press a saved printer to forget it. The same dialog can make the app
  the home screen, for a tablet dedicated to a printer. It is drawn with
  grumpyscreen's own `[theme]` colours and Montserrat, the UI's font.
* Settings changed in the UI are saved to the app's
  `files/grumpyscreen-override.cfg`; a `[theme]` there restyles the printer
  dialog too.

* Thumbnails are downloaded from Moonraker into the app cache.
* The screen stays on while the app is in front; grumpyscreen's own screen
  timeout decides when it goes dark.
* The UI renders at the screen's native resolution; grumpyscreen sizes its
  layout from the display, as it does on the printer panels.
* It redraws at the display's refresh rate (asking for the fastest mode the
  panel has, e.g. 90/120Hz) instead of the 33fps lv_conf.h sets for printer
  panels, and only when something on screen changes.

### Compared with the printer (K1) screen

The UI is the same code, so every panel is there except the ones that act
on the printer host itself, which are not compiled into the app. The Tools
tab is always the same five tiles; the `[commands]` in grumpyscreen.cfg are
not used.

| Tools tile | K1 | Android |
|---|---|---|
| Switch Printer | - | the printer list |
| Restart Klipper | Moonraker `machine.services.restart` | same |
| Firmware Restart | Klipper RPC | same |
| Restart GUI | restarts the service | restarts the app |
| Shutdown Host | `/sbin/halt` | Moonraker `machine.shutdown` |
| Create Support ZIP, Switch to Stock, Factory Reset | printer-side scripts | not built |
| Network tab | the printer's wifi | not built: Android manages the device's wifi |

The screen timeout turns the backlight off, as on the printer.

#### Macros

The console's Macros tab is the same as on the printer: one tile per
`[macro "..."]` section, which go in `grumpyscreen-override.cfg`.

Logs go to logcat: `adb logcat -s grumpyscreen`.

## Icons

Phone and tablet screens are two to five times as dense as the 800x480
layout the 64px icon bitmaps (`assets/material`) were drawn for, and
zooming those up only blurs them. The app instead embeds the icons' SVGs
(`assets/material_svg`) and draws each one once, with
[nanosvg](https://github.com/memononen/nanosvg), at the size the screen
needs (64px at 800x480, 102px at 1600x720), so they stay sharp at any
resolution. Editing an SVG there is all it takes to change an icon.

The nav bar's icons (`nav_*.svg`) are drawn at the strip's size instead,
which grows with the screen.

## Measuring

`android/tools/measure.sh [seconds]` prints the running app's RAM (PSS and
native heap) and CPU use, idle and while switching nav tabs, over adb.

## Trying it without a printer

```
python3 tools/fake_moonraker.py --port 7125 --http-thumbnails
```

and point the app at your machine's address (from the Android emulator,
`10.0.2.2`).
