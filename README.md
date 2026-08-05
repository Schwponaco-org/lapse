# LAPSE

**Language-Agnostic Playback Synchronization Engine**

Automatically fixes subtitle sync in your media library. Detects how far off your subtitles are and corrects them, including linear drift caused by framerate mismatches between the video and the subtitle file.

A Jellyfin plugin is available at [rs-jensen/lapse-jellyfin-plugin](https://github.com/rs-jensen/lapse-jellyfin-plugin) for direct integration with your media server.

Built in C++ using FFmpeg for audio decoding, libfvad for voice activity detection, and FFTW3 for fast cross-correlation. Python handles orchestration, file watching, and state tracking via SQLite so the same file is never processed twice.

---

## How it works

**No-split mode (default)**

1. Decodes audio from the video file using FFmpeg
2. Runs WebRTC voice activity detection to build a speech activity profile at 8kHz
3. Parses the subtitle file and extracts subtitle spans from the timestamps
4. Finds the single offset that maximises total overlap between subtitle spans and speech spans
5. Applies the correction and keeps a .bak of the original

**OLS mode**

Uses FFT cross-correlation across 15-minute chunks and fits a line through the per-chunk offsets using weighted linear regression. Handles both constant offset and linear drift from framerate mismatches. Less accurate than no-split for constant offsets but produces a slope and intercept that can correct gradual drift.

**Split mode**

If your subtitles were made for a different cut of the film, such as a director's cut or a version with ad breaks, the offset changes at multiple points throughout the file. No-split and OLS cannot handle this because they apply a single correction to the entire file.

Split mode uses a span alignment algorithm that finds the optimal offset per segment, allowing different parts of the subtitle file to be shifted by different amounts. A split penalty controls how aggressively the algorithm introduces new segments. Higher penalty means fewer splits.

---

## Repo structure

```
lapse/
├── engine/       C++ source for the CLI binary
├── docker/       Python orchestrator, Dockerfile, compose file
└── .github/      CI workflows for Docker image and binary releases
```

---

## Usage

### CLI

Build the binary:

```bash
cd engine
g++ -O2 -std=c++20 -o lapse main.cpp correlate.cpp decoder.cpp srt_parser.cpp write_subtitle.cpp \
    $(pkg-config --cflags --libs libavcodec libavformat libavutil libswresample) \
    -lfvad -lfftw3
```

Sync subtitles to a video file (no-split, default):

```bash
./lapse video.mkv subtitles.srt
```

Sync subtitles to another subtitle file (faster, more accurate):

```bash
./lapse reference.srt subtitles.srt
```

Sync with OLS mode for gradual framerate drift:

```bash
./lapse video.mkv subtitles.srt ols
```

Sync with split mode for director's cuts or ad-break versions:

```bash
./lapse video.mkv subtitles.srt split 6
```

The fourth argument is the split penalty. A value of 6 is a good default. Higher values produce fewer splits.

### Docker (experimental)

The Docker image includes the C++ binary and the Python file watcher. Point it at your media library and it will scan on startup then watch for new files.

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

LAPSE matches video files to subtitle files by filename similarity within the same directory. It handles the usual naming differences from scene releases reasonably well. The first run is worth watching with `docker compose logs -f lapse` to make sure matches look correct.

---


## Supported formats

**Video:** All formats supported by FFmpeg (`.mp4`, `.mkv`, `.avi`, `.mov`, `.ts`, `.webm` and more)

**Subtitles:** `.srt`, `.ass`, `.ssa`, `.vtt`

Planned to support most common video and subtitle formats.

---

Licensed under the GNU General Public License v3.0 -- see [LICENSE](LICENSE) for details.