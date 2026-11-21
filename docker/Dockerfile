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
        libssl3t64 \
        libgstrtspserver-1.0-0 \
    && rm -rf /var/lib/apt/lists/*

FROM base AS toolchain

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
        libboost-test-dev \
        libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        libgstrtspserver-1.0-dev \
        libssl-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
CMD ["bash"]

# Pinned metadata plugin, shared by the runtime and development images.
FROM toolchain AS metadata-plugin-build

RUN apt-get update \
    && apt-get install -y --no-install-recommends cargo rustc libpango1.0-dev \
    && rm -rf /var/lib/apt/lists/*

# Annotated upstream tag 0.13.7 resolves to this commit. Cargo.lock pins the
# Rust dependencies; do not replace this with a moving release branch.
RUN git clone https://github.com/GStreamer/gst-plugins-rs.git /opt/gst-plugins-rs \
    && cd /opt/gst-plugins-rs \
    && git checkout cdec77cad2d9b7348d800507bc6eeec5039717eb \
    && test "$(git rev-parse HEAD)" = cdec77cad2d9b7348d800507bc6eeec5039717eb

RUN --mount=type=cache,id=osrv-metadata-cargo,target=/root/.cargo,sharing=locked \
    cd /opt/gst-plugins-rs \
    && cargo build --locked --release --jobs 2 -p gst-plugin-onvif \
    && install -D target/release/libgstrsonvif.so /opt/metadata/libgstrsonvif.so \
    && git archive HEAD | gzip > /opt/metadata/gst-plugins-rs-source.tar.gz \
    && cp net/onvif/LICENSE-MPL-2.0 /opt/metadata/LICENSE-MPL-2.0

RUN --mount=type=cache,id=osrv-metadata-cargo,target=/root/.cargo,sharing=locked \
    strip --strip-unneeded /opt/metadata/libgstrsonvif.so \
    && mkdir -p /opt/metadata/dependency-notices \
    && find /root/.cargo/registry/src /root/.cargo/git/checkouts -type f \
        \( -iname 'license*' -o -iname 'copying*' -o -iname 'copyright*' -o -iname 'notice*' \) \
        -exec cp --parents '{}' /opt/metadata/dependency-notices/ \;

# Runtime dependency proof without compiling the emulator. Include source and
# notices with the plugin; the upstream ONVIF plugin uses MPL-2.0.
FROM base AS metadata-runtime
COPY --from=metadata-plugin-build /opt/metadata/libgstrsonvif.so /usr/local/lib/gstreamer-1.0/
COPY --from=metadata-plugin-build /opt/metadata/LICENSE-MPL-2.0 /opt/metadata/gst-plugins-rs-source.tar.gz /usr/local/share/osrv-metadata/
COPY --from=metadata-plugin-build /opt/metadata/dependency-notices/ /usr/local/share/osrv-metadata/dependency-notices/
ENV GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0

FROM toolchain AS dev
COPY --from=metadata-runtime /usr/local/ /usr/local/
ENV GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0
RUN apt-get update \
    && apt-get install -y --no-install-recommends python3-jsonschema python3-lxml python3-xmlschema python3-gi gir1.2-gst-rtsp-server-1.0 \
    && rm -rf /var/lib/apt/lists/* \
    && gst-inspect-1.0 appsrc \
    && gst-inspect-1.0 rtponvifmetadatapay

FROM toolchain AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends python3-jsonschema \
    && rm -rf /var/lib/apt/lists/*

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
    && install -D build/main /app/main \
    && cp -a build/config_validation /app/config_validation

FROM metadata-runtime AS runtime

RUN apt-get update \
    && apt-get install -y --no-install-recommends python3-jsonschema \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/main ./main
COPY --from=build /app/config_validation ./config_validation
COPY server_configs ./server_configs

EXPOSE 8080 8554
CMD ["./main"]
