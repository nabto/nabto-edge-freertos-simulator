

## Clone

git clone --recurse-submodules https://github.com/nabto/nabto-edge-freertos-simulator

## Building

Plain CMake build.

```
mdkir build
cd build
cmake ..
make -j
```

## Running

Run the demo inside a container such that the host os is not polluted with tap interfaces and iptables rules.

```
docker build -t nabto-edge-freertos-simulator .
```

Running the container privileged

```
docker run --rm -it --privileged nabto-edge-freertos-simulator
```