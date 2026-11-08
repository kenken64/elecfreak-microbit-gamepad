FROM python:3.12-slim-bookworm

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PLATFORMIO_CORE_DIR=/opt/platformio \
    PLATFORMIO_DISABLE_UPGRADE_CHECK=true \
    PLATFORMIO_DISABLE_PROGRESSBAR=true \
    GP_HOST=0.0.0.0 \
    GP_DATA_DIR=/data \
    GP_REQUIRE_AUTH=1 \
    PORT=8080

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential ca-certificates git libusb-1.0-0 srecord \
    && rm -rf /var/lib/apt/lists/* \
    && pip install --no-cache-dir platformio==6.2.0 \
    && pio settings set enable_telemetry No

WORKDIR /app

# Download the V1 compiler/framework into the image, outside the runtime volume.
COPY platformio.ini ./
RUN pio pkg install -e microbit_v1

COPY src/ ./src/
COPY softdevice/ ./softdevice/
COPY blocks/ ./blocks/
COPY tools/ ./tools/

# Verify the toolchain and prime the compiler cache during image creation.
# Only runtime builds are published to each user's persistent workspace.
RUN pio run -e microbit_v1

# The web process owns the mounted volume; compilers run under a separate UID
# without the web process's authentication secrets or access to user storage.
RUN useradd --create-home --uid 10001 builder \
    && chown -R builder:builder /app/.pio /opt/platformio
USER builder
RUN HOME=/home/builder pio run -e microbit_v1
USER root
ENV GP_BUILD_USER=builder

EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD python -c "import os, urllib.request; urllib.request.urlopen('http://127.0.0.1:' + os.environ.get('PORT', '8080') + '/healthz', timeout=3)"

CMD ["python", "tools/blocks_editor.py", "--no-browser"]
