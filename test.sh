#!/bin/bash

sudo ip tuntap add dev tap0 mode tap user vscode
sudo brctl addbr br0
sudo brctl addif br0 tap0
sudo ip link set dev br0 up
sudo ip link set dev tap0 up
sudo ip addr add 192.168.1.1/24 dev br0