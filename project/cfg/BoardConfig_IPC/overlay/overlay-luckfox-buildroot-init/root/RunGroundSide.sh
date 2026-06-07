#!/bin/sh

killall -9 wfb_rx wfb_tx mavlink-routerd socat 2>/dev/null

./scripts/wfb_init.sh

while [ ! -d "/sys/class/net/usb0" ]; do
    echo "请将手机连接到开发板后开启usb网络共享"
    sleep 1
done

echo "usb0就绪"

echo "DHCP请求中"
udhcpc -i usb0

sleep 1

# 获取手机IP（关键）
PHONE_IP=$(ip route | awk '/default/ && /usb0/ {print $3}')
USB0_IP=$(ip -4 addr show dev usb0 | awk '/inet / {print $2}' | cut -d/ -f1)

if [ -z "$PHONE_IP" ]; then
    echo "Error: cannot get phone IP (gateway)" >&2
    exit 1
fi

echo "手机IP: $PHONE_IP"

# lunch wfb_rx
# video
wfb_rx -K wfb-keys/gs.key -p 0 -u 5600 -c "$PHONE_IP" wlan0 > /dev/null &
# command down stream
wfb_rx -K wfb-keys/gs.key -p 1 -u 14550 wlan0 > /dev/null &
# command up stream
wfb_tx -K wfb-keys/gs.key -p 2 -k 1 -n 3 -M 0 -u 14551 wlan0 > /dev/null &

sleep 1

echo "启动 socat 转发..."

socat UDP4-DATAGRAM:127.0.0.1:14551,bind=127.0.0.1:14550,reuseaddr UDP4-DATAGRAM:"$PHONE_IP":14550,bind="$USB0_IP",reuseaddr &

echo "所有链路已就绪！"

