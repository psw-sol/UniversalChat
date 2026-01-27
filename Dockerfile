# =============================================================================
# UniversalChatServer Dockerfile
# Multi-stage build for C++ chat server
# =============================================================================

# -----------------------------------------------------------------------------
# Stage 1: Build stage
# -----------------------------------------------------------------------------
FROM ubuntu:22.04 AS builder

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    # Boost libraries
    libboost-dev \
    libboost-system-dev \
    libboost-thread-dev \
    # Protobuf
    protobuf-compiler \
    libprotobuf-dev \
    # JSON library
    nlohmann-json3-dev \
    # Logging library
    libspdlog-dev \
    # SSL for password hashing
    libssl-dev \
    # Redis (optional)
    libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source code
COPY . .

# Create build directory and configure
RUN mkdir -p build && cd build && \
    cmake .. \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF \
        -DENABLE_REDIS=ON \
        -DCMAKE_INSTALL_PREFIX=/usr/local

# Build the project
RUN cd build && ninja

# Install
RUN cd build && ninja install

# -----------------------------------------------------------------------------
# Stage 2: Runtime stage
# -----------------------------------------------------------------------------
FROM ubuntu:22.04 AS runtime

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies only
RUN apt-get update && apt-get install -y \
    libboost-system1.74.0 \
    libboost-thread1.74.0 \
    libprotobuf23 \
    libspdlog1 \
    libssl3 \
    libhiredis0.14 \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user for security
RUN useradd -m -s /bin/bash chatserver

# Create directories
RUN mkdir -p /app/config /app/logs && \
    chown -R chatserver:chatserver /app

# Copy executable from builder
COPY --from=builder /usr/local/bin/chat_server /app/chat_server

# Copy config file
COPY config/server.docker.json /app/config/server.json

# Set ownership
RUN chown -R chatserver:chatserver /app

# Switch to non-root user
USER chatserver

# Set working directory
WORKDIR /app

# Expose port
EXPOSE 7777

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD nc -z localhost 7777 || exit 1

# Run the server
ENTRYPOINT ["./chat_server"]
CMD ["--config", "/app/config/server.json"]
