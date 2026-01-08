FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV QUANTLIB_VERSION=1.40
ENV INSTALL_PREFIX=/usr/local

# Copy and run build script
COPY ci/build-deps.sh /tmp/build-deps.sh
RUN chmod +x /tmp/build-deps.sh && /tmp/build-deps.sh && rm /tmp/build-deps.sh

# Install uv (Python deps installed at runtime via uv sync)
RUN curl -LsSf https://astral.sh/uv/install.sh | sh
ENV PATH="/root/.local/bin:$PATH"

# Library paths
ENV LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib:${LD_LIBRARY_PATH}"
ENV PKG_CONFIG_PATH="${INSTALL_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH}"

WORKDIR /workspace
