FROM ubuntu:22.04

RUN DEBIAN_FRONTEND=noninteractive apt-get update && apt-get -y install clang-14=1:14.0.0-1ubuntu1.1 build-essential

WORKDIR /build
