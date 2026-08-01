# LAPSE

**Language-Agnostic Playback Synchronization Engine**

Automatically fixes subtitle sync in your media library. Detects how far off your subtitles are and corrects them, including linear drift caused by framerate mismatches between the video and the subtitle file.

Built in C++ using FFmpeg for audio decoding, libfvad for voice activity detection, and FFTW3 for fast cross-correlation. Python handles orchestration, file watching, and state tracking via SQLite so the same file is never processed twice.

---

## how it works

1. Decodes audio from the video file using FFmpeg
2. Runs WebRTC voice activity detection to build a speech activity profile at 8kHz
3. Parses the SRT file and builds a matching dialogue activity profile from the timestamps
4. Cross-correlates the two profiles across 15 minute chunks using FFT, which finds the offset for each chunk in O(N log N) instead of the naive O(N²)
5. Fits a line through the per-chunk offsets using weighted linear regression to find both the constant offset and any linear drift
6. Applies the correction to the SRT file and keeps a .srt.bak of the original

---

## repo structure

```
lapse/
├── engine/       C++ source for the CLI binary
├── docker/       Python orchestrator, Dockerfile, compose file
└── .github/      CI workflows for Docker image and binary releases
```

---

## usage

### cli

Build the binary:

```bash
cd engine
g++ -O2 -o lapse main.cpp correlate.cpp decoder.cpp srt_parser.cpp \
    $(pkg-config --cflags --libs libavcodec libavformat libavutil libswresample) \
    -lfvad -lfftw3
```

Run it on a single file:

```bash
./lapse video.mkv subtitles.srt
```

This decodes the audio, runs VAD, finds the offset and drift, corrects the SRT file in place, and prints the result. No Python required.

Sync subtitles to another subtitle file:

```bash
./lapse reference.srt subtitles.srt
```

When the first argument is an SRT file, LAPSE skips audio decoding and aligns directly against the reference subtitle's timestamps instead. Faster and more accurate than aligning to audio.

### docker (experimental)

The Docker image includes the C++ binary and the Python file watcher. Point it at your media and it will scan on startup then watch for new files.

```yaml
services:
  lapse:
    image: ghcr.io/rs-jensen/lapse:latest
    restart: always
    volumes:
      - ./data:/data
      - /your/media:/media
    environment:
      - MEDIA_ROOT=/media
      - DB_PATH=/data/lapse.db
      - LAPSE_BIN=/app/lapse
```

```bash
docker compose up -d
```

LAPSE matches video files to SRT files by filename similarity within the same directory. It handles the usual naming differences from scene releases reasonably well. The first run is worth watching with `docker compose logs -f lapse` to make sure matches look correct.

---

## supported formats

**Video:** `.mp4`, `.mkv`, `.avi`

**Subtitles:** `.srt`

Planned to support most common video and subtitle formats.


Licensed under the GNU General Public License v3.0 — see [LICENSE](LICENSE) for details.
