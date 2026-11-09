# Alpakka Firmware (TheLoomLabs fork)

*Custom firmware for the Alpakka gyro controller, extended for PC gaming.*

This is a fork of the official
[Input Labs firmware](https://github.com/inputlabs/alpakka_firmware). It adds:

- **Gyro acceleration**: per-profile curves (Linear, Power, Natural, Sigmoid)
  for precise aim plus easy 180 / 360 turns.
- **Polling rate**: 250 / 500 / 1000 Hz over USB (wireless stays at 250 Hz).
- **Aim-down-sights modifier**: separate sensitivity and acceleration while a
  chosen button is held.
- **Stabilization**: smoothing and tightening for slow movements.
- **Player space** gyro (experimental).
- **Macros**: up to 14 steps (tap, hold, release, wait), play once / repeat
  while held / toggle repeat.
- Bounds checks on profile and section indexes sent by the host.

The new settings are configured with our app fork,
[TheLoomLabs/ctrl-app](https://github.com/TheLoomLabs/ctrl-app). Existing
profiles keep working: every new setting defaults to off.

## Install

1. Download the `.uf2` file for your controller from
   [Releases](https://github.com/TheLoomLabs/alpakka_firmware/releases).
2. Put the controller in bootsel mode (app: **Logs → Restart in bootsel mode**).
3. Copy the `.uf2` file to the `RPI-RP2` drive that appears. The controller
   restarts with the new firmware.

To go back to stock, flash the official firmware from the
[Input Labs releases](https://github.com/inputlabs/alpakka_firmware/releases)
the same way.

Not affiliated with or supported by Input Labs; please do not report issues with
this fork to them.

*The rest of this document is the official developer documentation; it applies
to this fork unchanged.*

## Project links
- [Alpakka Manual](https://inputlabs.io/devices/alpakka/manual).
- [Alpakka Firmware](https://github.com/inputlabs/alpakka_firmware) (official).
- [Alpakka Firmware, TheLoomLabs fork](https://github.com/TheLoomLabs/alpakka_firmware). _(you are here)_
- [Alpakka PCB](https://github.com/inputlabs/alpakka_pcb).
- [Alpakka 3D-print](https://github.com/inputlabs/alpakka_case).
- [Input Labs Roadmap](https://github.com/orgs/inputlabs/projects/2/views/2).

## Supported developer operative systems
- GNU/Linux (and MacOS) - See [Development in Linux](https://inputlabs.io/devices/alpakka/manual/dev_unix).
- Windows - See [Development in Windows](https://inputlabs.io/devices/alpakka/manual/dev_windows).

## System dependencies
With `apt`, `rpm`, `pacman`, `brew`, or the equivalent package manager of your system, install:
- **gcc**
- **git**
- **cmake**

## Project dependencies
- `make install`: Download and configure dependencies automatically.

## Compilation targets
- `alpakka_v0`: Alpakka v0.x.x with Raspberry Pico.
- `alpakka_v1`: Alpakka v1.x.x with Marmota module.
- `dongle`: Wireless dongle.
- `llama`: ESP wireless module flasher (ESP-LLAMA repository).

Example usage:
```
DEVICE=alpakka_v1 make
```

## Development commands
- `DEVICE=<device> make`: Build compilation environment and build executables.
- `make rebuild`: Build executables again using cache (faster).
- `make load`: Load built .uf2 file into the Pico (requires bootsel mode or active session).
- `make reload`: Do both `rebuild` and `load` commands (for dev convenience).
- `make clean`: Delete previous build files.
- `make session`: Connect to UART serial stdio, and display controller log.

While having an active session:
- `make restart`: Restart the controller.
- `make bootsel`: Put the controller in bootsel mode.
- `make calibrate`: Calibrate thumbstick and IMUs.
- `make format`: Format NVM sector and reset to initial values.
- `make test`: Start a semi-manual testing procedure for the buttons and axis.

## Devkit button
- Single press: Restart the controller.
- Double press: Put the controller in bootsel mode.

Read the [Alpakka developer manual](https://inputlabs.io/devices/alpakka/manual/dev) for details about the **devkit** and more.
