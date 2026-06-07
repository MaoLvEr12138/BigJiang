#!/bin/sh

# 载入协议驱动和网卡驱动
insmod /root/drivers/cfg80211.ko
insmod /root/drivers/8812au.ko

sleep 1

# 设置为monitor模式
ip link set wlan0 down
iw dev wlan0 set type monitor
ip link set wlan0 up

sleep 1

# 参数设置
iw reg set US
iw dev wlan0 set channel 149 HT20
iw dev wlan0 set power_save off
iw dev wlan0 set txpower fixed 1500

