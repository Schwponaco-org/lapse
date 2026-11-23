# LAPSE

**Language-Agnostic Playback Synchronization Engine**

LAPSE fixes subtitles that are out of sync with the video. It listens to the film, finds where people are talking, and moves the subtitle so every line comes up with the speech it belongs to. The language of the film and the subtitle does not matter.

It fixes a subtitle that is early or late by the same amount all the way through, one that slowly drifts because it was made for another frame rate, and one that was made for a different cut of the film.

## Try it in the browser

### [schwponaco.org/lapse](https://schwponaco.org/lapse/)

The browser version is experimental. It is the same engine built for WebAssembly and it runs in the tab, so your video stays on your machine. It is handy for a file or two. For a whole library, use one of the options below.

---

## How it compares

LAPSE 2.5.0 was tested against alass and ffsubsync on 39 feature films that were picked because they are hard to sync. The table shows films synced out of 39. Partial means some segments of a split file landed and some did not.

| Test | LAPSE 2.5.0 | alass | ffsubsync |
| ---- | :---------: | :---: | :-------: |
| Normal file (shift or drift) | **37** | 31 | 33 |
| Split file, the tool decides by itself | **34** +2 partial | n/a | n/a |
| Split file, splitting turned on | **37** +1 partial | 28 +1 partial | 18 +1 partial |
| First run, all 39 films | **373.3s** | 514.1s | 656.3s |

The full results are in [docs/benchmarks.md](docs/benchmarks.md).

---

## Ways to use LAPSE

- **[Jellyfin plugin](https://github.com/Schwponaco-org/lapse-jellyfin-plugin).** Syncs subtitles from inside Jellyfin, by hand or for a whole library on a schedule.
- **[Docker](#docker).** Watches your media library, pairs every subtitle with its video and keeps them in sync. It has a web page where you can see and undo what it did.
- **[AutoSubSync](https://github.com/denizsafak/AutoSubSync).** A desktop app for Windows, macOS and Linux. Drop in a video and a subtitle and pick lapse as the sync tool. It downloads LAPSE and keeps it up to date for you.
- **The `lapse` program.** Run it on one file, or on a whole list of them. See [Install](#install) and [Quick start](#quick-start).
- **[Terminal interface](https://github.com/Schwponaco-org/lapse-tui).** Runs the `lapse` you have installed, for a server over ssh. Linux and macOS.

---

## Install

Every release has a ready to run archive for each platform. Unpack it and run the program. There is nothing else to install.

| Platform | Archive | Runs on |
|---|---|---|
| Linux x86-64 | `lapse-linux-amd64.tar.gz` | glibc 2.35 and newer (Ubuntu 22.04+, Debian 12+) |
| Linux ARM64 | `lapse-linux-arm64.tar.gz` | Raspberry Pi 4/5, ARM servers, Apple silicon VMs |
| Alpine x86-64 | `lapse-linux-musl-amd64.tar.gz` | Alpine 3.20 and newer, and other musl systems |
| Alpine ARM64 | `lapse-linux-musl-arm64.tar.gz` | Alpine 3.20 and newer, and other musl systems |
| macOS Apple silicon | `lapse-macos-arm64.tar.gz` | macOS 11 Big Sur and newer |
| macOS Intel | `lapse-macos-x86_64.tar.gz` | macOS 10.15 Catalina and newer |
| Windows x64 | `lapse-windows-x64.zip` | Windows 10 and newer |

Each archive holds the program, `libonnxruntime` (`onnxruntime.dll` on Windows), `silero_vad.onnx` and the licence. `SHA256SUMS` in the release covers every file.

On Linux and macOS, make the program executable after unpacking:

```bash
tar xf lapse-linux-amd64.tar.gz
cd lapse-linux-amd64
chmod +x lapse
./lapse --vad
```

`--vad` prints `silero` or `libfvad`. It is a quick way to check that the archive is complete.

On Windows, `onnxruntime.dll` needs the Microsoft Visual C++ 2015-2022 Redistributable, which most machines already have. The Windows build also runs on Windows on ARM.

To build it yourself, see [docs/building.md](docs/building.md).

## Quick start

```bash
./lapse movie.mkv movie.en.srt
```

LAPSE works out what is wrong with `movie.en.srt`, fixes it, and keeps the original next to it as `movie.en.srt.bak`. When it is not sure of the answer, it leaves your file alone and writes its guess to `movie.en.lapse-unsure.srt` so you can look at it.

```bash
./lapse movie.en.srt movie.da.srt
```

A subtitle that is already right works as a reference too. It is quicker and more exact than the sound.

```bash
./lapse movie.mkv
```

With no subtitle given, LAPSE takes the subtitle track out of the video, syncs it to the sound and writes it next to the video as `movie.lapse.srt`.

```bash
./lapse --undo movie.en.srt
```

Puts the original back from the `.bak`.

Every option is in [docs/command-line.md](docs/command-line.md).

---

## Docker

The Docker image has the `lapse` program and a watcher. Point it at your media library and it syncs everything on start, then picks up new files as they arrive.

```yaml
services:
  lapse:
    image: ghcr.io/schwponaco-org/lapse:latest
    restart: always
    ports:
      - 8080:8080
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

Set `PUID` and `PGID` to the user that owns your media. Without them the container runs as root and the files it writes are owned by root.

For more than one library, mount each one and list them all:

```yaml
    volumes:
      - /your/movies:/movies
      - /your/tv:/tv
    environment:
      - MEDIA_ROOT=/movies,/tv
```

Open `http://your-host:8080` for the web interface. It has no login, so keep it on your own network.

Watch the first run with `docker compose logs -f lapse` to check that subtitles were paired with the right videos.

`latest` is rebuilt whenever something is merged. Every release also has its own tag, such as `ghcr.io/schwponaco-org/lapse:2.5.1`, if you want to stay on one version.

The web interface, every setting, translation and how files are paired are in [docs/docker.md](docs/docker.md).

---

## What it can fix

| Problem | What it looks like | What LAPSE does |
|---|---|---|
| Shifted | Every line is early or late by the same amount | Finds the offset, up to 15 minutes either way |
| Drifting | The lines start fine and get further out, because the subtitle was made for 25 fps and the video runs at 23.976 | Stretches the subtitle back |
| Cut differently | A scene was cut or added, or the subtitle was made for a TV broadcast with ad breaks | Splits the subtitle into parts and gives each part its own offset |
| Starts over | Two episodes in one file, or a subtitle that restarts part way through | Lines each part up on its own |
| Another release | The subtitle was made for a different copy of the film, and you have that copy | Compares the sound of the two videos (`--from`) |

Sound effects in brackets, music notes and the credit lines subtitle sites add are left out of the measuring. They still move with the lines around them.

## Documentation

| | |
|---|---|
| [Command line](docs/command-line.md) | Every option, the verdicts, the JSON output and batch mode |
| [Docker](docs/docker.md) | The web interface, all settings, translation and how files are paired |
| [Subtitle formats](docs/formats.md) | Every format LAPSE reads and writes |
| [Using LAPSE in your own program](docs/integration.md) | Calling `lapse` from a script or an app, or over HTTP |
| [How it works](docs/how-it-works.md) | What LAPSE does with the sound and the subtitle |
| [Building](docs/building.md) | Building from source |
| [Benchmarks](docs/benchmarks.md) | LAPSE against alass and ffsubsync on 39 hard films |

---

Licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE).
