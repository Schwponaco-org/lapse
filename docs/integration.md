# Using LAPSE in your own program

This page is for people who want to call LAPSE from a script, an app or a media tool. The Jellyfin plugin and AutoSubSync both work this way.

There are two ways to do it:

- **Run the `lapse` program** and read one line of JSON back. This works anywhere you can start a process.
- **Send the subtitle over HTTP** to the Docker image. This suits a program that runs in its own container and cannot run `lapse` itself.

## Running the program

### Shipping it

Every release has a ready to run archive for each platform, listed in the [README](../README.md#install). Keep the files in the archive together. `lapse` looks for `libonnxruntime` (`onnxruntime.dll` on Windows) and `silero_vad.onnx` next to itself. Without them it still runs, with the simpler libfvad speech detector. FFmpeg and the other libraries are built into the program, so nothing else has to be installed.

`lapse --vad` prints `silero` or `libfvad` and exits with 0 for `silero`. `lapse --version` prints the version. Both are quick checks that your copy works.

If your program runs in a different container from LAPSE, put the unpacked archive on a volume both can see.

### One sync

```bash
lapse <reference> <subtitle> --json --output <path>
```

The reference is a video, or a subtitle that is already in sync. A subtitle as reference is quicker and more exact, since nothing has to be decoded. If you have the same film in several languages, sync the first subtitle against the video and the rest against that first subtitle.

`--json` prints one line of JSON on stdout. Everything else goes to stderr, so you can read stdout without having to filter it.

```json
{"mode":"auto/shifted","reference":"vad","offset_ms":22,"ratio":1,"confidence":0.455,
 "margin":0.12,"sigma":12.3,"agreement":0.75,"verdict":"solid","coverage":1,
 "cues":1578,"ignored_cues":1,"parts":1,"snapped":0,"written":true,"unchanged":false,"output":"...","splits":[],"offsets":[22]}
```

The fields you will look at most:

| Field | What to do with it |
|---|---|
| `verdict` | `solid` means the result was written. `unsure` and `nothing` mean it was not proven. See below |
| `written` | Whether a file was written at all |
| `output` | Where the result went |
| `unchanged` | `true` when the subtitle was already in sync and nothing was written |
| `offset_ms`, `ratio`, `offsets` | What was done to the subtitle, if you want to show it to the user |
| `sigma` | How sure LAPSE is. The verdict is based on it |

Every field is described in [docs/command-line.md](command-line.md#output-for-scripts).

### Where the result goes

By default LAPSE writes over the subtitle and keeps the original as `.bak`. In a program you usually want to decide that yourself:

| You want | Options |
|---|---|
| Leave the input alone and write the result somewhere else | `--output out.srt --no-backup` |
| Write over the input and keep a backup | nothing |
| Write over the input with no backup | `--no-backup` |
| Only find the offset, write nothing | `--dry-run` |

`--encoding utf8` writes the result as UTF-8 whatever the input was. Without it the result keeps the encoding of the input.

### Verdicts and exit codes

| Verdict | Exit code | What happens |
|---|---|---|
| `solid` | 0 | The result is written |
| `unsure` | 3 | The input is left alone and the guess goes to `name.lapse-unsure.srt` beside it |
| `nothing` | 3 | The same as `unsure`. Nothing in the sound backed up any offset |

The other exit codes are 2 when the subtitle has fewer than 10 lines (there is still a JSON line), 1 when a file could not be read or written, and 255 for options LAPSE does not know.

If you do not want the `.lapse-unsure` files, add `--no-sidecar`. Then nothing is written unless the verdict is `solid`. `--confidence N` sets how sure LAPSE has to be for `solid`. The default is 8. `--force` writes the result whatever the verdict.

The simplest rule for a program is to trust `solid` and show the user anything else.

### Progress

Set `LAPSE_PROGRESS=1` and LAPSE prints progress on stderr while it works, also under `--json`. When stderr is not a terminal you get one line for every 10%:

```
Listening to the audio 40%
```

### Many files

Starting `lapse` loads onnxruntime and the speech model each time. For a list of files, `--batch` keeps them loaded. Write one job per line on stdin, with the same arguments you would give on the command line:

```
video1.mkv subs1.srt
video2.mkv subs2.srt --output synced2.srt
"a path with spaces.mkv" subs3.srt
```

You get exactly one line of JSON back on stdout per job, in the same order. A job that failed before it could report gets `{"error":true}`, so you can always match answers to jobs line by line.

### Other useful options

| Option | Use |
|---|---|
| `--audio-track N`, `--sub-track N` | Pick the audio or subtitle track when you know which one is right |
| `--no-embedded` | Always listen to the sound, even when the video has a subtitle track |
| `--min-shift ms` | Leave a subtitle alone unless a line would move by at least this much. Good when you sync the same files again and again |
| `--threads N` | Keep LAPSE to `N` threads so it does not take the whole machine |
| `--no-cache` | Do not read or write the cache. By default the speech of each video is saved, so the next subtitle for it takes about a second |
| `--shift ms` | Move a subtitle by hand, with no video. Handy for a "nudge" button |
| `--undo <subtitle>` | Put the `.bak` back |

The full list is in [docs/command-line.md](command-line.md).

## Over HTTP

The Docker image can take one subtitle at a time over HTTP. You send the subtitle and the path to the video, and you get the synced subtitle back. Nothing is written next to your media.

How to set up the container for it, and the full request and answer, are in [docs/docker.md](docker.md#syncing-for-other-programs).

## Next to the Docker watcher

If your program only calls LAPSE when it handles a file, files added some other way, such as copied in by hand, will not be synced. You can run the Docker image as well to catch those.
