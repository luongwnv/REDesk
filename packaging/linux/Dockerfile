# REDesk — Ubuntu build image (ADR-001 §5 packaging).
#
# Builds the Qt UI + engine and produces a .deb. This first Linux build uses the
# portable stub backends (REDESK_USE_REAL_BACKENDS=OFF) so it compiles without
# the full PipeWire/VAAPI/libei/portal stack — the app runs with the Qt UI, the
# REDesk icon, and the synthetic-capture preview. Wiring the real Linux capture/
# input backends (like the macOS ScreenCaptureKit backend) is a later phase.
#
# Usage (from repo root):
#   docker build -f packaging/linux/Dockerfile -t redesk-linux .
#   docker run --rm -v "$PWD/dist:/out" redesk-linux   # copies the .deb to ./dist
#
# Ubuntu 24.04 ships Qt 6.4; we add the qt6 packages from the archive.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config file \
        qt6-base-dev qt6-declarative-dev qt6-declarative-private-dev \
        qml6-module-qtquick qml6-module-qtquick-controls \
        qml6-module-qtquick-templates \
        qml6-module-qtquick-layouts qml6-module-qtquick-window \
        qml6-module-qtqml-workerscript \
        libgl1-mesa-dev \
        libsodium-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

# Configure + build. UI links Qt; backends are stubs (OFF) for this first build.
RUN cmake -S . -B build-linux -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DREDESK_USE_REAL_BACKENDS=OFF \
    && cmake --build build-linux -j"$(nproc)"

# Stage an install tree and build a .deb with CPack.
RUN cmake --install build-linux --component gui --prefix /opt/redesk-stage 2>/dev/null || true \
    && cd build-linux && cpack -G DEB 2>/dev/null || true

# Default: copy any produced packages + the bare binary to the mounted /out.
CMD bash -c '\
    mkdir -p /out && \
    cp -v build-linux/*.deb /out/ 2>/dev/null || true ; \
    cp -v build-linux/bin/REDesk /out/REDesk-linux-bin 2>/dev/null || true ; \
    ls -la /out'
