#  Plasma Radial Keyboard (RadialKB)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform: Wayland](https://img.shields.io/badge/Platform-Wayland%20%2F%20KDE%20Plasma-orange.svg)]()
[![Language: C](https://img.shields.io/badge/Language-C99-brightgreen.svg)]()

**RadialKB** is a lightweight, dual-stick radial on-screen keyboard designed for gamepads on **KDE Plasma (Wayland)**.

Inspired by the Daisywheel input system (popularized by Steam Controller and handheld gaming interfaces), it enables high-speed, intuitive text input using only two analog sticks and a controller's buttons—without needing touchscreens or mouse emulation.

Written in pure C using native Wayland protocols, Cairo, and Pango. It has zero heavy toolkit dependencies (no Qt or GTK runtime overhead) while integrating with KDE Breeze themes and blur effects.

---

## Features

- **Dual-Stick Input:**
  - **Left Stick:** Selects a character group (8 radial sectors).
  - **Right Stick:** Flick in one of 8 directions to choose and commit a character.
- **KDE Plasma Integration:**
  - Implements `zwp_input_method_v1` and `zwp_input_panel_v1`.
  - Integrates into KDE Plasma as a registered virtual keyboard.
- **Breeze Theme Integration:**
  - Dynamically detects KDE color schemes (`kdeglobals`), light/dark modes, and user accent colors.
  - Supports background acrylic blur through `ext-background-effect-v1` and `org_kde_kwin_blur`.
- **Exclusive Gamepad Grab (`evdev`):**
  - Uses `EVIOCGRAB` when active so inputs do not leak into background windows or games.
  - Fails gracefully if another application (like Steam) currently holds an exclusive grab.
- **Hot-Plug & Hot-Reload:**
  - Gamepad connect/disconnect handling via inotify on `/dev/input`.
  - Live reload of configuration and layout files without restarting the daemon.
- **Multi-Layout Support:**
  - Ships with English and Russian layouts.
  - Custom layouts can be defined with a simple text-based format.
- **Minimal Footprint:**
  - Pure C codebase, low memory and CPU footprint, sub-frame input response.

---

## Default Controls

| Action | Xbox Controller | PlayStation Controller |
| :--- | :--- | :--- |
| **Select Group** | Left Stick | Left Stick |
| **Type Character** | Right Stick (Flick) | Right Stick (Flick) |
| **Backspace** | `B` or `LB` | `Circle` or `L1` |
| **Space** | `X`, `Y` or `RB` | `Square`, `Triangle` or `R1` |
| **Enter** | `A` or `Start` / `RT` | `Cross` or `Options` / `R2` |
| **Shift** | `LT` (Hold) | `L2` (Hold) |
| **Symbols Layer** | `L3` (Stick Click) | `L3` (Stick Click) |
| **Switch Layout** | `R3` (Stick Click) | `R3` (Stick Click) |
| **Hide Keyboard** | `Select` / `Back` | `Share` |
| **Show / Toggle Chord** | `Select` + `Start` | `Share` + `Options` |
| **Navigation** | D-Pad | D-Pad |

---

## Requirements

### Build Dependencies

- C compiler (`gcc` or `clang`)
- `make`
- `pkg-config`
- `wayland-client` and `wayland-scanner`
- `cairo`
- `pango` / `pangocairo`

#### Debian / Ubuntu / KDE Neon:
```bash
sudo apt install build-essential pkg-config libwayland-dev wayland-protocols libcairo2-dev libpango1.0-dev
```

#### Arch Linux / Manjaro:
```bash
sudo pacman -S base-devel wayland wayland-protocols cairo pango
```

#### Fedora:
```bash
sudo pacman -S base-devel wayland wayland-protocols cairo pango
```

### Runtime Permissions:
```bash
sudo usermod -a -G input $USER
```
(Log out and log back in for changes to apply).

## Building and Installation:

1. Clone the repository:
```bash
git clone https://github.com/your-username/plasma-radial-keyboard.git
cd plasma-radial-keyboard
```
2. Compile and tests:
```bash
make && make test
```
3. Compile and tests:
```bash
sudo make install
```

## Setup in KDE Plasma:

1. Open System Settings.
2. Go to Input & Output → Keyboard → Virtual Keyboard.
3. Choose Radial Gamepad Keyboard.
4. Configure activation behavior:
* Use "With Touch, Tablet, and Mouse" for automatic activation, or press the Select + Start chord on your gamepad whenever a text input field is focused.

## Configuration:

Settings are stored in:
* Main config: ~/.config/radialkb/config.ini
* Custom layouts: ~/.config/radialkb/layouts.conf

#### Example custom configuration *config.ini*:
```ini
[general]
# Options:
# - smart:  opens automatically on focus if a gamepad is connected
# - manual: opens only via shortcut / chord
# - auto:   follows KWin's VirtualKeyboardMode setting
invoke = smart

# Toggle combo (single button or combination)
show = select+start

# Layout file name (inside ~/.config/radialkb/) or absolute path
layout = layouts.conf

[controls]
backspace = b, lb
space = x, y, rb
enter = a, start
shift = lt
symbols = l3
layout_switch = r3
hide = select

[ui]
scale = 1.0
font_scale = 1.0
font_family = Noto Sans
# Custom dimensions (0 = auto-calculated based on scale)
# width = 1100
# height = 500

[theme]
# solid, translucent, or acrylic
background_style = acrylic
background_opacity = 0.85
corner_radius = 18.0
# accent_color = #3daee9 # or "auto" to pull from KDE
```

#### Example custom layouts *layouts.conf*:
```ini
[en]
lower: a b c d | e f g h | i j k l | m n o p | q r s t | u v w x | y z @ # | . , ? ! ' - : ;
sym: 1 2 3 4 | 5 6 7 8 | 9 0 - + | ( ) [ ] { } < > | @ # $ % ^ & * ~ | = / \ <bar> <us> ` " ' | . , ? ! : ; … € | € £ ¥ § © ° • №
```
Special escape tokens:
* ```<bar>```: pipe character (|)
* ```<us>```: underscore (_)
* ```_```: empty slot

## Command-Line Arguments:

Usage: plasma-radial-keyboard [OPTIONS]
```txt
Options:
  --no-grab            Do not grab input devices exclusively with EVIOCGRAB
  --smart              Force smart invoke mode
  --manual             Force manual invocation only (via chord)
  --auto               Follow KWin virtual keyboard settings
  --layout FILE        Use specified layout file instead of default
```

## License:

Copyright (C) 2026 [HellXCore](https://github.com/HellXCore)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the [GNU General Public License
along with this program](LICENSE).  If not, see <https://www.gnu.org/licenses/>.