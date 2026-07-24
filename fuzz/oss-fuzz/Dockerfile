# OSS-Fuzz container image for slabflux ingress fuzzing.
FROM gcr.io/oss-fuzz-base/base-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake ninja-build libnuma-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR ${SRC}/slabflux
COPY . ${SRC}/slabflux

RUN fuzz/oss-fuzz/build.sh
