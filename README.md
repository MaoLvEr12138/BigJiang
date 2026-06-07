# BigJiang (大酱) - Low-Cost DIY Digital FPV System

**English** | [中文](README_CN.md)

## Overview

BigJiang is a low-cost DIY digital FPV (First Person View) system based on **Luckfox Pico Plus (RV1103)** + **RTL8812AU**. Both the ground side and air side use identical hardware, paired with [wfb-ng](https://github.com/svpcom/wfb-ng) to achieve low-latency digital video transmission and bidirectional MAVLink telemetry.

> This project is developed on top of the Luckfox Pico SDK (a modified version of the official Rockchip SDK).

## Features

- **Low-latency H.264 digital video**: Based on the RV1103 hardware encoder at 320×240@24fps, end-to-end total latency is approximately **100~150ms**. Uses Intra Refresh row-based refresh strategy, eliminating the instantaneous bitrate spikes of traditional I-frames.
- **Adjustable bitrate / resolution**: Resolution and encoding bitrate can be freely adjusted. At 240p, the actual bitrate can be compressed to **5,000~20,000 Bytes/s**.
- **Custom RTP/UDP video streaming**: Custom RTP FU-A fragmentation encapsulation, combined with intra-frame NALU time-slice pacing transmission, completely eliminates instantaneous network card bursts.
- **WFB-NG wireless link**: RTL8812AU network card operating in Monitor mode; wfb-ng provides highly robust FEC forward error correction wireless transmission.
- **MAVLink bidirectional telemetry**: Video stream, flight controller downlink telemetry, and ground-side uplink control operate as three fully independent channels with no mutual interference.
- **Phone direct-connect ground station**: The ground side connects to an Android phone via USB tethering. Video and telemetry can be received directly in **QGroundControl**; the video stream is forwarded to port 14550 by the ground-side script.
- **SOCAT serial passthrough**: The air side automatically bridges the flight controller's serial MAVLink data to the wfb link, requiring no additional flight controller configuration.

## System Architecture

```
Air Side
┌─────────────────────────────────────────────────────────┐
│  Camera ──→ VI ──→ VPSS (Scaling) ──→ VENC (H.264)    │
│                                          │               │
│                                   RTP FU-A / UDP        │
│                                     127.0.0.1:5600      │
│                                          │               │
│              wfb_tx ──────────→ RTL8812AU (Monitor Mode) │
│              wfb_rx ←────────── RTL8812AU (Monitor Mode) │
│                                          │               │
│    FC /dev/ttyS4 ←──→ socat ←──→ UDP:14550/14551       │
│                            (MAVLink Bidir Passthrough)   │
└─────────────────────────────────────────────────────────┘
                           │
                    ~~~ 5.8GHz ~~~
                           │
Ground Side
┌─────────────────────────────────────────────────────────┐
│      RTL8812AU (Monitor) ──→ wfb_rx ──→ UDP:5600 Video  │
│      RTL8812AU (Monitor) ←── wfb_tx ←── UDP:14551 Uplink │
│      RTL8812AU (Monitor) ──→ wfb_rx ──→ UDP:14550 Dnlink │
│                                          │               │
│                    socat bidirectional UDP forwarding    │
│                                          │               │
│                    usb0 (Phone USB Tethering)            │
│                                          │               │
│             Android Phone APP (QGroundControl / FPV)     │
└─────────────────────────────────────────────────────────┘
```

## Latency Performance

End-to-end latency was measured by **simultaneously recording the original stopwatch and the stopwatch displayed in the FPV feed**: within the same frame, compare the phone's actual stopwatch reading against the stopwatch reading shown in the video stream — the difference is the total end-to-end latency.

> The complete test video is available in the repository root: **[Delayed_test.mp4](Delayed_test.mp4)**.

Test results show that at 320×240@24fps, the average end-to-end latency is around **100~150ms**, with smooth video and no noticeable stuttering.

## Hardware Bill of Materials

| Component | Spec / Model | Notes | Est. Price |
|------|------------|------|----------|
| Board ×2 | Luckfox RV1103 Series | ARM Cortex-A7 @ 1GHz, 64MB DDR2; if the Pico Plus variant is too expensive, other RV1103-spec boards can be used as substitutes | ¥30~45 / each |
| WiFi Adapter ×2 | RTL8812AU (Dual-band USB) | This project has a fully ported 8812AU Monitor mode driver. Generic USB adapters cost ~¥20~30 (no PA, ~10m range). **Strongly recommend B-LINK BL-8812AU** adapter (¥50~60, with PA) | ¥20~60 / each |
| Camera ×1 | SC3336 / SC4336 (MIPI CSI) | Max input resolution 2304×1296 | ¥40~50 / each |
| MicroSD Card ×2 | MicroSD (TF) | No speed class requirement; 4GB minimum capacity is sufficient, ~¥5~10 each | ¥5~10 / each |
| Power Supply ×2 | 5V 1.5~2A | USB Type-C or header pin power; can use a cheap Li-ion battery with a DC-DC buck/boost module, ~¥10 | ¥10 / set |

> The air side and ground side use the exact same Luckfox RV1103 + RTL8812AU combination; only the startup scripts differ (`RunAirSide.sh` / `RunGroundSide.sh`).

## Quick Start

### 1. Flash the System Image

This project provides pre-built complete system images. The air side and ground side use the **same image**. Simply flash the image onto a MicroSD card.

> Image download: see the [Releases](https://github.com/GuoHaiZhe12138/BigJiang/releases) page.

Refer to the official Luckfox documentation for flashing instructions; use `rkflash.sh` or the `dd` command to write the image to the SD card.

### 2. Power On and Run

Insert the flashed SD card into the Luckfox Pico Plus, power it on, then SSH in (username `root`, password `luckfox`) and execute the appropriate startup script.

#### Air Side (Transmitter — with Camera + Flight Controller)

```bash
/root/RunAirSide.sh
```

The script automatically initializes the camera, starts video encoding and streaming, and sends video and MAVLink data via the RTL8812AU.

#### Ground Side (Receiver — with Phone)

```bash
/root/RunGroundSide.sh
```

The script waits for the phone's USB tethering to become ready, then starts the wfb receive link and forwards video and telemetry to the phone (UDP port 14550).

### 3. Phone Configuration

1. Install **QGroundControl** on an Android phone
2. Connect the phone via USB to the ground-side Luckfox Pico Plus's USB port
3. Enable "USB Tethering" in the phone's settings
4. Open QGroundControl; video and MAVLink telemetry will automatically connect (UDP port 14550)

### Extracting Overlay Files (Optional)

All project scripts, precompiled drivers, and executables are packaged into the system image via the Luckfox SDK overlay mechanism. If you need to extract these files individually, they can be found at the following path:

```
project/cfg/BoardConfig_IPC/overlay/overlay-luckfox-buildroot-init/root/
├── drivers/                  # Precompiled kernel modules
│   ├── cfg80211.ko
│   ├── 8812au.ko
│   └── simple_vi_bind_venc_udp    # Video encoding and streaming executable
├── scripts/
│   ├── wfb_init.sh           # WiFi adapter Monitor mode initialization
│   └── cameraInit.sh         # Camera ISP + encoding/streaming initialization
├── wfb-keys/                 # wfb-ng key pairs
│   ├── drone.key
│   └── gs.key
├── RunAirSide.sh             # Air side startup script
└── RunGroundSide.sh          # Ground side startup script
```

### socat MAVLink Forwarding Details

Both the air side and ground side use **socat** to achieve bidirectional MAVLink data forwarding (the project directory also includes mavlink-router, which you can use if needed), requiring no additional flight controller configuration. The corresponding socat commands in the startup scripts are as follows:

#### Air Side (RunAirSide.sh)

```bash
socat /dev/ttyS4,b115200,raw,echo=0 UDP4-DATAGRAM:127.0.0.1:14550,bind=127.0.0.1:14551 > /dev/null &
```

This command bidirectionally bridges the flight controller serial port (`/dev/ttyS4`, baud rate 115200) with local UDP ports:

- **FC → Ground (Downlink)**: Serial MAVLink data is sent to UDP `127.0.0.1:14550`, which is then transmitted over the wireless link by `wfb_tx -p 1 -u 14550`
- **Ground → FC (Uplink)**: Ground-side control commands received by `wfb_rx -p 2 -u 14551` are forwarded via UDP `127.0.0.1:14551` to the serial port and sent to the flight controller

#### Ground Side (RunGroundSide.sh)

```bash
socat UDP4-DATAGRAM:127.0.0.1:14551,bind=127.0.0.1:14550,reuseaddr \
    UDP4-DATAGRAM:"$PHONE_IP":14550,bind="$USB0_IP",reuseaddr &
```

This command establishes a bidirectional forwarding bridge between local UDP ports and the phone-side UDP port:

- **Local 14550 ⇄ Phone `$PHONE_IP:14550`**: QGroundControl receives downlink telemetry + sends uplink control through this port
- **Local 14551 ⇄ Phone `$PHONE_IP:14550`**: The wfb link's uplink channel (UDP 14551) is bidirectionally connected to the phone

> **Note**: `$PHONE_IP` is obtained from the phone's USB tethering gateway address via `ip route`; `$USB0_IP` is the IP address of the ground side's usb0 interface.

> **💡 Tip**: The video link and MAVLink link are completely decoupled. If you only need video transmission without flight controller telemetry, simply comment out the `socat`-related lines in `RunAirSide.sh` and `RunGroundSide.sh` — video transmission will not be affected at all.

## Directory Structure

> The directory structure of this repository is essentially the same as the original Luckfox SDK. On top of the original, I have done the following additional work: added wfb-ng, 8812au, and other USB network card drivers to Buildroot; wrote the video media streaming program; and modified Buildroot compilation options. The build process is exactly the same as the official Luckfox SDK — just use `build.sh`.

```
.
├── build.sh                              # Build entry script (same usage as official Luckfox SDK)
├── .BoardConfig.mk                       # Board-level config (Luckfox Pico Plus / SD Card)
├── config/
│   ├── kernel_defconfig                  # Kernel config (RTL8812AU driver and other modules)
│   ├── buildroot_defconfig               # Buildroot package config (wfb-ng, socat, etc.)
│   └── dts_config                        # Device tree config (enables UART3/4 and other peripherals)
├── media/samples/simple_test/
│   └── simple_vi_bind_venc_udp.c         # ★ Core video encoding & streaming program (capture → H.264 encode → RTP/UDP stream)
├── mavlink-router/
│   └── mavlink-router/                   # mavlink-router source + RV1103 cross-compile config
├── project/cfg/BoardConfig_IPC/overlay/
│   └── overlay-luckfox-buildroot-init/   # Overlay files (scripts, drivers, executables)
└── sysdrv/source/kernel/drivers/net/wireless/
    └── rtl8812au/                        # RTL8812AU kernel driver source
```

## Regarding Other WiFi Adapters

During development, I also tried the following adapters besides the RTL8812AU:

| Adapter Model | Chipset | Result |
|---------|------|------|
| **RT3070L** | Ralink RT3070 | Injection rate too slow; heavy packet loss on both TX and RX ends |
| **AR9271** | Atheros AR9271 | Same as above; severe injection rate bottleneck |
| **RTL8812AU** ✅ | Realtek RTL8812AU | Normal injection rate; smooth 240p video with FEC 8:12 |

The specific issue with the RT3070L and AR9271 is: even when setting wfb-ng's FEC redundancy to **k=1,n=4 (i.e., 1 data packet + 3 FEC packets, 4:1 redundancy)**, the 240p video can only barely be transmitted, and still suffers from noticeable stuttering and artifacts. The root cause is that the Packet Injection rate of these two adapters is too low to meet the high-frequency packet transmission demands of wfb-ng in Monitor mode.

Therefore, **the RTL8812AU is strongly recommended**. If you only have an RT3070L or AR9271 on hand, be prepared for significantly degraded video quality, and set the FEC parameters to at least `-k 1 -n 4`.

## License

The modified portions of this project (startup scripts, video streaming program, configuration files, etc.) are licensed under the [MIT License](LICENSE).

This project is developed based on the [Luckfox Pico SDK](https://github.com/LuckfoxTECH/luckfox-pico). The original SDK and Rockchip proprietary components (such as librkaiq, librknnmrt, ISP firmware, etc.) retain their respective original license agreements. These proprietary components are not included in this repository; users must obtain them from official Luckfox channels.

## Acknowledgments

- [Luckfox Pico SDK](https://github.com/LuckfoxTECH/luckfox-pico) - Base SDK and hardware abstraction layer
- [wfb-ng](https://github.com/svpcom/wfb-ng) - Highly robust wireless video/telemetry link
- [mavlink-router](https://github.com/mavlink-router/mavlink-router) - MAVLink message routing and distribution
- [QGroundControl](http://qgroundcontrol.com/) - Open-source ground station software