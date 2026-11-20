# Builder stage: identical in every service Dockerfile, so BuildKit builds all four binaries once
# and every image reuses the cached layer.
FROM debian:bookworm AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ cmake make git ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY common ./common
COPY edge ./edge
COPY router ./router
COPY storage ./storage
COPY write-api ./write-api
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build /build -j"$(nproc)" \
        --target storage_server write_api_server edge_server router_server

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends curl ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 outpost \
    && mkdir -p /data && chown outpost /data
COPY --from=builder /build/storage/storage_server /usr/local/bin/storage_server
USER outpost
EXPOSE 9091
ENTRYPOINT ["/usr/local/bin/storage_server"]
