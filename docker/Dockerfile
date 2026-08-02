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
    g++ pkg-config \
    git automake autoconf libtool \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/dpirch/libfvad.git /tmp/libfvad && \
    cd /tmp/libfvad && \
    autoreconf -i && \
    ./configure && \
    make && \
    make install && \
    ldconfig && \
    rm -rf /tmp/libfvad

WORKDIR /app
COPY docker/requirements.txt .
RUN pip3 install -r requirements.txt --break-system-packages
COPY engine/correlate.cpp engine/correlate.h engine/decoder.cpp engine/decoder.h \
     engine/srt_parser.cpp engine/srt_parser.h engine/main.cpp \
     engine/write_subtitle.cpp engine/write_subtitle.h .
RUN g++ -O2 -std=c++20 -o lapse main.cpp correlate.cpp decoder.cpp srt_parser.cpp write_subtitle.cpp \
    -lavcodec -lavformat -lavutil -lswresample -lfvad -lfftw3
COPY docker/main.py .
CMD ["python3", "main.py"]
