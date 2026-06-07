#!/bin/sh

# 初始化网卡
./scripts/wfb_init.sh

# 初始化摄像头并开始后台转发
./scripts/cameraInit.sh

# lunch wfb-ng
# video stream
wfb_tx -K wfb-keys/drone.key -p 0 -k 8 -n 12 -M 1 -u 5600 wlan0 > /dev/null &
# command dowm stream
wfb_tx -K wfb-keys/drone.key -p 1 -k 1 -n 3 -M 0 -u 14550 wlan0 > /dev/null &
# command up stream
wfb_rx -K wfb-keys/drone.key -p 2 -u 14551 wlan0 > /dev/null &

# lunch socat
socat /dev/ttyS4,b115200,raw,echo=0 UDP4-DATAGRAM:127.0.0.1:14550,bind=127.0.0.1:14551 > /dev/null &

