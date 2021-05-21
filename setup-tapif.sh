#!/bin/bash

export PRECONFIGURED_TAPIF=tap0

sudo ip tuntap add dev $PRECONFIGURED_TAPIF mode tap user `whoami`
sudo ip link set $PRECONFIGURED_TAPIF up
sudo brctl addbr lwipbridge
sudo brctl addif lwipbridge $PRECONFIGURED_TAPIF
sudo ip addr add 192.168.1.1/24 dev lwipbridge
sudo ip link set dev lwipbridge up

sudo iptables -P FORWARD ACCEPT
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -j MASQUERADE