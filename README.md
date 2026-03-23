# rp2040-fastboot-rebooter

> ## 严正声明 / STRONG WARNING
>
> **本项目基于 GNU GPL v3 发布。商业使用时必须公开对应源代码，保留版权与许可证声明并继续以 GPL 方式发布衍生版本。**
>
> **分发、销售、集成或修改本项目时，你必须公开对应源代码，保留版权与许可证声明，并继续以 GPL 方式发布衍生版本。**
>
> **如果你不愿遵守这些义务，请不要使用、分发或销售本项目。**
>
> **如发现违反 GNU GPL v3 的行为，仓库所有者保留追责及采取法律行动的权利。**

---

## What is this?

A **standalone fastboot host** on a Raspberry Pi Pico (RP2040). Flash it once on your PC, then use it without a PC — just power the Pico and connect an Android device to run a pre-programmed sequence of fastboot commands automatically.

**Use cases:** Device recovery, bootloader unlocking, automated OEM commands, unattended reboots, no PC-side tools required.

---

## Quick Start

Choose one path below:

### Option 1: Use pre-built firmware (5 minutes)

- Download `.uf2` from [Releases](../../releases)
- Flash to Pico via BOOTSEL drag-and-drop
- Power Pico + connect Android device → runs automatically

### Option 2: Build custom firmware (10 minutes)

- Edit `FASTBOOTCMDS.txt` with your commands
- Build with `cmake` (or VS Code task)
- Flash the same way
- Power Pico + connect Android device → runs your commands

---

## Usage — pre-built firmware

### Flash the Pico (PC required)

1. Go to the [Releases](../../releases) page and download the latest release archive.
2. Extract the `.uf2` file from the archive.
3. Hold the **BOOTSEL** button on the Pico, plug it into your PC via USB, then release the button. It will appear as a USB mass-storage drive named `RPI-RP2`.
4. Copy `fastboot_rebooter.uf2` onto the drive. The Pico will reboot automatically.

### Operate the Pico (no PC required)

5. Disconnect the Pico from your PC.
6. Power the Pico using any USB power source (5V, e.g., a USB charger or power bank).
7. Put your Android device in fastboot mode.
8. Connect the Android device to the Pico's **USB host port** (not the power port).
9. The Pico will automatically detect the device and send all configured fastboot commands in sequence with 2-second delays between each command.

> **Note:** The pre-built firmware contains the default `FASTBOOTCMDS.txt` baked in. To use custom commands you must compile the firmware yourself (see below).

---

## Usage — compile yourself

### Prerequisites (for building)

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) v2.0.0 or later (with submodules)
- CMake ≥ 3.13
- Ninja
- `arm-none-eabi-gcc` toolchain

### Build steps (PC required)

1. Clone this repository.

2. Edit `FASTBOOTCMDS.txt` with the fastboot commands you want to send.

3. Configure and build:

   ```sh
   export PICO_SDK_PATH=/path/to/pico-sdk
   cmake -S . -B build -G Ninja
   cmake --build build
   ```

   On Windows with the [Raspberry Pi Pico VS Code Extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico), you can use the **Compile Project** task directly.

4. The build produces `build/fastboot_rebooter.uf2`.

### Flash the Pico (PC required)

5. Flash it to the Pico using the BOOTSEL drag-and-drop method:
   - Hold **BOOTSEL**, plug Pico into your PC, release **BOOTSEL**
   - Copy `build/fastboot_rebooter.uf2` to the `RPI-RP2` drive
   - The Pico will reboot

   Alternatively, use `picotool` for automated flashing:

   ```sh
   picotool load build/fastboot_rebooter.uf2 -fx
   ```

### Operate the Pico (no PC required)

6. Follow steps 5-9 from the "**Operate the Pico**" section in pre-built firmware above.

---

## Configuration — FASTBOOTCMDS.txt

`FASTBOOTCMDS.txt` defines the fastboot commands to send. Used during build; baked into the firmware.

**Syntax:**

- One command per line, plain text
- Sent top-to-bottom in order
- 2-second delay between commands (adjust `CMD_DELAY_MS` in code before building)
- Blank lines ignored
- No comment syntax — lines starting with `#` are sent as-is
- Max 32 commands, each < 256 characters

**Example:**

```
oem set-gpu-preemption 0 androidboot.selinux=permissive
continue
```

---

## Keywords / 关键词

**English:** Android, fastboot, RP2040, Raspberry Pi Pico, USB host, fastboot automation, Android device recovery, bootloader, OEM fastboot commands, embedded tool, firmware utility

**中文：** 安卓, Android, fastboot, RP2040, 树莓派 Pico, USB 主机, fastboot 自动化, 安卓设备恢复, bootloader, 引导加载器, OEM fastboot 命令, 嵌入式工具, 固件工具
