# LAPSE - Language-Agnostic subtitle synchronization engine
# Copyright (C) 2026 Rasmus Stisen Jensen (rs-jensen)
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y \
    python3 python3-pip \
    ffmpeg \
    libavcodec-dev libavformat-dev libavutil-dev libswresample-dev \
    libfftw3-dev \
    g++ cmake pkg-config git \
    wget ca-certificates \
    gosu \
    && rm -rf /var/lib/apt/lists/*

ENV PYTHONUNBUFFERED=1
ENV MEDIA_ROOT=/media
ENV DB_PATH=/data/lapse.db
ENV LAPSE_BIN=/app/lapse

WORKDIR /app
COPY docker/requirements.txt .
RUN pip3 install -r requirements.txt --break-system-packages
# libfvad is not packaged for Debian, cmake fetches and links it in statically
COPY CMakeLists.txt ./
COPY engine ./engine
RUN cmake -B build && \
    cmake --build build -j "$(nproc)" && \
    cp build/lapse /app/lapse && \
    rm -rf build engine CMakeLists.txt

ARG ORT_VERSION=1.28.0
RUN set -eux; \
    case "$(dpkg --print-architecture)" in \
      amd64) ORT_ARCH=x64 ;; \
      arm64) ORT_ARCH=aarch64 ;; \
      *) ORT_ARCH="" ;; \
    esac; \
    if [ -n "$ORT_ARCH" ]; then \
      wget -q -O /tmp/ort.tgz "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-${ORT_ARCH}-${ORT_VERSION}.tgz" && \
      tar xf /tmp/ort.tgz -C /tmp && \
      cp /tmp/onnxruntime-linux-${ORT_ARCH}-${ORT_VERSION}/lib/libonnxruntime.so.${ORT_VERSION} /app/libonnxruntime.so && \
      wget -q -O /app/silero_vad.onnx "https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx" && \
      rm -rf /tmp/ort.tgz /tmp/onnxruntime-linux-*; \
    fi
COPY docker/main.py docker/web.py docker/index.html ./
COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
CMD ["python3", "main.py"]