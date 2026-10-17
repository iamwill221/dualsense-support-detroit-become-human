# Detroit: Become Human — DualSense proxy

Native DualSense (PS5) controller support for the PC version of *Detroit: Become Human*,
with PlayStation button prompts, rumble and lightbar, without Steam Input or DS4Windows.

It is a single drop-in DLL. The game files are not otherwise modified.

## Why

The game talks to Sony controllers through a bundled `libScePad.dll`. That library only
accepts DualShock 4 USB IDs (`054C:05C4`, `054C:09CC`, plus the `0BA0` wireless adapter), so a
DualSense (`054C:0CE6`) is ignored. The DualSense also uses different HID report layouts, so
patching the ID check alone would not work.

Windows additionally exposes the DualSense as a generic DirectInput gamepad, which the game
reads with Xbox prompts. That makes the prompts flip between Xbox and PlayStation icons.

## How it works

`src/libScePad.c` builds a replacement `libScePad.dll` that exports the same 19 `scePad*`
functions as the original:

- **Forwarding.** Every call goes to the original library (renamed `libScePad_orig.dll`), so a
  real DS4 keeps working and takes priority.
- **DualSense stand-in.** When player 1's slot reports no DS4 connected, the proxy opens the
  DualSense over HID and fills the game's `ScePadData` (0x78 bytes; layout taken from the
  original `scePadReadState`) with translated buttons, sticks, triggers, touchpad and motion.
  Motion uses the pad's factory calibration (feature report `0x05`); the orientation
  quaternion is integrated from the gyro with gravity tilt correction.
- **Output.** `scePadSetVibration` / `SetLightBar` are converted to DualSense output reports
  and sent from a worker thread on a separate handle, only when values change. Sending them
  synchronously stalled game frames, which caused lag.
- **DirectInput hiding.** At load, the proxy redirects the game's `DirectInput8Create`
  import and filters the DualSense out of `EnumDevices` / `CreateDevice`, so the game
  only sees it once, as a PlayStation pad.

## Requirements

- Windows x64, the Steam version of the game
- [Zig](https://ziglang.org/) as the C compiler: either `zig` on `PATH`, or `pip install ziglang`
- **Steam Input disabled for the game**: Steam > right-click the game > Properties >
  Controller > *Disable Steam Input*. Otherwise Steam adds a virtual Xbox pad.

## Usage

```powershell
.\scripts\build.ps1                 # -> build\libScePad.dll
.\scripts\install.ps1               # game must be closed
.\scripts\uninstall.ps1             # restores the original DLL
```

`install.ps1` takes `-GameDir` if the game is not in the default Steam library. Re-run it
after a game update or *Verify integrity of game files*, since those restore the original DLL.

Debug build (logs every button / stick change):

```powershell
.\scripts\build.ps1 -DebugLog
```

The proxy always writes a short lifecycle log to `%TEMP%\dualsense_proxy.log`
(load, pad found, DirectInput hooked, errors).

## Test tools

`.\scripts\build.ps1 -Tools` builds these into `build\`. Each takes the full path of a
`libScePad.dll` as its argument. That DLL must sit next to `libScePad_orig.dll`.

| Tool | What it checks |
|---|---|
| `padtest.exe <dll>` | Opens player 1 and prints connection, buttons, sticks and triggers for 3 s |
| `perf.exe <dll>` | Times per-frame ReadState + SetVibration + SetLightBar calls |
| `ditest.exe <dll>` | Lists DirectInput game controllers before and after loading the DLL |
| `motiontest.exe <dll> [seconds]` | Prints orientation, acceleration and gyro for player 1 |

## Status

| Feature | State |
|---|---|
| Buttons, sticks, triggers, D-pad over USB | Working |
| PlayStation prompts, rumble, lightbar | Working |
| Gyro / accelerometer | Factory-calibrated; orientation computed from gyro + gravity. Verified with motiontest (at rest and moving), untested in game |
| Touchpad | Mapped, untested in game |
| Bluetooth | Implemented (report `0x31` + CRC32), untested |
| Unplug / replug mid-game | Retries every 2 s, untested |
| Adaptive triggers, DualSense haptic effects | Not possible, since the game never asks for them |

The DualSense has no classic rumble motors. The game's DS4 rumble is played through its
haptic actuators, so it feels sharper than on a DualShock 4.
