FROM debian:buster

RUN apt-get update && apt-get install -y build-essential iptables cmake git
