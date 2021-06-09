#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

sudo ip tuntap add dev tap0 mode tap user `whoami`
sudo ip link set dev tap0 up
sudo ip addr add 192.168.100.1/24 dev tap0

sudo sysctl -w net.ipv4.ip_forward=1

sudo nft --version 2>&1 > /dev/null
if [ $? -ne 0 ]; then
  echo "missing nft executable, install the nftables package"
else
  sudo ${SCRIPT_DIR}/firewall.nft
fi
