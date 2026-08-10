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
3. Cross-correlates the two, checks the best handful of offsets against the audio a second way, and keeps whichever one survives that
4. Applies the correction and keeps a .bak of the original

**Where the reference comes from**

If the video already carries a text subtitle track, LAPSE reads the cue timings straight out of it. Those timings are exact, so nothing has to be guessed and no audio is decoded at all. Forced and hearing impaired tracks are skipped and the track marked default is preferred. Pass `--no-embedded` to skip this and always use the audio.

Otherwise the audio is decoded with FFmpeg and turned into a speech profile at 16kHz. The film is cut into ten minute stretches and as many of them are decoded and run through the detector at once as there are cores, so listening to all of a three hour film costs about fifteen seconds rather than the two minutes it used to. The stretches are ten minutes each on every machine on purpose. They used to be one per core, and because each stretch starts the detector from nothing, the warm-up landed at different timestamps on a four core machine than on a sixteen core one and the same film came out with two different profiles. Now only the number of stretches in flight follows the core count, so the same file gives the same answer everywhere and a benchmark or a bug report means something. LAPSE used to sample thirty windows instead of sitting through the whole thing; it no longer does, because nine percent of the audio is nine percent of the evidence and the sampling was costing more accuracy than it was saving time. `--full-scan` is still accepted and is now the only behaviour.

A file that was joined out of parts, two episodes in one video or a recording whose ad breaks were cut out, needs a different offset either side of the seam, and the difference is minutes rather than the seconds a recut moves things. `auto` notices when the slices disagree that far apart, finds where the change happens and measures each part on its own.

What it found gets written under a key built from the video's path, size and modified time, so the next subtitle track for the same film starts from the answer instead of decoding again. That goes to `$XDG_CACHE_HOME/lapse` or `~/.cache/lapse` on Linux and macOS, and `%LOCALAPPDATA%\lapse\cache` on Windows. Set `LAPSE_CACHE` to put it somewhere else, or pass `--no-cache` to skip it.

The profile itself is built like this:

- Dialogue usually sits on the centre channel of a 5.1 mix, but not always. LAPSE listens to the centre channel, the full downmix and the front pair over three minutes taken from a third of the way in, keeps whichever one looks most like conversation, and decodes the rest of the film from that one alone. The sample used to come from the opening ten minutes, which on a lot of films is titles and score rather than talking. A mix the detector is unsure about, or one that is talking nearly all of the time, loses to one it can call clearly.
- The level is tracked as the file plays and the signal is brought to a steady loudness before the detector sees it, so a quiet transfer is not treated differently from a loud one.
- Every 10ms frame gets a probability rather than a yes or no. Spans the detector was unsure about count for less when the offset is scored.

**Finding the offset**

Sliding the cues past the speech one pair at a time is `cues x spans` work, and on a long film that is forty million events to sort -- it was where the hundred second runs went. One cross correlation answers every offset at once instead.

Two things came out of being able to afford that. The first is that both signals get a half minute running mean taken out of them before they are correlated. A plain overlap score asks how many cues landed on speech, and that is highest wherever the film talks most whether or not anything lines up, so a film with a quiet half hour and a busy one grows a hill in the score that has nothing to do with sync and the real spike ends up on the side of it. Taking the running mean out leaves only the part that is about lining up.

The second is that there is no longer one candidate. The correlation is read four ways -- sharp, blurred, onsets only, blocks only -- because a subtitle that sits right on the word and one that was typed a beat early all the way through do not show up in the same place. Every candidate that falls out of that, plus leaving the file exactly where it is, then gets checked against the audio by the measure described under **Whether to believe it** below, and the one that survives is the answer.

### Voice detection

LAPSE uses [Silero VAD](https://github.com/snakers4/silero-vad) when it can find it, and falls back to libfvad when it cannot. Silero is a small neural model that is far better at telling speech from music, singing and effects than the older detector, and it is handed eight stretches of the film at a time rather than one window at a time, which is most of the reason a two hour film costs a few seconds rather than half a minute.

Silero needs two files that are not built into the binary:

| File | Where LAPSE looks |
|---|---|
| `libonnxruntime.so`, `libonnxruntime.dylib` on macOS, `onnxruntime.dll` on Windows | `LAPSE_ONNXRUNTIME`, then next to the lapse binary, then the usual library paths |
| `silero_vad.onnx` | `LAPSE_VAD_MODEL`, then next to the lapse binary, then `/usr/local/share/lapse` and `/usr/share/lapse` on Linux and macOS, then the working directory |

Both are in every release archive and both are already inside the Docker image. Drop them next to the binary and they are picked up on their own. `lapse --vad` says which detector a given setup ends up with.

If either one is missing LAPSE says so in one line and carries on with libfvad, which needs nothing extra. There it runs at all four aggressiveness settings and the number that agree becomes the probability, and stretches of steady unchanging level are pushed down so music and singing stay out of the reference.

**OLS mode**

For subtitles that drift, usually because they were timed for a different framerate than the video. It stretches the subtitle by each framerate ratio that actually gets shipped (23.976, 24, 25, 29.97 and the conversions between them), runs the no-split search for each one and keeps whichever ratio explained the most of the file.

If none of them fit, it falls back to FFT cross-correlation across 15-minute chunks with a weighted linear regression through the per-chunk offsets, which can pick up a stretch that is not a standard ratio.

**Split mode**

If your subtitles were made for a different cut of the film, such as a director's cut or a version with ad breaks, the offset changes at multiple points throughout the file. No-split and OLS cannot handle this because they apply a single correction to the entire file.

Split mode uses a span alignment algorithm that finds the optimal offset per segment, allowing different parts of the subtitle file to be shifted by different amounts. A split penalty controls how aggressively the algorithm introduces new segments. Higher penalty means fewer splits.

**Drifting and cut about at once**

A subtitle can be both, and used to have to be one or the other. A file timed for 25fps played back at 23.976 that also had its ad breaks taken out drifts steadily and jumps at every break, and stretching it alone leaves every break wrong while splitting it alone leaves every cue between the breaks wrong.

`auto` now takes the drift out first and then looks at what is left. If the stretched file still jumps far enough, often enough, to be a real cut rather than a hard film, the two are fixed together and the mode comes back as `auto/drifting+split`. The stretch has to have earned its place first, so a file that only needed sliding is never stretched to buy a split.

One cue at each seam still keeps the wrong side's offset. Cues either side of a cut overlap once they are sorted, and `auto` welds overlapping cues into one span, so the pair across the seam cannot come apart. This is not new and `auto/shifted+split` has always done it too.

---

## Repo structure

```
lapse/
├── engine/       C++ source for the CLI binary
├── docker/       Python orchestrator, Dockerfile, compose file
└── .github/      CI workflows, and the scripts that build a release binary
```

---

## Usage

### Download

Every release ships a self-contained archive per platform. Unpack it and run the binary, there is nothing to install:

| Platform | Archive | Runs on |
|---|---|---|
| Linux x86-64 | `lapse-linux-amd64.tar.gz` | glibc 2.31 and newer (Ubuntu 20.04+, Debian 11+) |
| Linux ARM64 | `lapse-linux-arm64.tar.gz` | Raspberry Pi 4/5, ARM servers, Apple silicon VMs |
| macOS Apple silicon | `lapse-macos-arm64.tar.gz` | macOS 11 Big Sur and newer |
| macOS Intel | `lapse-macos-x86_64.tar.gz` | macOS 10.15 Catalina and newer |
| Windows x64 | `lapse-windows-x64.zip` | Windows 10 and newer |
| Windows ARM64 | `lapse-windows-arm64.zip` | Windows 11 on ARM |

Each archive holds the binary, `libonnxruntime` (`onnxruntime.dll` on Windows), `silero_vad.onnx` and the licence. FFmpeg, libfvad and FFTW3 are linked into the binary, so nothing else has to be on the machine. `SHA256SUMS` covers every asset.

On Linux and macOS the binary needs the executable bit after unpacking:

```bash
tar xf lapse-linux-amd64.tar.gz
cd lapse-linux-amd64
chmod +x lapse
./lapse --vad
```

`--vad` prints which detector it found and is the quickest way to tell that the archive landed intact. It says `silero` when the ONNX Runtime beside it loaded, `libfvad` when it did not.

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

That is what CI does, on Linux, on macOS with the Xcode command line tools, and on Windows inside an MSYS2 MINGW64 or CLANGARM64 shell. The three scripts exist because those are cut-down static builds -- FFmpeg with `--disable-everything` and a hand-picked codec list -- and none of that is expressible in CMake, since FFmpeg has no CMake build at all. Set `MACOSX_DEPLOYMENT_TARGET` on macOS to pick the oldest system the binary should run on. `$HOME/deps` is a throwaway prefix, those libraries have no business in `/usr/local` where every other program would find them, so delete it when the build is done.

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

A sync that has to listen to a whole film takes long enough to look like a hung program, so the slow parts say how far along they are on stderr. On a terminal it is one line that redraws itself, and anywhere else, a log or a pipe, it prints every tenth of the way instead so it does not fill the file. `--quiet` and `--json` turn it off with everything else.

Two flags answer a question and exit instead of syncing anything. `--formats` lists the subtitle extensions the engine reads and writes, and `--vad` prints `silero` or `libfvad` depending on which detector this copy can reach, exiting non-zero on the fallback.

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

#### Whether to believe it

The subtitle is cut into eight stretches and each one is lined up on its own, without knowing what the others found. If the answer is right they all land on it. If it is wrong they scatter, because there is nothing there for them to agree about.

That is the number LAPSE trusts, and it decides what happens to your file:

| Verdict | What it means | What happens | Exit |
|---|---|---|---|
| `solid` | the answer clears the bar and three or more stretches back it | the subtitle is overwritten, `.bak` kept | 0 |
| `unsure` | there is an answer, it is just not proven | original untouched, answer written to `name.lapse-unsure.srt` | 3 |
| `nothing` | the audio does not support any offset | original untouched, best guess written to `name.lapse-unsure.srt` | 3 |

LAPSE never simply refuses any more. If it cannot prove an answer it still writes one, it just puts it in a file of its own and leaves yours where it was, so you can drop the sidecar in and look for yourself. `--no-sidecar` brings back the old behaviour of writing nothing at all.

A stretch that never locked lands anywhere in the half hour being searched, so hitting within 400 ms of the answer by luck is about one in two thousand. Three of them doing it at once is not a coincidence -- which is why a wrong offset cannot buy its way to `solid`, however tall its peak looks.

The bar itself is 8, which is what this corpus liked best, and `--confidence` moves it. Lower it and LAPSE will overwrite files it is less sure about, raise it and more of them come back as a sidecar instead. It only decides what happens to your file, never what the answer is: a result still needs three stretches behind it before anything is overwritten, so no threshold makes LAPSE write a guess over your subtitle.

`--force` overwrites regardless. `--strict` and `--no-sidecar` go the other way and write nothing rather than leaving a sidecar behind.

`sigma` is the other half of it, and it is not a property of the search. Once an offset is on the table, LAPSE measures two things the search never optimised -- how bunched the gaps are between a cue starting and somebody starting to talk, and how much of the subtitle lands on speech at all -- and compares both against the same subtitle dropped at a hundred offsets picked out of a hat. `sigma` is how many standard deviations clear of that the answer is. On a corpus of twenty three films a real lock sits between 8 and 25, and a film whose audio simply does not support its subtitle sits under 6. `confidence` is the old share-of-cues-on-speech figure, kept because it is useful for comparing runs on the same film.

#### Machine readable output

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

`parts` is how many pieces the file ended up in and `splits` holds the cue index each new piece starts at, so `parts` is always `splits` plus one. `ratio` is `1` unless the file was stretched, and since `auto/drifting+split` stretches *and* cuts, that is the one case where `ratio` is not `1` and `splits` is not empty at the same time.

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

Planned to support: `.sup`, `.idx/.sub`, `.sub`, `.smi`, `.ttml`/`.dfxp`, `.sbv`, embedded MKV/MP4 tracks via mkvmerge/ffmpeg.

Run `lapse --formats` to print the subtitle formats your binary can read, one per line.

---

Licensed under the GNU General Public License v3.0 -- see [LICENSE](LICENSE) for details
