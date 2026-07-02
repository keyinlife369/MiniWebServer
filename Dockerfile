# MiniWebServer 容器化构建环境
# 构建: docker build -t miniwebserver-build .
# 运行: docker run --rm -v .:/src miniwebserver-build

FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# 安装构建依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    libboost-all-dev \
    libssl-dev \
    libmysqlclient-dev \
    qt6-base-dev \
    libqt6network6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# 构建项目
COPY . .
RUN mkdir -p build && cd build && \
    cmake .. -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF \
        -DBUILD_BENCH=ON \
        -DMYSQL_DIR=/usr \
    && cmake --build . -j$(nproc)

# 运行阶段（最小镜像）
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y \
    libboost-all-dev \
    libssl3 \
    libmysqlclient21 \
    qt6-base-dev \
    libqt6network6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/MiniWebServer /app/MiniWebServer
COPY --from=builder /src/static /app/static
COPY --from=builder /src/certs /app/certs

WORKDIR /app
EXPOSE 8080 8443

CMD ["./MiniWebServer"]
