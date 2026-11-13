# LAPSE

**Language-Agnostic Playback Synchronization Engine**

LAPSE fixes subtitles that are out of sync with the video. It listens to the film, finds where people are talking, and moves the subtitle so every line comes up with the speech it belongs to. Nothing in it depends on the language, so it works the same for a Danish subtitle on a Danish film as for an English one on a Japanese film.

It fixes a subtitle that is early or late by the same amount all the way through, one that slowly drifts further out because it was made for a different frame rate, and one that was made for a different cut of the film, with scenes or ad breaks in other places.

You can run it four ways:

- **`lapse`**, a single program you run on one file at a time, or on a whole list of them with `--batch`.
- **The Docker image**, which watches your media library, pairs every subtitle with its video and keeps the lot in sync, with a web page to see and undo what it did.
- **The [Jellyfin plugin](https://github.com/Schwponaco-org/lapse-jellyfin-plugin)**, which runs the same engine from inside Jellyfin.
- **The [terminal interface](https://github.com/Schwponaco-org/lapse-tui)**, which runs the `lapse` you have installed, for a server over ssh or anyone who would rather stay in the terminal. It runs on Linux and macOS.

There is also a copy in the browser at [schwponaco.org/lapse](https://schwponaco.org/lapse/). It is the same engine built for WebAssembly and runs in the tab, so the video never leaves your machine. It is handy for a file or two. For a whole library the Docker image fits better, since it keeps track of what it already synced.

---

## Quick start

```bash
./lapse movie.mkv movie.en.srt
```

LAPSE works out what is wrong with `movie.en.srt`, fixes it, and keeps the original next to it as `movie.en.srt.bak`. When it cannot prove its answer, it leaves your file alone and writes the guess to `movie.en.lapse-unsure.srt` so you can look at it.

```bash
./lapse movie.mkv
```

With no subtitle given, LAPSE takes the subtitle track out of the video, syncs it to the sound, and writes it next to the video as `movie.lapse.srt`.

---

## What it can fix

| Problem | What it looks like | What LAPSE does |
|---|---|---|
| Shifted | Every line is early or late by the same amount | Finds the one offset, up to 15 minutes either way |
| Drifting | The lines start fine and get further out, because the subtitle was timed to 25 fps and the video runs at 23.976, or the other way round | Stretches the subtitle back. The usual frame rate pairs are tried first, and anything else between 0.94 and 1.06 is searched for |
| Cut differently | Part of the film was cut out or added, the subtitle was made for a TV broadcast with ad breaks, or for the director's cut | Splits the subtitle into parts and gives each part its own offset |
| Starts over | Two episodes in one file, or a subtitle that restarts part way through | Lines each part up on its own |
| Another release | You have the release the subtitle was made for, and want it on a different one | Compares the sound of the two videos, which is more to go on than the subtitle itself (`--from`) |

It can sync against the sound of the video, against a subtitle track inside the video, or against another subtitle file that is already right. Sound effects in brackets, music notes, and the credit lines subtitle sites add, such as `Synced and corrected by`, are left out of the measuring. They still move with everything around them.

### How it works

1. **Listening.** The audio is decoded in pieces, several at once, and run through [Silero VAD](https://github.com/snakers4/silero-vad), a small speech detection model, through onnxruntime. If onnxruntime is missing, LAPSE uses libfvad instead. On a 5.1 track it tries the centre channel, the front pair and a plain downmix, and keeps whichever sounds most like speech.
2. **Lining up.** The subtitle and the speech are turned into two signals each, one for where talking is and one for where it starts, and cross correlated with FFTW. A film that talks a lot in one half and very little in the other would pull the answer towards the busy half, so slow changes in how much talking there is are taken out of both signals first.
3. **Deciding what is wrong.** The subtitle is also checked in eight slices. When they all agree, it is a shift. When they lie on a line, it drifts. When they land in groups, it was cut, and a dynamic programming search finds where the cuts are. Every split has to pay a price that grows with the size of the file, so a file is only cut up when that is clearly better.
4. **Judging the answer.** The answer gets a sigma, which says how far it stands out from the noise. It is only written over your file when it stands out clearly and enough of the slices back it up.
5. **Remembering.** The speech found in a video is cached. The next subtitle for the same video starts from it and takes about a second.

---

## Install

### Download

Every release has a ready to run archive for each platform. Unpack it and run the program. There is nothing to install.

| Platform | Archive | Runs on |
|---|---|---|
| Linux x86-64 | `lapse-linux-amd64.tar.gz` | any x86-64 CPU, glibc 2.35 and newer (Ubuntu 22.04+, Debian 12+) |
| Linux ARM64 | `lapse-linux-arm64.tar.gz` | Raspberry Pi 4/5, ARM servers, Apple silicon VMs |
| Alpine x86-64 | `lapse-linux-musl-amd64.tar.gz` | Alpine 3.20 and newer, and other musl systems |
| Alpine ARM64 | `lapse-linux-musl-arm64.tar.gz` | Alpine 3.20 and newer, and other musl systems |
| macOS Apple silicon | `lapse-macos-arm64.tar.gz` | macOS 11 Big Sur and newer |
| macOS Intel | `lapse-macos-x86_64.tar.gz` | macOS 10.15 Catalina and newer |
| Windows x64 | `lapse-windows-x64.zip` | Windows 10 and newer |

Each archive holds the program, `libonnxruntime` (`onnxruntime.dll` on Windows), `silero_vad.onnx` and the licence. FFmpeg, libfvad and FFTW3 are built into the program. `SHA256SUMS` covers every file in the release.

On Linux and macOS the program needs to be made executable after unpacking:

```bash
tar xf lapse-linux-amd64.tar.gz
cd lapse-linux-amd64
chmod +x lapse
./lapse --vad
```

`--vad` prints `silero` or `libfvad`, depending on which speech detector it found, and is a quick way to check the archive is complete.

The Windows x64 build also runs on Windows on ARM, under emulation. Loading `onnxruntime.dll` needs the Microsoft Visual C++ 2015-2022 Redistributable, which most machines already have. Without it LAPSE says so and uses libfvad.

### Build from source

```bash
cmake -B build
cmake --build build -j
```

The program ends up in `build/lapse`. FFmpeg has to come from the system. FFTW3 and libfvad are used if they are installed and built inside `build/` if they are not, so on a machine with FFmpeg those two commands are all it takes. Nothing is written outside the build folder.

| System | FFmpeg and the build tools |
| --- | --- |
| macOS | `brew install cmake ffmpeg pkg-config` |
| Debian, Ubuntu | `sudo apt install cmake pkg-config libavformat-dev libavcodec-dev libavutil-dev libswresample-dev` |
| Fedora | `sudo dnf install cmake pkgconf-pkg-config ffmpeg-devel` |
| Arch | `sudo pacman -S cmake ffmpeg pkgconf` |
| MSYS2 | `pacman -S $MINGW_PACKAGE_PREFIX-{cmake,ffmpeg,pkgconf,toolchain}` |

`cmake --build build` prints where each dependency came from. `ctest --test-dir build` runs the same smoke test as CI, and `cmake --install build` puts the program in `/usr/local/bin`.

Build options:

| Option | What it does |
| --- | --- |
| `-DLAPSE_FETCH_MISSING=OFF` | Stop with an error instead of downloading a missing dependency |
| `-DLAPSE_STATIC_DEPS=ON` | Link the dependencies statically |
| `-DLAPSE_DEPS_PREFIX=<prefix>` | Build against a prefix made by the scripts below |
| `-DLAPSE_LTO=OFF` | Skip link time optimisation, for a quicker build |
| `-DCMAKE_BUILD_TYPE=Debug` | A build you can step through in a debugger |
| `-DLAPSE_SANITIZE=ON` | A build with AddressSanitizer and UBSan in it, slow, for finding memory bugs. `.github/scripts/mutate-formats.py` feeds one mutated files in every format |

To build the way the releases are built, with FFmpeg, FFTW3 and libfvad static and cut down to what LAPSE uses:

```bash
.github/scripts/build-ffmpeg.sh "$HOME/deps"
.github/scripts/build-fftw.sh "$HOME/deps"
.github/scripts/build-libfvad.sh "$HOME/deps"
cmake -B build-release -DLAPSE_DEPS_PREFIX="$HOME/deps"
cmake --build build-release -j
```

CI does this on Linux, on macOS with the Xcode command line tools, and on Windows in an MSYS2 MINGW64 shell. On macOS, `MACOSX_DEPLOYMENT_TARGET` sets the oldest system the program will run on. `$HOME/deps` can be deleted when the build is done.

onnxruntime is not needed to build. LAPSE looks for it when it runs, first where `LAPSE_ONNXRUNTIME` points, then next to the program, then in the usual library paths. The model is looked for where `LAPSE_VAD_MODEL` points, next to the program, in `/usr/local/share/lapse` and `/usr/share/lapse`, and in the current folder.

---

## Command line

```
lapse <video or reference subtitle> [subtitle] [mode] [penalty] [options]
```

The first argument is what the subtitle should be lined up against. A video is listened to, or its own subtitle track is used when it has a usable one. A subtitle file that is already right works as well, and is quicker and more exact than the sound:

```bash
./lapse movie.mkv movie.da.srt
./lapse movie.en.srt movie.da.srt
```

### Modes

Without a mode LAPSE uses `auto` and works out for itself whether the file is shifted, drifting, cut, or a mix of those. The other modes are there for when you already know:

| Mode | What it does |
|---|---|
| `auto` | Works out what the file needs. The default |
| `nosplit` | One offset for the whole file |
| `ols` | One offset and a stretch, for frame rate drift |
| `split` | Lets the file be cut into parts with their own offsets |

```bash
./lapse movie.mkv movie.srt split 6
```

The number after `split` is the split penalty, the price every cut has to pay. Higher means fewer cuts. Without it LAPSE picks one from the size of the file. When the video has chapters, a cut that lands on a chapter mark costs half as much, because broadcasters put chapter marks where the ad breaks were.

### Options

The options can go anywhere on the line.

| Option | What it does |
|---|---|
| `--output <path>` | Write the result to `<path>` and leave the input alone |
| `--no-backup` | Do not keep a `.bak` of the file that was overwritten |
| `--no-sidecar` | Write nothing when the answer is not solid, instead of a `.lapse-unsure` file |
| `--strict` | Same as `--no-sidecar` |
| `--force` | Write over the input whatever the verdict, and sync files with very few lines |
| `--dry-run` | Work everything out and write nothing |
| `--confidence N` | The sigma an answer needs before the original is overwritten. Default 8 |
| `--min-shift ms` | Leave the file alone unless some line would move by at least this much |
| `--no-embedded` | Ignore subtitle tracks inside the video and listen to the sound |
| `--no-cache` | Do not read or write the cache |
| `--full-scan` | Accepted for older scripts. The whole film is always listened to |
| `--audio-track N` | Listen to audio track N, counting from 0, instead of the default one |
| `--sub-track N` | Use subtitle track N inside the video as the reference |
| `--fps N` | The frame rate to read a MicroDVD file at, instead of asking the video or the file |
| `--encoding NAME` | Write the result as `utf8`, `utf8-bom`, `utf16le`, `utf16be` or `latin1` |
| `--snap [ms]` | Move line starts and ends onto picture cuts close by. Default 120 ms |
| `--from <video>` | The subtitle fits this video. Move it to the first one by their sound |
| `--whisper <model>` | Only trust the speech whisper.cpp hears words in |
| `--json` | Print one line of JSON on stdout and everything else on stderr |
| `--quiet` | Print only errors |

And a few that do one thing and stop:

| Command | What it does |
|---|---|
| `lapse --shift <ms> <subtitle>` | Move every line by `ms`, earlier when it is negative. No video is needed, and `--output`, `--no-backup`, `--encoding`, `--fps` and `--dry-run` work as usual |
| `lapse --undo <subtitle>` | Put the `.bak` back and delete it |
| `lapse --version` | Print the version |
| `lapse --formats` | List the subtitle extensions this copy reads and writes |
| `lapse --vad` | Print `silero` or `libfvad`, and exit non-zero for `libfvad` |
| `lapse --batch` | Read jobs from stdin, see [Batch mode](#batch-mode) |

### Where the result goes

| Result | Options |
|---|---|
| Overwrite, keep a backup (default) | none |
| Overwrite, no backup | `--no-backup` |
| New file, original left alone | `--output out.srt --no-backup` |
| New file, and a backup of the original | `--output out.srt` |

An existing `.bak` is never overwritten, so the first backup is always the file as it was before LAPSE ever touched it.

A subtitle that is already in sync is not written again. The file keeps its modified time and no `.bak` is made, and `unchanged` in the `--json` line says so. Measuring speech is good to a few hundredths of a second, so syncing a file that is already right can still nudge it by 30 or 40 ms, and doing that again and again walks it about. `--min-shift 80` leaves it alone unless something would move by 80 ms or more. A file that drifts, or a result written to another path with `--output`, is always written.

### Verdicts

Every run ends with a verdict, and the verdict decides what happens to your file.

| Verdict | What it means | What happens | Exit code |
|---|---|---|---|
| `solid` | The answer stands out clearly and the slices of the file back it up | Your subtitle is overwritten and a `.bak` kept | 0 |
| `unsure` | There is an answer, but it is not proven | Your file is left alone and the answer goes to `name.lapse-unsure.srt` | 3 |
| `nothing` | Nothing in the sound backs up any offset | Your file is left alone and the best guess goes to `name.lapse-unsure.srt` | 3 |

A result short of `solid` is never written to the `--output` path itself. It goes beside it, so `--output out.srt` gives `out.lapse-unsure.srt` and never a guess named like the real thing, and the original gets no `.bak` for it.

`--confidence` moves the line for `solid`. `--force` writes over your file whatever the verdict. `--strict` and `--no-sidecar` write nothing when the verdict is not `solid`. The exit code follows the verdict either way, so an `unsure` that was not written still exits 3, with `written` set to `false` in the `--json` line.

The other exit codes: 2 for a subtitle with fewer than 10 lines to go on (it still gets a `--json` line saying so), 1 when something could not be read or written, and 255 for options it does not understand.

### Output for scripts

`--json` prints one line on stdout and sends every other message to stderr:

```json
{"mode":"auto/shifted","reference":"vad","offset_ms":22,"ratio":1,"confidence":0.455,
 "margin":0.12,"sigma":12.3,"agreement":0.75,"verdict":"solid","coverage":1,
 "cues":1578,"ignored_cues":1,"parts":1,"snapped":0,"written":true,"unchanged":false,"output":"...","splits":[],"offsets":[22]}
```

`mode` says what LAPSE decided the file needed. `ols`, `nosplit` and `split` mean you asked for that mode. Everything under `auto` is what it worked out by itself:

| `mode` | What it found | What moved |
|---|---|---|
| `auto/shifted` | One offset fits the whole file | `offset_ms` |
| `auto/shifted+split` | One offset nearly fits, with cuts in it | `offset_ms`, then `splits` |
| `auto/drifting` | The file drifts, usually a frame rate mismatch | `ratio` and `offset_ms` |
| `auto/drifting+split` | It drifts and was cut as well | `ratio`, then `splits` |
| `auto/recut` | The offset changes through the file | `splits` |
| `auto/joined` | Two parts in one video | `splits` |
| `auto/restart` | The subtitle starts over part way through | `splits` |

| Field | Meaning |
|---|---|
| `reference` | What the subtitle was lined up against: `vad` (the sound), `vad+whisper`, `embedded` (a track in the video), `subtitle` (a subtitle file), or `none` when there were too few lines to try |
| `offset_ms` | The main offset in milliseconds |
| `ratio` | The stretch, `1` unless the file drifted |
| `sigma` | How far the answer stands out from the noise. This is the number the verdict uses |
| `confidence`, `margin` | How much of the subtitle landed on speech, and how far ahead of the next best answer it is |
| `agreement` | The share of the eight slices that agree with the answer |
| `coverage` | How much of the film was listened to |
| `cues`, `ignored_cues` | How many lines the file has, and how many were left out of the measuring |
| `parts`, `splits`, `offsets` | How many parts the file ended up in, the line each new part starts at, counting from 0 in file order, and the offset each part got |
| `snapped` | How many line starts or ends `--snap` moved onto a picture cut |
| `written`, `unchanged`, `output`, `why` | Whether a file was written, whether it was left as it was because nothing moved, where the result went, and why it was not written when it was not |

### Batch mode

Starting LAPSE loads onnxruntime and the Silero model from disk. That is fine for one file and slow for a library. `--batch` reads jobs from stdin instead, one per line, with the same arguments you would give on the command line, and keeps the model loaded for as long as stdin is open:

```bash
echo 'video1.mkv subs1.srt' >> jobs.txt
echo 'video2.mkv subs2.srt ols' >> jobs.txt
./lapse --batch < jobs.txt
```

Every line gets exactly one line of JSON back, in the order the jobs came in, whether the job worked or not. A job that failed before it could report gets `{"error":true}`. `--json` is implied. Paths with spaces in them need double quotes.

### Moving a subtitle to another release

A subtitle that is right for one copy of a film is often wrong for another: the TV broadcast against the Blu-ray, PAL against NTSC, a copy with a scene missing. If you still have the copy the subtitle was made for, the two soundtracks show exactly how the copies differ. `--from` names that copy:

```bash
./lapse bluray.mkv tv.srt --from tv-recording.ts
```

Both videos are listened to, and the speech of the old one is lined up against the speech of the new one with everything LAPSE does for a subtitle: one offset, a frame rate stretch, parts cut out or put in. Every line of the subtitle then moves the way the speech around it moved. Both videos are cached, so a second subtitle for the same pair is quick. On a test film played 4.27% faster, every line landed within 10 ms, where syncing the same subtitle against the speech directly was 145 ms out.

### Listening for words

The speech detector hears voices. It cannot tell whether a voice is saying anything, so singing, a crowd, or shouting over gunfire all count as speech, and in a film with a lot of that the answer stands out less. `--whisper` also runs the audio through [whisper.cpp](https://github.com/ggml-org/whisper.cpp) and keeps only the stretches of speech it heard words in:

```bash
./lapse video.mkv subtitles.srt --whisper ~/models/ggml-base.en.bin
```

whisper.cpp is not included. LAPSE runs `whisper-cli` from the `PATH`, or wherever `LAPSE_WHISPER_CLI` points. The model is any ggml file from the whisper.cpp project, with the name they gave it, so LAPSE can tell which one it is. `ggml-base.en.bin` is plenty for English and `ggml-base.bin` for other languages. The words are cached with the speech, so only the first run on a film pays for the transcription. That is about two minutes for a two hour film on a recent laptop, and a lot longer on a small server without a GPU.

The timing of each line still comes from the speech detector. Whisper only decides which speech counts, so a good answer does not move. It stands out more on a busy soundtrack: with music under the speech at the same level, sigma went from 18 to 19. It is off unless you ask for it, and when `whisper-cli` cannot be found or hears fewer than 50 words, LAPSE says so and uses all the speech.

### Snapping to picture cuts

Subtitlers who time to picture put a line up on the cut. Moving the whole file by one number keeps every line the same distance from the cut it was timed against, so a sync that is right to within a few frames can still look a little late.

`--snap` takes that last step. Once the offset is settled, a line that starts within 120 ms of a picture cut is moved onto it, and its end moves with it so it stays on screen as long as before. An end that then sits within the same distance of a cut is moved onto that cut too, so a line does not hang over into the next shot. An end is never pulled in to less than half a second after its start, and never pushed past the start of the next line:

```bash
./lapse video.mkv subtitles.srt --snap
./lapse video.mkv subtitles.srt --snap 60
```

The cuts come from the keyframes the encoder already put in the file, so finding them only means reading the video packets, with no decoding. Encoders put a keyframe where the picture changes, and also on a timer when it does not. The timer ones sit exactly the encoder's longest gap after the one before, so they are left out. A file where every keyframe is evenly spaced was encoded without scene detection, so there is nothing to snap to, and LAPSE says so and leaves it. It also does nothing when the reference is a subtitle file.

### Encodings

A subtitle is written back in the encoding it came in, whether that is UTF-8, UTF-8 with a BOM, UTF-16 or an old codepage. `--encoding` writes it in the one you name instead, which is a quick way to get a library of mixed files down to one encoding. `utf16` is taken as `utf16le`, and `iso-8859-1` and `cp1252` as `latin1`.

LAPSE cannot tell one old codepage from another, so a codepage file converted to UTF-8 is read as ISO-8859-1. That is right for Western European languages. Cyrillic and CJK files come out as valid UTF-8 with the wrong letters. A character ISO-8859-1 does not have becomes `?`. `.sup` files have no text, so `--encoding` does nothing to them.

### The cache

The speech found in a video, and the timings of its subtitle tracks, are saved so the next run on the same video skips the decoding. The cache lives in `$LAPSE_CACHE` if that is set, otherwise in `$XDG_CACHE_HOME/lapse` or `~/.cache/lapse`, and in `%LOCALAPPDATA%\lapse\cache` on Windows. An entry belongs to one video file, so changing the file starts over. `--no-cache` skips it.

---

## Docker

The Docker image has the `lapse` program and a watcher written in Python. Point it at your media library and it scans everything on start, then picks up new files as they arrive.

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

Everything you mount gets scanned. For more than one library, mount each one and list them all:

```yaml
    volumes:
      - /your/movies:/movies
      - /your/tv:/tv
    environment:
      - MEDIA_ROOT=/movies,/tv
```

Set `PUID` and `PGID` to the user that owns your media. Without them the container runs as root, and the files it writes are owned by root.

The first run is worth watching with `docker compose logs -f lapse` to check that subtitles were paired with the right videos.

`latest` is built again whenever something is merged. Every release is also tagged with its version, such as `ghcr.io/schwponaco-org/lapse:2.5.0`, for when you want to stay on one.

### Web interface

Publish port `8080` and open `http://your-host:8080`. There is no login, so keep it on your own network or behind whatever already protects the rest of your setup.

```yaml
    ports:
      - 8080:8080
```

From there you can:

- **Pause and resume.** Pausing stops straight away. The file being synced at that moment is dropped and starts over when you resume, and it does not count as a failed attempt. While paused, new files are still noticed and noted down, they just wait. The setting survives a restart.
- **Undo.** Put the original subtitle back from its `.bak`, and leave that file alone from then on. Select several to undo them in one go.
- **Sync again.** Forget what is on record for the files you pick, or the whole library, and run them through again.
- **Set the rescan interval** without editing the compose file.
- **Leave folders out.** Everything you mounted is listed, one level down. Untick what should not be touched and the scanner walks past it. Deeper paths can be typed in.
- **Translate.** Pick subtitles, give a language code and send them off. See [Translation](#translation).
- **Shift.** Move a subtitle by hand, in milliseconds, for when a sync came out close but not quite right. The first lines are shown before and after as you type for `.srt`, `.vtt`, `.ass` and `.ssa`, and you choose whether the result goes next to the original or over it. Nothing is decoded, so it is instant, and it works on every format the engine writes.
- **Sync subtitles to a reference.** For a film with several subtitle tracks where one is already right. Lines every other track up against it, or just the one you pick. The sound is not used, so it is quick, and usually more exact than syncing each track on its own. Give a video instead of a subtitle when the subtitle was made for that copy of the film, and it is moved to its own video by the sound of the two (`--from`).
- **Convert.** Write a subtitle out as `.srt`, `.vtt`, `.ass` or `.ssa`. The original stays unless you tick the box. A file already at the new name is kept as `.bak` first.
- **Film counts.** The line under the tiles says how many films are fully synced, partly synced or untouched. Subtitle tracks inside the video are left out of the count unless you tick `Count embedded tracks too`.
- **Change the engine settings.** Mode, where results go, confidence and every switch the command line has, under Settings. See [Engine settings](#engine-settings).
- **Light or dark.** The button in the corner follows your system until you press it, then stays on your choice. Each browser remembers its own.

The library list can be filtered by status, name and confidence, so `Confidence under 5` quickly finds the results worth a second look.

What you change in the web interface is saved in the database and wins over the environment variables it overlaps with, so `SCAN_INTERVAL` is where it starts.

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
| `MODE` | `auto` | `auto`, `nosplit`, `ols` or `split` |
| `PENALTY` | `6` | Split penalty, only used in split mode |
| `SCAN_INTERVAL` | `900` | Seconds between full rescans. `0` turns them off |
| `MIN_CONFIDENCE` | `0` | Put the original back when a result stands out less than this, on the same scale as `CONFIDENCE`. `0` keeps everything |
| `MAX_ATTEMPTS` | `3` | How many times a failing pair is tried before it is left alone |
| `TIMEOUT` | `1800` | Seconds a single sync may take |
| `POLLING` | `0` | Set to `1` on network shares where file events do not arrive |
| `CACHE_DAYS` | `30` | Days a cached scan of a video is kept after it was last used. `0` keeps them forever |

### Engine settings

Every option the command line takes is available in the container. Switches are `0` or `1`. The rest are left empty to use the engine's default.

| Variable | Option | What it does |
|---|---|---|
| `OUTPUT_SUFFIX` | `--output` | Write to `name<suffix>.srt` and never touch the original. `.synced` gives `movie.synced.srt`, and a result it is not sure of `movie.synced.lapse-unsure.srt` |
| `NO_BACKUP` | `--no-backup` | Do not keep a `.bak` of the file that was overwritten |
| `NO_SIDECAR` | `--no-sidecar` | Do not write a `.lapse-unsure` guess when the result is uncertain |
| `NO_EMBEDDED` | `--no-embedded` | Ignore subtitle tracks inside the video |
| `NO_CACHE` | `--no-cache` | Listen to the video and read its subtitle tracks every time |
| `FULL_SCAN` | `--full-scan` | Accepted and ignored, the whole film is always listened to |
| `FORCE` | `--force` | Sync even when there is little to go on |
| `STRICT` | `--strict` | Only overwrite when the result is clearly right |
| `DRY_RUN` | `--dry-run` | Work out every offset and write nothing, not even to the database |
| `CONFIDENCE` | `--confidence` | How far the answer has to stand out before the original is overwritten |
| `AUDIO_TRACK` | `--audio-track` | Which audio track to listen to |
| `SUB_TRACK` | `--sub-track` | Which embedded subtitle track to use as the reference |
| `FPS` | `--fps` | Frame rate for frame based subtitles that do not carry one |
| `SNAP` | `--snap` | Milliseconds a line may be moved to land on a picture cut. Empty leaves it off |
| `MIN_SHIFT` | `--min-shift` | Leave a subtitle alone when nothing in it would move by this many milliseconds |
| `ENCODING` | `--encoding` | Write every result as `utf8`, `utf8-bom`, `utf16le`, `utf16be` or `latin1`. Empty keeps the encoding each file came in |
| `WHISPER` | `--whisper` | Path inside the container to a whisper.cpp model such as `ggml-base.en.bin`. Needs `whisper-cli` mounted in too, see [Listening for words](#listening-for-words) |

All of these are also under Settings in the web interface, and what you save there is used from the next scan on. `OUTPUT_SUFFIX` and `NO_BACKUP` together make the file output choice there:

| File output | What happens |
|---|---|
| Write a new file | The original is left alone. `Movie.en.srt` gets `Movie.en.synced.srt` next to it |
| Write a new file, keep a backup | The same, and a copy of the original is kept as `.bak` |
| Overwrite, keep a backup | The subtitle is replaced and the old one kept as `.bak`. The default |
| Overwrite, no backup | The subtitle is replaced and nothing is kept |

`OUTPUT_SUFFIX` keeps your library exactly as it is. The synced copy goes next to each original, and files the container wrote itself are skipped on later scans. Add `NO_BACKUP=1` if you do not want `.bak` files as well.

`DRY_RUN=1` is a good first pass over a big library. The log shows what every file would get, and since nothing is written or recorded, the next run with it off starts from scratch.

Cached scans are kept in a `cache` folder next to the database, so they survive a restart, and anything not used for `CACHE_DAYS` is deleted. Set `LAPSE_CACHE` to keep them somewhere else.

### Translation

A synced subtitle can be sent off to be translated and written back next to the original as `name.<language>.srt`. Nothing is translated until you set a provider.

| Variable | Default | What it does |
|---|---|---|
| `TRANSLATE_PROVIDER` | `deepl` | `deepl`, `google` or `libretranslate` |
| `TRANSLATE_KEY` | unset | API key. Not needed for a LibreTranslate server that does not ask for one |
| `TRANSLATE_URL` | unset | Address of your own LibreTranslate, such as `http://libretranslate:5000` |
| `TRANSLATE_TO` | unset | Languages every synced subtitle is translated into, comma separated. Leave it empty to only translate what you pick in the web interface |
| `TRANSLATE_FROM` | unset | Source language. The provider works it out when this is empty |
| `TRANSLATE_BATCH` | `40` | Lines per request |

```yaml
    environment:
      - TRANSLATE_PROVIDER=deepl
      - TRANSLATE_KEY=your-key:fx
      - TRANSLATE_TO=da
```

A DeepL key ending in `:fx` is a free key and goes to the free endpoint by itself.

`.srt`, `.vtt`, `.ass` and `.ssa` can be translated. Timings, line numbers and headers stay as they are, and in `.ass` only the `Dialogue:` lines are sent. An existing file is never overwritten, and the translations the container wrote are not synced again on the next scan.

If you already use [Lingarr](https://github.com/lingarr-translate/lingarr) or something like it, leave `TRANSLATE_TO` empty and carry on with that.

### How subtitles are paired with videos

Subtitles are paired with videos by file name, inside the same folder. If the subtitles sit in a folder of their own, like the `Subs` folder many releases have, the folder above is checked too.

Language and tag words such as `en`, `danish`, `forced` and `sdh` are ignored when names are compared, so one video can have several subtitles. Episode numbers have to match, so `S01E01` never gets the subtitle for `S01E02`. When a folder has only one video, every subtitle in it belongs to that video.

A new file is left alone until it stops changing, so a download that is still being written is not touched. Full rescans catch anything the file events missed, which is what `SCAN_INTERVAL` and `POLLING` are for on network storage.

Every sync is recorded in the database, so the same file is never done twice. Replacing a subtitle with a new file gets it synced again.

---

## Subtitle formats

| Extension | Format | Notes |
|---|---|---|
| `.srt` | SubRip | |
| `.vtt` | WebVTT | Cue settings and inline timestamps are kept and moved |
| `.ass`, `.ssa` | Advanced SubStation Alpha, SubStation Alpha | Reads the `Format:` line, so the time columns can be anywhere |
| `.sub` | MicroDVD, MPL2 or SubViewer 2 | Told apart by what is inside, see below |
| `.mpl2` | MPL2 | |
| `.sbv` | YouTube SubViewer | |
| `.smi`, `.sami` | SAMI | |
| `.ttml`, `.dfxp` | TTML | Clock, offset, tick and frame times. Frame times are written back as frames, the others as clock times |
| `.itt` | iTunes Timed Text | TTML with the times in frames |
| `.xml` | TTML saved as `.xml` | Read when the file is TTML inside |
| `.stl` | EBU STL, or the text STL from DVD Studio Pro and Spruce | Told apart by what is inside |
| `.scc` | Scenarist closed captions | |
| `.sup` | PGS, Blu-ray bitmap subtitles | Timing only, there is no text |
| `.idx` | VobSub, DVD bitmap subtitles | Point it at the `.idx`. `delay:` lines are read and kept |

Every format is written back the way it came in, down to its own resolution, so an MPL2 result lands on the nearest tenth of a second and an STL on the nearest frame. Everything around the times, such as styles, headers, comments and line endings, is left as it was. Upper case extensions work too, and `lapse --formats` lists what your copy reads.

Three formats share `.sub` and the name does not say which one you have, so LAPSE looks inside:

| | Looks like | Needs a frame rate |
|---|---|---|
| MicroDVD | `{450}{487}text` | Yes, frames mean nothing without one |
| MPL2 | `[180][195]text` | No, the numbers are tenths of a second |
| SubViewer 2 | `00:03:00.00,00:03:01.50` on its own line | No |

So an MPL2 or SubViewer `.sub` syncs with no video and no `--fps`. A MicroDVD file gets its frame rate from the video, then from a `{1}{1}25.000` line at the top, then from how long the two subtitles run when the reference is a subtitle. When none of those work, LAPSE stops and asks for `--fps`.

The text STL has no frame rate in it either. A file with a frame number of 25 or more is read at 30 frames a second, and anything else at 25.

A file named `.txt`, or with no extension, is opened and read before it is turned away. Subtitles come out of archives named like that often enough to be worth it. SubRip, WebVTT, SubStation, MicroDVD, MPL2, SubViewer and TTML all show what they are in the first few lines, and the file is written back as whatever it turned out to be. Anything over 4 MB, or with an extension LAPSE does not know, is left alone, so pointing it at a video by mistake fails straight away. The container picks up the extensions `lapse --formats` lists and a `.xml` with TTML inside. It leaves `.txt` alone, since most of those beside a video are release notes.

**Video:** anything FFmpeg can read, such as `.mkv`, `.mp4`, `.avi`, `.mov`, `.ts` and `.webm`.

**Subtitle tracks inside the video:** `lapse video.mkv` on its own takes out the default text track, `mov_text` included, syncs it to the sound and writes it next to the video. When a video has a usable subtitle track, LAPSE uses that track as the reference instead of listening to the sound, which is quicker and more exact. PGS and VobSub tracks have no text to take out, but their timing works as a reference too. Reading a track means reading the whole file, since its packets are spread all through it, so the timings are cached as well.

---

## More documentation

- [docs/pipeline-integration.md](docs/pipeline-integration.md) is for running `lapse` from Bazarr, Sonarr, Radarr or any other tool that can run a command when a file arrives.
- [docs/benchmarks.md](docs/benchmarks.md) has the test against alass and ffsubsync on 39 feature films picked to be hard to sync, with the method and the result for every film. It covers LAPSE 2.0.0 and 2.5.0.

---

## Repository

```
lapse/
├── engine/       C++ source for the lapse program
├── docker/       The watcher, the web interface, the Dockerfile and a compose file
├── docs/         Benchmarks and other documentation
└── .github/      CI workflows and the scripts that build a release
```

---

Licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE).
