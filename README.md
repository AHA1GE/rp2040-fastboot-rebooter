# rp2040-fastboot-rebooter

Turn a Raspberry Pi Pico (RP2040) into a USB host that automatically sends a sequence of fastboot commands to an Android device. Plug the Pico between your PC and the Android device in fastboot mode, and it will fire each command in order — no PC-side `fastboot` tool required. Useful for unattended operations such as rebooting out of fastboot, toggling device options, or running any fixed fastboot command sequence.

---

## FASTBOOTCMDS.txt

`FASTBOOTCMDS.txt` defines the commands that will be sent. Edit this file before building the firmware (or use the pre-built binary that ships with a default command set).

**Syntax rules:**

- One fastboot command per line, plain text.
- Lines are sent to the device in order, top to bottom.
- A 2-second delay is inserted between consecutive commands (configurable via `CMD_DELAY_MS` in `fastboot_rebooter.c` before building).
- Blank lines are ignored.
- Lines starting with `#` are **not** specially treated — do not use comments.
- Maximum 32 commands; each command must be shorter than 256 characters.

**Example:**

```
oem set-gpu-preemption 0 androidboot.selinux=permissive
continue
```

---

## Usage — pre-built firmware

1. Go to the [Releases](../../releases) page and download the latest release archive.
2. Extract the `.uf2` file from the archive.
3. Hold the **BOOTSEL** button on the Pico, plug it into your PC via USB, then release the button. It will appear as a USB mass-storage drive named `RPI-RP2`.
4. Copy `fastboot_rebooter.uf2` onto the drive. The Pico will reboot and start running the firmware automatically.
5. Wire the Pico's USB host port to the Android device and put the device in fastboot mode.

> **Note:** The pre-built firmware contains the default `FASTBOOTCMDS.txt` baked in. To use custom commands you must compile the firmware yourself (see below).

---

## Usage — compile yourself

### Prerequisites

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) v2.0.0 or later (with submodules)
- CMake ≥ 3.13
- Ninja
- `arm-none-eabi-gcc` toolchain

### Steps

1. Clone this repository.

2. Edit `FASTBOOTCMDS.txt` with the commands you want to send.

3. Configure and build:

   ```sh
   export PICO_SDK_PATH=/path/to/pico-sdk
   cmake -S . -B build -G Ninja
   cmake --build build
   ```

   On Windows with the [Raspberry Pi Pico VS Code Extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico), you can use the **Compile Project** task directly.

4. The build produces `build/fastboot_rebooter.uf2`.

5. Flash it to the Pico using the same BOOTSEL drag-and-drop method described above, or use `picotool`:

   ```sh
   picotool load build/fastboot_rebooter.uf2 -fx
   ```
