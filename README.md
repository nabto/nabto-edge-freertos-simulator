# Nabto Edge FreeRTOS + LwIP simulator

The purpose of this repository/example is:

1. A FreeRTOS Nabto Edge integration.
2. Show how a typical embedded target with FreeRTOS and the LwIP IP stack works with Nabto Edge.

The example consists of the FreeRTOS linux simulator, the LwIP stack running
simulated on linux using a tap interface and the Nabto Edge embedded SDK.

FreeRTOS linux simulator, the example is based around the simulator. The
simulator is running the LwIP thread and a thread running the nabto edge
example.

LwIP, the example uses the LwIP IP stack integrated on linux using a TAP
interface. The tap interface is a layer 2 integration between the LwIP stack and
the linux system where the demo is running on.

Nabto Edge Embedded SDK, the Nabto Edge example uses the Embedded SDK to create
a Nabto Edge device. The example mimics the simple_coap example from the
embedded sdk. It is a Nabto Edge device with a CoAP server responding to the
/hello-word path.

## Project State

Implemented:
  * FreeRTOS integrated with Nabto SDK
  * LwIP TCP, DNS and UDP integrated with Nabto SDK
  * LwIP via TAP interfaces.
  * Simple CoAP demo

TODO:
  * libpcap integration for LwIP such that LwIP can be used directly with ethernet interfaces.


## Building

git clone --recurse-submodules https://github.com/nabto/nabto-edge-freertos-simulator

It is a plain CMake build.

```
mdkir build
cd build
cmake ..
make -j
```

## Running

### Linux

dependencies: nftables

./setup-tapif.sh
./build/nabto_demo

setup-tapif.sh creates a new tapif and setup NAT rules in the firewall using

### Docker

To not contaminate your system with custom tap interfaces and firewall rules the
example can be run inside a docker container.

docker build . -t edge-freertos
docker run --rm -it --privileged edge-freertos

Make a new git clone and build the software inside the container.

### Vscode

Open the project in vscode and start hacking using a remote-container workflow.


## Notes

### PCAP Inject to Wifi networks.

This is most likely not going to work for an explanation read the [VirtualBox
notes](https://www.virtualbox.org/manual/ch06.html#network_bridged). TLDR:
Multiple macs sending on a single wifi interface in client mode is not a good
idea.

### mDNS inside docker container

If the demo app is running inside a docker container mdns discovery is not
working outside the container.

### mDNS

mDNS only works on the local machine if the simulator is not running inside a
container and the mDNS client is also running on the local machine, due to
multicast on the tap interface not being routed to the primary network
interface.
