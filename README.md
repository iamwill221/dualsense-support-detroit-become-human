# DualSense for Detroit: Become Human (PC)

Play *Detroit: Become Human* on PC with a **PS5 DualSense** controller: PlayStation
buttons on screen, rumble, lightbar and motion. You don't need Steam Input or DS4Windows.

## For players

### Install

1. Download `Detroit-DualSense-<version>.zip` and unzip it anywhere.
2. Double-click **`Install.bat`**. It finds the game by itself. If Windows asks for
   permission, click **Yes**.
3. In Steam, right-click the game > **Properties** > **Controller** and choose
   **Disable Steam Input**.
4. Connect the DualSense (USB cable or Bluetooth) and start the game.

If a game update removes DualSense support, run `Install.bat` again.

### Uninstall

Double-click **`Uninstall.bat`**, or use Steam > *Verify integrity of game files*.

### Troubleshooting

| Problem | Fix |
|---|---|
| Xbox buttons on screen, or they flip between Xbox and PlayStation | Steam Input is still on (step 3) |
| Controller does nothing | Make sure it's connected before starting the game; try a USB cable |
| "Windows protected your PC" | Click **More info** > **Run anyway** |
| The installer can't find the game | Paste the game folder when it asks. To find it: Steam > right-click the game > *Manage* > *Browse local files* |
| Something else | Open `%TEMP%\dualsense_proxy.log` and include it in your bug report |

### What works

| Feature | Status |
|---|---|
| Buttons, sticks, triggers, D-pad | ✅ |
| PlayStation button prompts | ✅ |
| Rumble, lightbar | ✅ |
| Motion (gyro / accelerometer) | ✅ Tested outside the game |
| Touchpad | ✅ |
| USB and Bluetooth | ✅ |
| Adaptive triggers, DualSense haptic effects | ❌ The game never asks for them |

The DualSense has no classic rumble motors. It plays the game's rumble through its
haptic actuators, so vibration feels sharper than on a PS4 controller.

---

## For developers

### Why this is needed

The game reads Sony controllers through a bundled `libScePad.dll`. That library only
accepts DualShock 4 USB IDs (`054C:05C4`, `054C:09CC`, and the `0BA0` adapter), so a DualSense
(`054C:0CE6`) is ignored. The DualSense also sends its data in a different format, so
patching the ID check alone would not be enough.

Windows also exposes the DualSense as a generic DirectInput gamepad, which the game reads
with Xbox prompts. That is why the prompts flip between Xbox and PlayStation icons.

### How it works

`src/libScePad.c` builds a replacement `libScePad.dll` that exports the same 19 `scePad*`
functions:

- **Forwarding.** Every call goes to the original library (renamed `libScePad_orig.dll`), so a
  real DS4 keeps working and takes priority.
- **DualSense stand-in.** When player 1 has no DS4 connected, the proxy opens the DualSense
  over HID. It fills the game's `ScePadData` (0x78 bytes; layout taken from the original
  `scePadReadState`) with translated buttons, sticks, triggers, touchpad and motion.
- **Motion.** The proxy uses the pad's factory calibration (feature report `0x05`), then
  calculates the orientation quaternion from the gyro, with gravity tilt correction.
- **Output.** `scePadSetVibration` / `SetLightBar` become DualSense output reports. They are
  sent from a worker thread on a separate handle, and only when the values change.
  Sending them synchronously stalled game frames.
- **DirectInput hiding.** At load, the proxy redirects the game's `DirectInput8Create` import
  and filters the DualSense out of `EnumDevices` / `CreateDevice`.

### Build

Requires [Zig](https://ziglang.org/) as the C compiler: either `zig` on `PATH`, or `pip install ziglang`.

```powershell
.\scripts\build.ps1                    # -> build\libScePad.dll
.\scripts\build.ps1 -DebugLog          # also log every button / stick change
.\scripts\build.ps1 -Tools             # also build the test tools
.\scripts\package.ps1 -Version 1.0.0   # -> dist\Detroit-DualSense-1.0.0.zip for players
.\scripts\install.ps1                  # install from build\ (auto-detects the game, or -GameDir)
.\scripts\uninstall.ps1
```

The proxy always writes a short log to `%TEMP%\dualsense_proxy.log`.

### Test tools

Each tool takes the full path of a `libScePad.dll`. That DLL must sit next to
`libScePad_orig.dll`.

| Tool | What it checks |
|---|---|
| `padtest.exe <dll>` | Connection, buttons, sticks and triggers for 3 s |
| `motiontest.exe <dll> [seconds]` | Orientation, acceleration and gyro |
| `perf.exe <dll>` | Time per frame of the ReadState + SetVibration + SetLightBar calls |
| `ditest.exe <dll>` | DirectInput controllers before and after loading the DLL |

### Layout

```
src/libScePad.c     the proxy
scripts/            build, package, install, uninstall (common.ps1 = shared helpers)
package/            files shipped only in the player zip (Install.bat, Uninstall.bat, README.txt)
tools/              test programs
```
