FROM debian:trixie

# Install necessary packages
RUN apt update -y && apt install -y \
	build-essential \
	cmake\
	pkg-config

RUN apt install -y \
	libgstreamer1.0-0 \
	libgstreamer1.0-dev \
	libgstreamer-plugins-base1.0-0 \
	libgstreamer-plugins-base1.0-dev \
	libgstreamer-plugins-bad1.0-0 \
	libgstreamer-plugins-bad1.0-dev \
	gstreamer1.0-rtsp \
	libgstrtspserver-1.0-dev

RUN apt install -y \
	libboost-system-dev \ 
	libboost-dev \
	libboost-thread-dev \
	libboost-regex-dev

WORKDIR /workspace

COPY . /workspace

RUN mkdir build && cd build && \
	cmake .. && \
	make

CMD ["./build/main"]