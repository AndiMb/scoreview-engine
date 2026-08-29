# The environment of the "native" CI job (ubuntu-22.04, g++-10 — with the
# distro's g++ 11 kors_logger trips over a missing <memory>).
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++-10 cmake ninja-build ca-certificates zlib1g-dev python3 \
    && rm -rf /var/lib/apt/lists/*
ENV CC=gcc-10 CXX=g++-10
