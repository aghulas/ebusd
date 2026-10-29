#!/bin/sh
# Build a static ebusd binary (with MQTT, without TLS/KNX) in an Alpine container and run the unit tests.
# Usage: docker run --rm -v "$PWD":/src:ro -v "$PWD/out":/out alpine:3.20 sh /src/contrib/weishaupt/build-static.sh
# Output: /out/ebusd-<arch> and /out/sha256-<arch>.txt with <arch> as used by Home Assistant (aarch64, amd64).
set -e
MOSQUITTO_VERSION=v2.0.18
case "$(uname -m)" in
  aarch64) ARCH=aarch64 ;;
  x86_64) ARCH=amd64 ;;
  *) echo "unsupported architecture $(uname -m)"; exit 1 ;;
esac

apk add -q build-base autoconf automake argp-standalone linux-headers cmake git

git clone -q --depth 1 -b $MOSQUITTO_VERSION https://github.com/eclipse/mosquitto /tmp/mosquitto
mkdir /tmp/mosquitto/build && cd /tmp/mosquitto/build
cmake .. -DWITH_STATIC_LIBRARIES=ON -DWITH_SHARED_LIBRARIES=OFF -DWITH_TLS=OFF -DWITH_CJSON=OFF \
  -DDOCUMENTATION=OFF -DWITH_BROKER=OFF -DWITH_APPS=OFF -DWITH_CLIENTS=OFF -DWITH_PLUGINS=OFF \
  -DCMAKE_INSTALL_PREFIX=/usr/local >/dev/null
make -j"$(nproc)" >/dev/null && make install >/dev/null
ln -s /usr/local/lib/libmosquitto_static.a /usr/local/lib/libmosquitto.a

mkdir /tmp/ebusd && tar -C /src --exclude=.git -cf - . | tar -C /tmp/ebusd -xf -
cd /tmp/ebusd
./autogen.sh >/dev/null 2>&1 || autoreconf -fi >/dev/null
CPPFLAGS="-I/usr/local/include" LDFLAGS="-static -L/usr/local/lib" LIBS="-largp -lpthread" \
  ./configure --without-ssl --without-knx --without-contrib >/dev/null
make -j"$(nproc)" >/dev/null

# unit tests of the core library (contrib is not built)
(cd src/lib/ebus/test && make >/dev/null && ./test_filereader >/dev/null && ./test_data >/dev/null \
  && ./test_message >/dev/null && ./test_symbol >/dev/null)
./src/ebusd/ebusd --version
./src/ebusd/ebusd --help | grep -q mqtthost

mkdir -p /out
strip -o /out/ebusd-$ARCH src/ebusd/ebusd
cd /out && sha256sum ebusd-$ARCH > sha256-$ARCH.txt && cat sha256-$ARCH.txt
