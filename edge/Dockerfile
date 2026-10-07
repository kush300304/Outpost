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
RUN cmake --build build --target edge_server -j$(nproc)

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/build/edge/edge_server /app/edge_server

EXPOSE 8080
ENTRYPOINT ["/app/edge_server"]
