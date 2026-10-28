# plasma-radial-keyboard

Dual-stick radial on-screen keyboard for gamepads, implemented as a native
**Wayland input method** for KWin (Plasma 6, Wayland). KWin launches it as the
Virtual Keyboard.

> Status: Pre-Alpha. Core stick/key/chord logic is unit-tested (`make test`);
> the Wayland/Cairo front end (`src/main.c`) is not covered by tests.

## Build (CachyOS / Arch)

    sudo pacman -S --needed base-devel wayland cairo pango
    make && make test
    sudo make install            # -> /usr/bin + /usr/share/applications/radialkb.desktop

`make install` does not install a configuration file. Without
`~/.config/radialkb/config.ini` the built-in defaults are used (see
[Configuration](#configuration)); copy the sample `config.ini` from the
repository there to customise.

## Enable in Plasma

1. System Settings → Keyboard → Virtual Keyboard → choose
   "Radial Gamepad Keyboard".  (Or: `kwriteconfig6 --file kwinrc --group Wayland
   --key InputMethod /usr/share/applications/radialkb.desktop`, then re-login.)
2. Plasma Bigscreen: the same key in `~/.config/plasma-bigscreen/kwinrc` under `[Wayland]`.

The panel needs a text field that has input-method focus **and** a connected
DualShock/DualSense/Xbox-compatible pad. Without a supported pad it never appears;
unplugging the last pad hides it and releases the grab.

## Invoke modes

Set with `invoke` in `config.ini`, a command-line flag, or an environment variable
(the latter two win over the config file, also after a config hot-reload).

| Mode | Flag | Behaviour |
|---|---|---|
| **smart** (default) | `--smart` | Opens automatically when a text field is focused and a pad is connected, regardless of Plasma's setting. If you hide it with the **hide** button it stays hidden until the focus moves to another field or you press the **show** chord. |
| **manual** | `--manual`, `RADIALKB_MANUAL=1` | Never auto-shows; only the **show** chord opens it (text field + pad still required). |
| **auto** | `--auto` | Follows Plasma's `kwinrc` `[Wayland]` `VirtualKeyboardMode` (below). |

In **auto** mode:

| Plasma "Show Virtual Keyboard" | `VirtualKeyboardMode` | What this keyboard does |
|---|---|---|
| Never | `0` | No auto-show; the show chord can open it. |
| With Touch and Tablet | `1` | No auto-show; the show chord can open it. |
| With Touch, Tablet, and Mouse | `2` | Auto-show on a focused text field (pad still required). |

Legacy `VirtualKeyboardEnabled=false` is treated as Never; if neither key is
present the mode defaults to `1`. `smart` and `manual` ignore this setting.

## Show / hide

* **Show chord** (`show`, default `select+start`): opens the panel while a text
  field is focused and a pad is connected. Accepts one button (`guide`) or two
  joined with `+` (`lb+rb`, `l3+r3`). The order in which the two buttons are
  pressed does not matter.
* **Chord buttons act on release.** A chord button pressed on its own is
  delivered as an ordinary tap when released, so with the defaults a lone
  **Select** still hides the panel and a lone **Start** still types Enter, while
  Select+Start only opens it.
* **Hide** (`hide`, default `select`): hides the panel until the next focus
  change or the show chord. The panel also hides when the text field loses
  focus, Plasma/the mode says so, or the last pad is unplugged.

## Input grab (games)

While the panel is visible (and has presented its first frame), pads are grabbed
with `EVIOCGRAB` so sticks/buttons do not leak into the game. Hiding **always**
ungrabs; FDs stay open so hotplug and the show chord keep working.

If another client already holds the device (for example Steam Input, error
`EBUSY`), the grab is skipped, one warning is logged and input is shared; the
grab is retried the next time the panel is shown.

`--no-grab` / `RADIALKB_NO_GRAB=1` never grabs.

## Controls

Defaults; everything except the sticks, RT and the D-pad can be remapped in
`[controls]`.

| Input | Action |
|---|---|
| Left stick | pick one of 8 groups (stays selected when released) |
| Right stick | flick in one of 8 directions, **return to centre to type** |
| LT (hold) | Shift |
| B / LB | Backspace (hold = repeat) |
| X / Y / RB | Space (hold = repeat) |
| A / Start | Enter |
| RT | Enter (fixed) |
| D-pad | Arrow keys (hold = repeat, fixed) |
| L3 | symbols/digits layer (toggle) |
| R3 | next language |
| Select | hide panel (see above) |
| Select+Start | show panel (customizable) |

If two buttons are bound to the same action, auto-repeat continues as long as
either one is held. The hint pills at the bottom of the panel always show the
**first** button bound to each action, so they follow your remapping.

A small red dot in the top-right corner of the panel means `config.ini` has an
error; the details are printed to stderr (`journalctl --user -f | grep radialkb`)
and the affected value falls back to its default.

## Configuration

File: `$XDG_CONFIG_HOME/radialkb/config.ini` (default `~/.config/radialkb/config.ini`).
It is hot-reloaded when it changes, as are `kwinrc`, `kdeglobals` and the active
layout file; other files in `~/.config` are ignored. Only whole-line comments
(`#` or `;` at the start of a line) are supported, so values such as
`#3daee9` are safe.

Without the file everything below uses the defaults shown.

    [general]
    invoke = smart            # smart | manual | auto
    show = select+start       # alias: toggle; one button or "a+b"
    layout = layouts.conf     # optional; relative to ~/.config/radialkb/ or absolute

    [controls]                # comma-separated lists, up to 6 buttons per action
    backspace = b, lb
    space = x, y, rb
    enter = a, start
    shift = lt
    symbols = l3
    layout_switch = r3
    hide = select

    [ui]
    scale = 1.0               # 0.5 .. 3.0, scales the whole panel
    font_scale = 1.0          # 0.5 .. 3.0
    font_family = Noto Sans
    width = 0                 # 600 .. 3840 px; omit/0 = 1100 * scale
    height = 0                # 300 .. 2160 px; omit/0 = 500 * scale

    [theme]
    background_style = acrylic   # acrylic | solid | translucent
    background_opacity = 0.85    # 0.1 .. 1.0 (ignored for solid)
    corner_radius = 18           # 0 .. 48
    accent_color = auto          # auto, or #rrggbb / #rrggbbaa / r,g,b

Button names: `a b x y`, `lb rb` (`l1 r1`), `lt rt` (`l2 r2`), `l3 r3`
(`thumbl thumbr`), `select` (`share back`), `start` (`options menu`),
`guide` (`mode ps`), plus the PlayStation/compass aliases `cross circle square
triangle south east west north`. Unknown names are reported as a config error.

`acrylic` uses the compositor's background blur (`ext_background_effect_v1` or
`org_kde_kwin_blur`) when available; otherwise it is just translucent.

Command line: `--no-grab`, `--manual | --smart | --auto`, `--layout FILE`.
Environment: `RADIALKB_NO_GRAB=1`, `RADIALKB_MANUAL=1`.

## Theme

Colours follow the Plasma colour scheme in `~/.config/kdeglobals`
(`Colors:Window`, `Colors:Button`, `Colors:Selection`, `General` `AccentColor`),
including light/dark switches. `accent_color` in `config.ini` overrides the
system accent. Without a readable `kdeglobals` a built-in Breeze Dark palette is used.

## Layouts

Built in: `en`, `ru`. Override with `~/.config/radialkb/layouts.conf`
(or `layout =` in `config.ini`, or `--layout FILE`); up to 8 languages:

    [en]
    lower: a b c d | e f g h | ...      # 8 groups separated by |
    upper: A B C D | ...                # optional: generated from lower when omitted
    sym:   1 2 3 4 | 5 6 7 8 | ...      # optional: copied from the first layout
    [de]
    lower: ...

Tokens within a group: first 4 → N,E,S,W; next 4 → NE,SE,SW,NW.
`_` = empty slot, `<bar>` = `|`, `<us>` = `_`. A token is at most 15 bytes.
The symbols layer takes priority over Shift.

## If it does not show up

* Compare `/usr/share/applications/radialkb.desktop` with
  `/usr/share/applications/org.kde.plasma.keyboard.desktop`:
  the `X-KDE-Wayland-*` keys must match what your KWin expects.
* Run `journalctl --user -f | grep -i radialkb` while focusing a text field.
* "no DualShock/Xbox-compatible pad": need read access to `/dev/input/event*`
  (logind/uaccess, or the `input` group). Hotplug is watched via inotify.
* Mode `manual`, or `auto` with Plasma set to anything but "With Touch, Tablet,
  and Mouse", will not auto-show: press the show chord (default Select+Start)
  while the text field is focused.
* "grabbed by another client": Steam Input or an emulator owns the pad; the
  keyboard still works but game input is not blocked while it is visible.
* The protocol XML in `protocol/` is a restatement of input-method-unstable-v1;
  if KWin disagrees, swap in KWin's own copy.

## Development

    make test        # unit tests for layouts, sticks, binds, chord, visibility logic

The Wayland-independent logic lives in `src/kb_core.h` and is the part covered by
`tests/test_core.c`; rendering, protocol handling and config loading are in
`src/main.c`.
