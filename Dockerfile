# SPDX-FileCopyrightText: 2022 Pepijn de Vos
#
# SPDX-License-Identifier: MPL-2.0

FROM ubuntu:latest as build

RUN apt-get update -qq && \
DEBIAN_FRONTEND=noninteractive apt-get -y install --no-install-recommends \
build-essential \
cmake \
pkg-config \
capnproto \
libcapnp-dev \
libngspice0-dev \
libboost-all-dev

COPY . /tmp/build
WORKDIR /tmp/build

RUN mkdir -p build; \
cd build; \
cmake -DCMAKE_INSTALL_PREFIX="/usr/local" ..; \
make DESTDIR=/tmp -j$(nproc) install

FROM ubuntu:latest

RUN apt-get update -qq && \
DEBIAN_FRONTEND=noninteractive apt-get -y install --no-install-recommends \
capnproto \
libngspice0 \
libboost-filesystem1.74.0

COPY --from=build /tmp/usr/local /usr/local

EXPOSE 5923
CMD ["NgspiceSimServer"]