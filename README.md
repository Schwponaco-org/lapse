# LAPSE

**Language-Agnostic Playback Synchronization Engine**

Automatically fixes subtitle sync in your media library. Detects how far off your subtitles are and corrects them, including linear drift caused by framerate mismatches between the video and the subtitle file.

A Jellyfin plugin is available at [rs-jensen/lapse-jellyfin-plugin](https://github.com/rs-jensen/lapse-jellyfin-plugin) for direct integration with your media server.

The `lapse` binary is a C++ engine built on FFmpeg, libfvad and FFTW3, and it syncs one file at a time. The Docker image wraps that engine with a Python watcher that scans your library, matches subtitles to video and keeps a SQLite record so nothing gets processed twice. Run the binary by hand for a single file, or run the container to keep a whole library synced on its own.

---

## Docs

[docs/benchmarks.md](docs/benchmarks.md) covers testing against alass and ffsubsync on 39 feature films picked to be hard to sync, with methodology and per-film results. Passed 36 of 39 in normal mode, 32 of 39 with splitting on, and median sync time on a cached film is 0.66 seconds.

---

## Install

### Download

Every release ships a self-contained archive per platform. Unpack it and run the binary, there is nothing to install:

| Platform | Archive | Runs on |
|---|---|---|
| Linux x86-64 | `lapse-linux-amd64.tar.gz` | glibc 2.31 and newer (Ubuntu 20.04+, Debian 11+) |
| Linux ARM64 | `lapse-linux-arm64.tar.gz` | Raspberry Pi 4/5, ARM servers, Apple silicon VMs |
| macOS Apple silicon | `lapse-macos-arm64.tar.gz` | macOS 11 Big Sur and newer |
| macOS Intel | `lapse-macos-x86_64.tar.gz` | macOS 10.15 Catalina and newer |
| Windows x64 | `lapse-windows-x64.zip` | Windows 10 and newer |

Each archive holds the binary, `libonnxruntime` (`onnxruntime.dll` on Windows), `silero_vad.onnx` and the licence. FFmpeg, libfvad and FFTW3 are linked into the binary, so nothing else has to be on the machine. `SHA256SUMS` covers every asset.

On Linux and macOS the binary needs the executable bit after unpacking:

```bash
tar xf lapse-linux-amd64.tar.gz
cd lapse-linux-amd64
chmod +x lapse
./lapse --vad
```

`--vad` prints which detector it found (`silero` or `libfvad`) and is the quickest way to tell that the archive landed intact.

The Windows x64 build also runs on Windows on ARM under emulation if the ARM64 one gives you trouble. Loading `onnxruntime.dll` on Windows needs the Microsoft Visual C++ 2015-2022 Redistributable, which most machines already have. Without it LAPSE says so and carries on with libfvad.

### Build

```bash
cmake -B build
cmake --build build -j
```

The binary lands in `build/lapse`. FFmpeg has to come from the system, everything else is used if it is installed and built inside `build/` if it is not, so on a machine that only has FFmpeg the two commands above are the whole story. Nothing is written outside the build directory.

| | |
| --- | --- |
| macOS | `brew install cmake ffmpeg pkg-config` |
| Debian, Ubuntu | `sudo apt install cmake pkg-config libavformat-dev libavcodec-dev libavutil-dev libswresample-dev` |
| Fedora | `sudo dnf install cmake pkgconf-pkg-config ffmpeg-devel` |
| Arch | `sudo pacman -S cmake ffmpeg pkgconf` |
| MSYS2 | `pacman -S $MINGW_PACKAGE_PREFIX-{cmake,ffmpeg,pkgconf,toolchain}` |

Installing libfftw3 and libfvad as well is fine and slightly faster, the build picks them up instead. `cmake --build build` prints where each dependency came from. `ctest --test-dir build` runs the same smoke test CI runs, and `cmake --install build` puts the binary in `/usr/local/bin` and nothing else anywhere.

Useful options: `-DLAPSE_FETCH_MISSING=OFF` to fail instead of downloading anything, `-DLAPSE_STATIC_DEPS=ON` to link the dependencies statically, `-DLAPSE_DEPS_PREFIX=<prefix>` to build against a prefix from the scripts below, `-DCMAKE_BUILD_TYPE=Debug` for a build worth stepping through.

To reproduce a release build instead, where FFmpeg, FFTW3 and libfvad are all static and minimal and the binary depends on nothing outside the platform itself:

```bash
.github/scripts/build-ffmpeg.sh "$HOME/deps"
.github/scripts/build-fftw.sh "$HOME/deps"
.github/scripts/build-libfvad.sh "$HOME/deps"
cmake -B build-release -DLAPSE_DEPS_PREFIX="$HOME/deps"
cmake --build build-release -j
```

That is what CI does, on Linux, on macOS with the Xcode command line tools, and on Windows inside an MSYS2 MINGW64 shell. Set `MACOSX_DEPLOYMENT_TARGET` on macOS to pick the oldest system the binary should run on. `$HOME/deps` is a throwaway prefix -- delete it when the build is done.

ONNX Runtime is not needed to build. LAPSE looks for it when it runs (`LAPSE_ONNXRUNTIME` env var, then next to the binary, then the usual library paths) and falls back to libfvad if it is missing.

---

## Docker

The Docker image includes the C++ binary and the Python file watcher. Point it at your media library and it will scan on startup then watch for new files.

```yaml
services:
  lapse:
    image: ghcr.io/schwponaco-org/lapse:latest
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

### Web interface

Publish port `8080` and open `http://your-host:8080`. There is no login, so keep it on your own network or behind whatever you already put in front of the rest of your stack.

```yaml
    ports:
      - 8080:8080
```

From there you can:

- **Pause and resume.** A paused container still watches for new files, it just does not sync anything until you let it go again. The setting survives a restart.
- **Undo.** Puts the original subtitle back from its `.bak` and leaves that file alone from then on. Select several and undo them in one go.
- **Sync again.** Forget what is on record for the files you picked and let them run through again, or do the whole library at once.
- **Set the rescan interval** without editing the compose file.
- **Leave folders out.** Everything you mounted is listed, one level down. Untick what you do not want touched and the scanner walks past it. Paths deeper than that can be typed in.

What you change here is kept in the database and wins over the environment variables it overlaps with, so `SCAN_INTERVAL` is the starting point rather than the last word.

| Variable | Default | What it does |
|---|---|---|
| `WEB` | `1` | Set to `0` to run without the web interface |
| `WEB_PORT` | `8080` | Port inside the container |

### Settings

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
| `CACHE_DAYS` | `30` | Days a cached audio scan is kept before it is deleted. `0` keeps them forever |

### Engine settings

Everything the CLI takes is available in the container. Switches are `0` or `1`, the rest are left empty to use the engine default.

| Variable | Flag | What it does |
|---|---|---|
| `OUTPUT_SUFFIX` | `--output` | Write to `name<suffix>.srt` and never touch the original. `.synced` gives `movie.synced.srt` |
| `NO_BACKUP` | `--no-backup` | Do not leave a `.bak` beside the file that was overwritten |
| `NO_SIDECAR` | `--no-sidecar` | Do not write a `.lapse-unsure` guess when the result is uncertain |
| `NO_EMBEDDED` | `--no-embedded` | Ignore subtitle tracks inside the video when picking a reference |
| `NO_CACHE` | `--no-cache` | Listen to the video every time instead of reusing a cached scan |
| `FULL_SCAN` | `--full-scan` | Listen to the whole video rather than sampling it |
| `FORCE` | `--force` | Sync even when there is little to go on |
| `STRICT` | `--strict` | Only overwrite when the result is clearly right |
| `DRY_RUN` | `--dry-run` | Work out every offset and write nothing, not even to the database |
| `CONFIDENCE` | `--confidence` | How far the answer has to stand out before the original is overwritten |
| `AUDIO_TRACK` | `--audio-track` | Which audio track to listen to |
| `SUB_TRACK` | `--sub-track` | Which embedded subtitle track to use as reference |
| `FPS` | `--fps` | Frame rate for frame based subtitles that do not carry one |

`OUTPUT_SUFFIX` is the way to keep a library untouched. The original stays as it is, the synced copy lands beside it, and files the container wrote itself are skipped on later scans. Pair it with `NO_BACKUP=1` if you would rather not collect `.bak` files as well.

`DRY_RUN=1` is worth a first pass over a large library. The log shows what every file would get without anything being written or recorded, so a second run with it off starts from scratch.

Cached audio scans are kept in a `cache` folder next to the database so they survive a restart, and anything unused for `CACHE_DAYS` is deleted. Set `LAPSE_CACHE` to put them somewhere else.

### Matching

Subtitles are matched to video by filename inside the same folder. If the subtitles sit in their own folder, like the `Subs` folder a lot of releases ship with, the folder above is checked as well.

Language and tag words such as `en`, `danish`, `forced` and `sdh` are ignored when names are compared, so a video can have several subtitle files matched to it at once. Episode numbers have to agree, so `S01E01` never picks up the subtitle for `S01E02`. When a folder holds one video, any subtitle in it belongs to that video.

New files are picked up as they arrive. A file is left alone until it stops changing, so a download that is still being written is not touched. Full rescans catch anything the file events missed, which is what `SCAN_INTERVAL` and `POLLING` are for on network storage.

Every sync is written to the database, so the same file is never done twice. Replacing a subtitle file with a new one gets it synced again.

The first run is worth watching with `docker compose logs -f lapse` to check that the matches look right.

---

## CLI usage

Sync subtitles to a video file (no-split, default):

```bash
./lapse video.mkv subtitles.srt
```

Take the subtitles out of the video, sync them, and leave them next to it as `video.lapse.srt`:

```bash
./lapse video.mkv
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

The fourth argument is the split penalty. A value of 6 is a good default. Higher values produce fewer splits. Leave the mode off and LAPSE decides for itself whether the file needs shifting, drifting, splitting, or some combination (`auto` mode)

### Output options

By default LAPSE overwrites the subtitle file it was given and leaves a `.bak` next to it. Two flags change that:

```
--output <path>     write the corrected subtitle to <path> instead of overwriting the input
--no-backup         do not create the .bak file
--no-embedded       ignore subtitle tracks inside the video and use the audio
--full-scan         accepted and ignored, the whole film is always listened to
--no-sidecar        write nothing at all rather than an unsure file beside the original
--no-cache          do not read or write the saved speech profile
--force             overwrite the input whatever the verdict is
--strict            refuse instead of writing an unsure result beside the original
--dry-run           work out the answer but write nothing
--confidence N      how far the answer has to stand out before the original is overwritten, default 8
--json              one machine readable line on stdout, everything else on stderr
--quiet             say nothing but errors
--audio-track N     use the Nth audio track instead of the default one
--sub-track N       use the Nth embedded subtitle track as the reference
```

`--undo <subtitle>` puts the `.bak` back and removes it.

Three flags answer a question and exit instead of syncing anything. `--version` prints the version number, `--formats` lists the subtitle extensions the engine reads and writes, and `--vad` prints `silero` or `libfvad` depending on which detector this copy can reach, exiting non-zero on the fallback.

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

### Verdicts

Every run reports a verdict, and it decides what happens to your file:

| Verdict | What it means | What happens | Exit |
|---|---|---|---|
| `solid` | the answer clears the bar | the subtitle is overwritten, `.bak` kept | 0 |
| `unsure` | there is an answer, it is just not proven | original untouched, answer written to `name.lapse-unsure.srt` | 3 |
| `nothing` | the audio does not support any offset | original untouched, best guess written to `name.lapse-unsure.srt` | 3 |

LAPSE never simply refuses. If it cannot prove an answer it still writes one, it just puts it in a sidecar file and leaves yours alone. `--no-sidecar` writes nothing at all instead. `--confidence` moves the bar for what counts as `solid`; `--force` overwrites regardless; `--strict` writes nothing rather than a sidecar.

### Machine readable output

`--json` puts one line on stdout and moves every other message to stderr:

```json
{"mode":"auto/shifted","reference":"vad","offset_ms":22,"ratio":1,"confidence":0.455,
 "margin":0.12,"sigma":12.3,"agreement":0.75,"verdict":"solid","coverage":1,
 "cues":1578,"ignored_cues":1,"parts":1,"written":true,"output":"...","splits":[]}
```

`mode` says what LAPSE decided the file needed. `ols`, `nosplit` and `split` mean you asked for that yourself. Everything under `auto` is what it worked out on its own:

| `mode` | What it found | What moved |
|---|---|---|
| `auto/shifted` | one offset fits the whole file | `offset_ms` |
| `auto/shifted+split` | one offset nearly fits, with cuts in it | `offset_ms`, then `splits` |
| `auto/drifting` | the file drifts, usually a framerate mismatch | `ratio` and `offset_ms` |
| `auto/drifting+split` | it drifts and was cut about as well | `ratio`, then `splits` |
| `auto/recut` | the offset changes throughout | `splits` |
| `auto/joined` | two parts in one video | `splits` |
| `auto/restart` | the subtitle starts over partway through | `splits` |

`parts` is how many pieces the file ended up in and `splits` holds the cue index each new piece starts at, so `parts` is always `splits` plus one. `ratio` is `1` unless the file was stretched.

---

## Repo structure

```
lapse/
├── engine/       C++ source for the CLI binary
├── docker/       Python orchestrator, web interface, Dockerfile, compose file
├── docs/         Benchmarks and other reference docs
└── .github/      CI workflows, and the scripts that build a release binary
```

---

## Supported formats

**Video:** All formats supported by FFmpeg (`.mp4`, `.mkv`, `.avi`, `.mov`, `.ts`, `.webm` and more)

**Subtitles:** `.srt`, `.ass`, `.ssa`, `.vtt`, `.sub` (MicroDVD), `.sup` (PGS), `.sbv`, `.idx` (VobSub, point it at the `.idx` file), `.smi`, `.ttml`, `.dfxp`

**Embedded tracks:** `lapse video.mkv` on its own pulls the default subtitle track out of the container and syncs it, see [CLI usage](#cli-usage) above. This covers text tracks, including `mov_text`. PGS and VobSub tracks are bitmap subtitles with no text to pull out, but when one is present LAPSE reads its timing and uses it as a fast, accurate sync reference instead of falling back to decoding the audio.

Run `lapse --formats` to print the subtitle extensions your binary can read, one per line

---

Licensed under the GNU General Public License v3.0 -- see [LICENSE](LICENSE) for details
