# Baugleiche Umgebung zum CI-Check "Build: Without Qt" (ubuntu-22.04, g++-10 -
# mit dem Distro-g++ 11 scheitert kors_logger an einem fehlenden <memory>).
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++-10 cmake ninja-build ca-certificates zlib1g-dev python3 \
    && rm -rf /var/lib/apt/lists/*
ENV CC=gcc-10 CXX=g++-10
