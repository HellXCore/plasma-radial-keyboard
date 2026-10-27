# plasma-radial-keyboard

Dual-stick radial on-screen keyboard for gamepads, implemented as a native
**Wayland input method** for KWin (Plasma 6, Wayland). KWin launches it as the
Virtual Keyboard.

> Status: first working draft. Core stick/key logic is unit-tested (`make test`).

## Build (CachyOS / Arch)

    sudo pacman -S --needed base-devel wayland cairo pango
    make && make test
    sudo make install            # -> /usr/bin + /usr/share/applications/radialkb.desktop

## Enable in Plasma

1. System Settings → Keyboard → Virtual Keyboard → choose
   "Radial Gamepad Keyboard".  (Or: `kwriteconfig6 --file kwinrc --group Wayland
   --key InputMethod /usr/share/applications/radialkb.desktop`, then re-login.)
2. Plasma Bigscreen: the same key in `~/.config/plasma-bigscreen/kwinrc` under `[Wayland]`.

### How Plasma's "Show Virtual Keyboard" is used

Plasma 6.8 has `kwinrc` `[Wayland]` `VirtualKeyboardMode`:

| Plasma | Value | What this keyboard does |
|---|---|---|
| Never | `0` | Does **not** auto-show. Chord (default Select+Start) can still open it while a text field is focused **and** a pad is connected. |
| With Touch and Tablet | `1` | Auto-show when a text field is focused **and** a DualShock/Xbox-compatible pad is plugged in (Plasma has no "on gamepad" option, this is the extra trigger). |
| With Touch, Tablet, and Mouse | `2` | Same as above: still requires a live pad, so a mouse-only session stays clean. |

Legacy `VirtualKeyboardEnabled=false` is treated as Never.

The panel never appears without a supported gamepad. Unplug → hide and release the grab.

## Invoke modes

* **Auto** (default): follows the table above.
* **Manual**: never auto-shows; only the gamepad chord toggles the panel (text field + pad still required).

Set in `~/.config/radialkb/config`:

    [general]
    invoke=auto          # or manual
    toggle=select+start  # select, start, l3, r3, mode, lb, rb

Or flags: `--auto` / `--manual`, env `RADIALKB_MANUAL=1`.

## Input grab (games)

While the **panel is visible**, pads are grabbed with `EVIOCGRAB` so sticks/buttons do not leak into the game.

Hide (Select, chord, Plasma Never, last pad unplugged, or text field lost) **always ungrabs**. FDs stay open so hotplug and the toggle chord still work, but SDL/raw evdev in games see the controller again.

`--no-grab` / `RADIALKB_NO_GRAB=1` never grabs.

## Controls

| Input | Action |
|---|---|
| Left stick | pick one of 8 groups (stays selected when released) |
| Right stick | flick in one of 8 directions, **return to centre to type** |
| LT (hold) | Shift |
| LB / B | Backspace (hold = repeat) |
| RB / X / Y | Space |
| RT / A / Start | Enter |
| D-pad | Arrow keys |
| L3 | symbols/digits layer |
| R3 | next language |
| Select | hide panel until next text field or toggle chord |
| Select+Start | toggle panel (customizable) |

## Theme

Colours are read from `~/.config/kdeglobals` (`Colors:Window` / `Colors:Selection`) and follow Plasma light/dark when you change the global scheme (file is watched).

## Layouts

Built in: `en`, `ru`. Override with `~/.config/radialkb/layouts.conf`
(or `--layout FILE`):

    [en]
    lower: a b c d | e f g h | ...      # 8 groups separated by |
    sym:   1 2 3 4 | 5 6 7 8 | ...
    [de]
    lower: ...                          # upper: generated; sym: copied from first layout

Tokens within a group: first 4 -> N,E,S,W; next 4 -> NE,SE,SW,NW.
`_` = empty slot, `<bar>` = `|`, `<us>` = `_`.

## If it does not show up

* Compare `/usr/share/applications/radialkb.desktop` with
  `/usr/share/applications/org.kde.plasma.keyboard.desktop`:
  the `X-KDE-Wayland-*` keys must match what your KWin expects.
* Run `journalctl --user -f | grep -i radialkb` while focusing a text field.
* "no DualShock/Xbox-compatible pad": need read access to `/dev/input/event*`
  (logind/uaccess, or the `input` group). Hotplug is watched via inotify.
* The protocol XML in `protocol/` is a restatement of input-method-unstable-v1;
  if KWin disagrees, swap in KWin's own copy.
