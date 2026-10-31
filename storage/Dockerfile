FROM gcc:12-bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    git \
    make \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
RUN cmake --build build --target storage_server -j$(nproc)

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
RUN mkdir -p /data
COPY --from=builder /app/build/storage/storage_server /app/storage_server

EXPOSE 8091
ENTRYPOINT ["/app/storage_server"]
