# The environment of the "native" CI job (ubuntu-24.04, g++-10 — with a newer
# g++ kors_logger trips over a missing <memory>; 24.04 still carries g++-10 as
# 10.5.0, so the compiler pin survived the move off 22.04, whose runners begin
# deprecation on 2026-09-17).
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++-10 cmake ninja-build ca-certificates zlib1g-dev python3 \
    && rm -rf /var/lib/apt/lists/*
ENV CC=gcc-10 CXX=g++-10
