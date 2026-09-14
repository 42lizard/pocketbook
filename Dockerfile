# syntax=docker/dockerfile:1
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl p7zip-full make cmake ninja-build file \
    libstdc++6 libncurses5 libtinfo5 zlib1g \
    && rm -rf /var/lib/apt/lists/*

# Official PocketBook SDK 6.8, B288 (InkPad 4 and Verse Pro).
# Preserve the SDK's relative/chained symlinks; only extract this pinned archive.
RUN curl -fL --retry 3 \
      https://github.com/pocketbook/SDK_6.3.0/releases/download/6.8/SDK-B288-6.8.7z \
      -o /tmp/sdk.7z \
    && echo 'b924fbeba90e9258854e910c2c29a3105e6fb9c6e0e6de4cb13a015be3fc23e2  /tmp/sdk.7z' | sha256sum -c - \
    && 7z x -snld20 /tmp/sdk.7z -o/opt > /tmp/sdk-extract.log \
    && rm /tmp/sdk.7z /tmp/sdk-extract.log

# GCC 6 needs the matching MPC/MPFR libraries supplied by the SDK.
RUN ln -s /opt/SDK-B288/usr/lib/libmpc.so.3 /usr/local/lib/libmpc.so.3 \
    && ln -s /opt/SDK-B288/usr/lib/libmpfr.so.4 /usr/local/lib/libmpfr.so.4 \
    && ldconfig

ENV SDK_ROOT=/opt/SDK-B288
ENV PATH="/opt/SDK-B288/usr/bin:${PATH}"
ENV CC=arm-obreey-linux-gnueabi-gcc CXX=arm-obreey-linux-gnueabi-g++
WORKDIR /workspace
CMD ["make"]
