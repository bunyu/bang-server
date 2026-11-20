# bang-server container image (for Railway, Fly.io or any Docker host).
#   docker build -t bang-server .
#   docker run -p 47654:47654 bang-server
# The server listens on $PORT (Railway sets it), default 47654.

# ---- build ----
FROM ubuntu:24.04 AS build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential g++-14 cmake git ca-certificates pkg-config \
        python3 python3-yaml python3-pil \
        libsqlite3-dev libuv1-dev libpng-dev libcxxopts-dev zlib1g-dev libssl-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
# The C++23 templates need a lot of memory per compiler process: keep parallelism low.
ARG JOBS=2
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
    && cmake --build build -j${JOBS} \
    && strip build/bangserver

# ---- run ----
FROM ubuntu:24.04
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        libsqlite3-0 libuv1t64 libpng16-16t64 zlib1g libssl3t64 libstdc++6 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/bangserver /usr/local/bin/bangserver
ENV PORT=47654
EXPOSE 47654
CMD ["sh", "-c", "exec bangserver \"$PORT\" -r -l info"]
