# LAPSE

**Language-Agnostic Playback Synchronization Engine**

Automatically fixes subtitle sync in your media library. Detects how far off your subtitles are and corrects them, including linear drift caused by framerate mismatches between the video and the subtitle file.

A Jellyfin plugin is available at [rs-jensen/lapse-jellyfin-plugin](https://github.com/rs-jensen/lapse-jellyfin-plugin) for direct integration with your media server.

Built in C++ using FFmpeg for audio decoding, libfvad for voice activity detection, and FFTW3 for fast cross-correlation. Python handles orchestration, file watching, and state tracking via SQLite so the same file is never processed twice.

---

## How it works

**No-split mode (default)**

1. Builds a reference of when someone is talking, either from a subtitle track already inside the video file or from the audio
2. Parses the subtitle file and extracts subtitle spans from the timestamps
3. Finds the single offset that maximises total overlap between subtitle spans and speech spans
4. Applies the correction and keeps a .bak of the original

**Where the reference comes from**

If the video already carries a text subtitle track, LAPSE reads the cue timings straight out of it. Those timings are exact, so nothing has to be guessed and no audio is decoded at all. Forced and hearing impaired tracks are skipped and the track marked default is preferred. Pass `--no-embedded` to skip this and always use the audio.

Otherwise the audio is decoded with FFmpeg and turned into a speech profile at 8kHz:

- Dialogue usually sits on the centre channel of a 5.1 mix, but not always. LAPSE listens to the centre channel, the full downmix and the front pair over the first ten minutes, keeps whichever one looks most like conversation, and decodes the rest of the film from that one alone. A mix the detector is unsure about, or one that is talking nearly all of the time, loses to one it can call clearly.
- The level is tracked as the file plays and the signal is brought to a steady loudness before the detector sees it, so a quiet transfer is not treated differently from a loud one.
- Every 10ms frame gets a probability rather than a yes or no. Spans the detector was unsure about count for less when the offset is scored.

### Voice detection

LAPSE uses [Silero VAD](https://github.com/snakers4/silero-vad) when it can find it, and falls back to libfvad when it cannot. Silero is a small neural model that is far better at telling speech from music, singing and effects than the older detector, and it runs at roughly 400 times realtime, so a two hour film costs about 20 seconds.

Silero needs two files that are not built into the binary:

| File | Where LAPSE looks |
|---|---|
| `libonnxruntime.so` (`.dylib` on macOS) | `LAPSE_ONNXRUNTIME`, then next to the lapse binary, then the usual library paths |
| `silero_vad.onnx` | `LAPSE_VAD_MODEL`, then next to the lapse binary, then `/usr/local/share/lapse`, `/usr/share/lapse`, then the working directory |

Both ship as release assets and both are already inside the Docker image. Drop them next to the binary and they are picked up on their own.

If either one is missing LAPSE says so in one line and carries on with libfvad, which needs nothing extra. There it runs at all four aggressiveness settings and the number that agree becomes the probability, and stretches of steady unchanging level are pushed down so music and singing stay out of the reference.

**OLS mode**

For subtitles that drift, usually because they were timed for a different framerate than the video. It stretches the subtitle by each framerate ratio that actually gets shipped (23.976, 24, 25, 29.97 and the conversions between them), runs the no-split search for each one and keeps whichever ratio explained the most of the file.

If none of them fit, it falls back to FFT cross-correlation across 15-minute chunks with a weighted linear regression through the per-chunk offsets, which can pick up a stretch that is not a standard ratio.

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
g++ -O2 -std=c++20 -o lapse main.cpp correlate.cpp decoder.cpp srt_parser.cpp write_subtitle.cpp silero.cpp \
    $(pkg-config --cflags --libs libavcodec libavformat libavutil libswresample) \
    -lfvad -lfftw3 -ldl
```

ONNX Runtime is not needed to build. LAPSE looks for it when it runs and carries on without it, see [Voice detection](#voice-detection).

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

#### Output options

By default LAPSE overwrites the subtitle file it was given and leaves a `.bak` next to it. Two flags change that:

```
--output <path>     write the corrected subtitle to <path> instead of overwriting the input
--no-backup         do not create the .bak file
--no-embedded       ignore subtitle tracks inside the video and use the audio
```

Together they cover the four ways a caller may want the output handled:

| Result | Flags |
|---|---|
| Overwrite, keep a backup (default) | *neither flag* |
| Overwrite, no backup | `--no-backup` |
| Write a sidecar, leave the original alone | `--output out.srt --no-backup` |
| Write a sidecar and back up the original | `--output out.srt` |

```bash
./lapse reference.srt danish.srt --output danish.synced.srt --no-backup
```

The flags may appear anywhere on the command line. An existing `.bak` is never overwritten, so the first backup stays the untouched original no matter how many times you run LAPSE on a file.

#### Confidence

Every run prints a confidence between 0 and 1 alongside the offset. It is the share of the subtitle file that ended up sitting on speech, so around 1.0 means every cue found its place and below roughly 0.5 means the two files probably do not belong together. LAPSE still writes the file -- the number is there so the caller can decide whether to keep the result.

### Docker

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
      - PUID=1000
      - PGID=1000
```

```bash
docker compose up -d
```

Everything you mount gets scanned. To use more than one library, add another volume and list both paths:

```yaml
    volumes:
      - /your/movies:/movies
      - /your/tv:/tv
    environment:
      - MEDIA_ROOT=/movies,/tv
```

Set `PUID` and `PGID` to the user that owns your media. Without them the container runs as root and the files it writes end up owned by root.

#### Settings

| Variable | Default | What it does |
|---|---|---|
| `MEDIA_ROOT` | `/media` | Where to look. Comma separated for more than one library |
| `DB_PATH` | `/data/lapse.db` | Where the record of finished work is kept |
| `PUID` / `PGID` | unset | Run as this user and group |
| `MODE` | `nosplit` | `nosplit`, `ols` or `split` |
| `PENALTY` | `6` | Split penalty, only used in split mode |
| `SCAN_INTERVAL` | `900` | Seconds between full rescans. `0` turns them off |
| `MIN_CONFIDENCE` | `0` | Put the original back when a result scores below this. `0` keeps everything |
| `MAX_ATTEMPTS` | `3` | How many times a failing pair is retried before it is left alone |
| `TIMEOUT` | `1800` | Seconds a single sync may take |
| `POLLING` | `0` | Set to `1` on network shares where file events do not arrive |

#### Matching

Subtitles are matched to video by filename inside the same folder. If the subtitles sit in their own folder, like the `Subs` folder a lot of releases ship with, the folder above is checked as well.

Language and tag words such as `en`, `danish`, `forced` and `sdh` are ignored when names are compared, so a video can have several subtitle files matched to it at once. Episode numbers have to agree, so `S01E01` never picks up the subtitle for `S01E02`. When a folder holds one video, any subtitle in it belongs to that video.

New files are picked up as they arrive. A file is left alone until it stops changing, so a download that is still being written is not touched. Full rescans catch anything the file events missed, which is what `SCAN_INTERVAL` and `POLLING` are for on network storage.

Every sync is written to the database, so the same file is never done twice. Replacing a subtitle file with a new one gets it synced again.

The first run is worth watching with `docker compose logs -f lapse` to check that the matches look right.

---


## Supported formats

**Video:** All formats supported by FFmpeg (`.mp4`, `.mkv`, `.avi`, `.mov`, `.ts`, `.webm` and more)

**Subtitles:** `.srt`, `.ass`, `.ssa`, `.vtt`

Planned to support most common video and subtitle formats.

Run `lapse --formats` to print the subtitle formats your binary can read, one per line.

---

Licensed under the GNU General Public License v3.0 -- see [LICENSE](LICENSE) for details.