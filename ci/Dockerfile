FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV QUANTLIB_VERSION=1.40
ENV INSTALL_PREFIX=/usr/local

# Copy and run build script
COPY ci/build-deps.sh /tmp/build-deps.sh
RUN chmod +x /tmp/build-deps.sh && /tmp/build-deps.sh && rm /tmp/build-deps.sh

# Set up Python venv for Conan/pre-commit
RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"
RUN pip install --upgrade pip && pip install conan pre-commit

# Configure Conan with Clang profile
RUN conan profile detect --force

# Library paths
ENV LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib:${LD_LIBRARY_PATH}"
ENV PKG_CONFIG_PATH="${INSTALL_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH}"

WORKDIR /workspace
