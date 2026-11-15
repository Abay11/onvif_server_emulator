# syntax=docker/dockerfile:1

FROM debian:trixie-slim AS base

# Runtime plugins are explicit because recommended packages are disabled.
# x provides timeoverlay; bad provides rtponviftimestamp for replay.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        fonts-dejavu-core \
        gstreamer1.0-libav \
        gstreamer1.0-plugins-base \
        gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-ugly \
        gstreamer1.0-rtsp \
        gstreamer1.0-x \
        libboost-date-time1.83.0 \
        libboost-regex1.83.0 \
        libboost-system1.83.0 \
        libboost-thread1.83.0 \
        libgstrtspserver-1.0-0 \
    && rm -rf /var/lib/apt/lists/*

FROM base AS dev

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        ccache \
        cmake \
        git \
        gstreamer1.0-tools \
        libboost-date-time-dev \
        libboost-regex-dev \
        libboost-system-dev \
        libboost-thread-dev \
        libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        libgstrtspserver-1.0-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
CMD ["bash"]

FROM dev AS build

ARG CMAKE_BUILD_TYPE=Debug
ARG BUILD_JOBS
ENV CCACHE_DIR=/root/.cache/ccache

COPY . .

# Copy the executable out of the cache mount: mount contents are not image layers.
RUN --mount=type=cache,id=onvif-build,target=/workspace/build,sharing=locked \
    --mount=type=cache,id=onvif-ccache,target=/root/.cache/ccache,sharing=locked \
    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DENABLE_UNIT_TESTS=OFF \
    && cmake --build build --parallel "${BUILD_JOBS:-$(nproc)}" \
    && install -D build/main /app/main

FROM base AS runtime

WORKDIR /app
COPY --from=build /app/main ./main
COPY server_configs ./server_configs

EXPOSE 8080 8554
CMD ["./main"]
