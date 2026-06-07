#!/bin/sh

# 运行驱动挂载脚本.
sh /oem/usr/ko/insmod_ko.sh

sleep 1

# 运行摄像头控制算法
rkaiq_3A_server &

sleep 1


/root/drivers/simple_vi_bind_venc_udp > /dev/null 2>&1 &

