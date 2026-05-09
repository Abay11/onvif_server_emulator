FROM debian:trixie

# Install necessary packages
RUN apt update -y && apt install -y --fix-missing \
	build-essential \
	cmake \
	pkg-config

RUN apt install -y --fix-missing \
	libgstreamer1.0-dev \
	libgstreamer-plugins-base1.0-dev \
	libgstreamer-plugins-bad1.0-dev \
	gstreamer1.0-rtsp \
	libgstrtspserver-1.0-dev \
	gstreamer1.0-plugins-good \
	gstreamer1.0-plugins-ugly \
	gstreamer1.0-libav \
	gstreamer1.0-tools

RUN apt install -y --fix-missing \
	libboost-system-dev \
	libboost-dev \
	libboost-thread-dev \
	libboost-regex-dev

WORKDIR /workspace

COPY . .

# Avoid full recompilation by using build cache
RUN --mount=type=cache,target=/workspace/build \
    mkdir -p build && cd build && \
    cmake .. && \
    make -j$(nproc) && \
    mkdir -p /app && cp main /app/main

ENV GST_DEBUG=*:3
WORKDIR /app
CMD ["./main"]