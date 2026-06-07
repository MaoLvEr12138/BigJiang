# (BigJiang)大酱 - 低成本无人机数字图传系统

[English](README.md) | **中文**

## 项目简介

大酱是一套基于 **Luckfox Pico Plus (RV1103)** + **RTL8812AU** 的自制无人机数字图传系统。地面端和天空端均使用相同的硬件方案，搭配 [wfb-ng](https://github.com/svpcom/wfb-ng) 实现低延迟数字图传与 MAVLink 双向遥测。

> 本项目基于 Luckfox Pico SDK（Rockchip 官方 SDK 修改版）进行二次开发。

## 功能特性

- **低延迟 H.264 数字图传**：基于 RV1103 硬件编码器，320×240@24fps 下端到端总延迟约 **100~150ms**，配合 Intra Refresh 行刷新策略，无传统 I 帧的瞬时码率峰值
- **可变码率/分辨率**：分辨率和编码码率可自由调整，240p 下可将实际码率压缩到 **5000~20000 Bytes/s**
- **自研 RTP/UDP 视频推流**：自定义 RTP FU-A 分片封装，辅以帧内 NALU 时间片 pacing 发送机制，彻底消除网卡瞬时洪峰
- **WFB-NG 无线链路**：RTL8812AU 网卡工作在 Monitor 模式，wfb-ng 提供高鲁棒性 FEC 前向纠错无线传输
- **MAVLink 双向遥测**：视频流、飞控下行遥测、地面端上行控制三条通道完全独立，互不干扰
- **手机直连地面站**：地面端通过 USB 网络共享连接安卓手机，在 **QGroundControl** 中即可接收视频和遥测，视频流会通过 ground 端脚本转发到 14550 端口
- **SOCAT 串口透传**：空中端自动将飞控串口 MAVLink 数据桥接到 wfb 链路，无需额外飞控配置

## 系统架构

```
天空端 (Air Side)
┌─────────────────────────────────────────────────────────┐
│  摄像头 ──→ VI ──→ VPSS (缩放) ──→ VENC (H.264编码)      │
│                                          │              │
│                                   RTP FU-A / UDP        │
│                                     127.0.0.1:5600      │
│                                          │              │
│              wfb_tx ──────────→ RTL8812AU (Monitor 模式) │
│              wfb_rx ←────────── RTL8812AU (Monitor 模式) │
│                                          │              │
│     飞控 /dev/ttyS4 ←──→ socat ←──→ UDP:14550/14551     │
│                              (MAVLink 双向透传)          │
└─────────────────────────────────────────────────────────┘
                           │
                    ~~~ 5.8GHz ~~~
                           │
地面端 (Ground Side)
┌─────────────────────────────────────────────────────────┐
│      RTL8812AU (Monitor) ──→ wfb_rx ──→ UDP:5600 视频   │
│      RTL8812AU (Monitor) ←── wfb_tx ←── UDP:14551 上行   │
│      RTL8812AU (Monitor) ──→ wfb_rx ──→ UDP:14550 下行   │
│                                          │               │
│                        socat 双向 UDP 转发               │
│                                          │               │
│                       usb0 (手机 USB 网络共享)            │
│                                          │               │
│                   安卓手机 APP (QGroundControl / FPV)     │
└─────────────────────────────────────────────────────────┘
```

## 延迟表现

端到端延迟通过**同时录制原始时钟与无人机拍摄到的时钟画面**的方式进行实测：在同一帧内对比手机秒表原始读数与图传画面中的秒表读数，两者差值即为端到端总延迟。

> 完整测试视频见仓库根目录下的 **[Delayed_test.mp4](Delayed_test.mp4)**。

实测结果表明，在 320×240@24fps 配置下，端到端总延迟平均在 **100~150ms** 左右，画面流畅无明显卡顿。

## 硬件清单

| 组件 | 规格 / 型号 | 说明 | 参考价格 |
|------|------------|------|----------|
| 主板 ×2 | Luckfox RV1103 系列 | ARM Cortex-A7 @ 1GHz，64MB DDR2；若 Pico Plus 版型价格偏高，可使用其他 RV1103 规格板型平替 | ¥30~45 / 片 |
| WiFi 网卡 ×2 | RTL8812AU (双频 USB) | 本工程已完美移植 8812AU Monitor 模式驱动。普通 USB 网卡约 ¥20~30（无功放，通信距离约 10m），**强烈推荐必联 BL-8812AU** 网卡（¥50~60，带功放） | ¥20~60 / 个 |
| 摄像头 ×1 | SC3336 / SC4336 (MIPI CSI) | 最大输入分辨率 2304×1296 | ¥40~50 / 个 |
| 存储卡 ×2 | MicroSD 卡 (TF) | 无速率要求，最低 4GB 容量即可，每张约 ¥5~10 | ¥5~10 / 张 |
| 供电 ×2 | 5V 1.5~2A | USB Type-C 或排针供电；可使用廉价锂电池搭配 DC-DC 稳压模块，成本约 ¥10 | ¥10 / 套 |

> 天空端和地面端使用完全相同的 Luckfox RV1103 + RTL8812AU 组合，仅启动脚本不同（`RunAirSide.sh` / `RunGroundSide.sh`）。

## 快速开始

### 1. 烧录系统镜像

本项目提供编译好的完整系统镜像，天空端和地面端使用**同一份镜像**。将镜像烧录至 MicroSD 卡即可。

> 镜像下载地址：见 [Releases](https://github.com/GuoHaiZhe12138/BigJiang/releases) 页面。

烧录方式参考 Luckfox 官方文档，使用 `rkflash.sh` 或 `dd` 命令将镜像写入 SD 卡。

### 2. 上电运行

将烧录好的 SD 卡插入 Luckfox Pico Plus，上电启动后 SSH 登录（用户名 `root`，密码 `luckfox`），根据用途执行不同的启动脚本。

#### 天空端（发送端 — 接摄像头 + 飞控）

```bash
/root/RunAirSide.sh
```

脚本会自动初始化摄像头、启动视频编码推流、并通过 RTL8812AU 发送视频和 MAVLink 数据。

#### 地面端（接收端 — 接手机）

```bash
/root/RunGroundSide.sh
```

脚本会等待手机 USB 网络共享就绪，然后启动 wfb 接收链路，将视频和遥测转发到手机端（UDP 14550 端口）。

### 3. 手机端配置

1. 安卓手机安装 **QGroundControl**
2. 手机通过 USB 连接地面端 Luckfox Pico Plus 的 USB 口
3. 在手机设置中开启「USB 网络共享」(USB Tethering)
4. 打开 QGroundControl，视频和 MAVLink 遥测会自动连接（UDP 14550 端口）

### 提取 overlay 文件（可选）

项目中的脚本、预编译驱动和可执行文件均通过 Luckfox SDK 的 overlay 机制打包进系统镜像。如果你需要单独提取这些文件，可以在以下路径找到：

```
project/cfg/BoardConfig_IPC/overlay/overlay-luckfox-buildroot-init/root/
├── drivers/                  # 预编译内核模块
│   ├── cfg80211.ko
│   ├── 8812au.ko
│   └── simple_vi_bind_venc_udp    # 视频编码推流可执行文件
├── scripts/
│   ├── wfb_init.sh           # WiFi 网卡 Monitor 模式初始化
│   └── cameraInit.sh         # 摄像头 ISP + 编码推流初始化
├── wfb-keys/                 # wfb-ng 密钥对
│   ├── drone.key
│   └── gs.key
├── RunAirSide.sh             # 天空端启动脚本
└── RunGroundSide.sh          # 地面端启动脚本
```

### socat MAVLink 转发详解

天空端和地面端均使用 **socat** 实现 MAVLink 数据的双向转发(工程目录内也有mavlink-router，如果你需要可以使用它)，无需额外飞控配置。启动脚本中对应的 socat 命令如下：

#### 天空端（RunAirSide.sh）

```bash
socat /dev/ttyS4,b115200,raw,echo=0 UDP4-DATAGRAM:127.0.0.1:14550,bind=127.0.0.1:14551 > /dev/null &
```

该命令将飞控串口（`/dev/ttyS4`，波特率 115200）与本地 UDP 端口双向桥接：

- **飞控 → 地面端（下行）**：串口 MAVLink 数据发往 UDP `127.0.0.1:14550`，由 `wfb_tx -p 1 -u 14550` 通过无线链路发送
- **地面端 → 飞控（上行）**：`wfb_rx -p 2 -u 14551` 收到的地面端控制指令通过 UDP `127.0.0.1:14551` 转发到串口，发送给飞控

#### 地面端（RunGroundSide.sh）

```bash
socat UDP4-DATAGRAM:127.0.0.1:14551,bind=127.0.0.1:14550,reuseaddr \
    UDP4-DATAGRAM:"$PHONE_IP":14550,bind="$USB0_IP",reuseaddr &
```

该命令在本地 UDP 端口和手机端 UDP 端口之间建立双向转发桥：

- **本地 14550 ⇄ 手机 `$PHONE_IP:14550`**：QGroundControl 通过此端口接收下行遥测 + 发送上行控制
- **本地 14551 ⇄ 手机 `$PHONE_IP:14550`**：wfb 链路上行通道（UDP 14551）与手机端双向互通

> **说明**：`$PHONE_IP` 通过 `ip route` 获取手机 USB 网络共享的网关地址，`$USB0_IP` 为地面端 usb0 网卡的 IP 地址。

> **💡 提示**：图传链路与 MAVLink 链路完全解耦。如果你只需要视频传输而不需要飞控遥测，直接在 `RunAirSide.sh` 和 `RunGroundSide.sh` 中将 `socat` 相关行注释掉即可，视频传输不受任何影响。

## 目录结构

> 本仓库的目录结构与 Luckfox 原始 SDK 基本一致。我在原版基础上额外做了以下工作：向 Buildroot 中添加了 wfb-ng、8812au 等 USB 网卡驱动，编写了 video media 推流程序，并修改了 Buildroot 的编译选项。编译方式与官方 Luckfox SDK 完全相同，使用 `build.sh` 即可。

```
.
├── build.sh                              # 编译入口脚本（用法同官方 Luckfox SDK）
├── .BoardConfig.mk                       # 板级配置（Luckfox Pico Plus / SD卡）
├── config/
│   ├── kernel_defconfig                  # 内核配置（RTL8812AU 驱动等模块）
│   ├── buildroot_defconfig               # Buildroot 包配置（wfb-ng、socat 等）
│   └── dts_config                        # 设备树（开启 UART3/4 等外设）
├── media/samples/simple_test/
│   └── simple_vi_bind_venc_udp.c         # ★ 核心视频编码推流程序（捕获画面 → H.264 编码 → RTP/UDP 推流）
├── mavlink-router/
│   └── mavlink-router/                   # mavlink-router 源码 + RV1103 交叉编译配置
├── project/cfg/BoardConfig_IPC/overlay/
│   └── overlay-luckfox-buildroot-init/   # overlay 文件（脚本、驱动、可执行文件）
└── sysdrv/source/kernel/drivers/net/wireless/
    └── rtl8812au/                        # RTL8812AU 内核驱动源码
```

## 关于其他 WiFi 网卡

在开发过程中，除了 RTL8812AU，我还尝试过以下网卡：

| 网卡型号 | 芯片 | 结果 |
|---------|------|------|
| **RT3070L** | Ralink RT3070 | 注入速度过慢，收发两端均出现大量丢包 |
| **AR9271** | Atheros AR9271 | 同上，注入速度瓶颈严重 |
| **RTL8812AU** ✅ | Realtek RTL8812AU | 注入速度正常，配合 FEC 8:12 可流畅传输 240p 视频 |

RT3070L 和 AR9271 的问题具体表现为：即使将 wfb-ng 的 FEC 冗余开到 **k=1,n=4（即 1 个数据包配 3 个纠错包，4:1 冗余）**，也只能勉强传输 240p 视频画面，而且仍存在明显的卡顿和马赛克。根本原因在于这两款网卡的 Packet Injection（包注入）速率过低，无法满足 wfb-ng 在 Monitor 模式下高频率发包的需求。

因此**强烈推荐使用 RTL8812AU**，如果你手头只有 RT3070L 或 AR9271，请做好画质严重下降的心理准备，并将 FEC 参数至少设置为 `-k 1 -n 4`。

## 许可证

本项目的修改部分（启动脚本、视频推流程序、配置文件等）使用 [MIT License](LICENSE)。

本项目基于 [Luckfox Pico SDK](https://github.com/LuckfoxTECH/luckfox-pico) 开发，原始 SDK 及 Rockchip 闭源组件（如 librkaiq、librknnmrt、ISP 固件等）保留各自的原始许可协议。这些闭源组件未包含在本仓库中，用户需自行从 Luckfox 官方渠道获取。

## 致谢

- [Luckfox Pico SDK](https://github.com/LuckfoxTECH/luckfox-pico) - 基础 SDK 与硬件抽象层
- [wfb-ng](https://github.com/svpcom/wfb-ng) - 高鲁棒性无线视频/遥测链路
- [mavlink-router](https://github.com/mavlink-router/mavlink-router) - MAVLink 消息路由分发
- [QGroundControl](http://qgroundcontrol.com/) - 开源地面站软件