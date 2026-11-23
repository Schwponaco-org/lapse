# Docker

How to set up the container is in the [README](../README.md#docker). This page covers everything else.

## Web interface

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

## Syncing for other programs

The web port also takes one subtitle at a time from another program running in its own container. Run it with `SCAN=0` so it never goes through the library by itself, and mount the media at the same path the other program sees, read only is enough:

```yaml
services:
  lapse:
    image: ghcr.io/schwponaco-org/lapse:latest
    restart: always
    ports:
      - 8080:8080
    volumes:
      - ./data:/data
      - /mnt/media:/mnt/media:ro
    environment:
      - SCAN=0
      - PUID=1000
      - PGID=1000
```

There is no login, so keep the port on your own network. Results are cached under `/data/cache`, like the rest of the container's work.

`GET /api/version` answers `{"version": "2.5.0"}`.

`POST /api/sync` takes a JSON body:

| Field | |
|---|---|
| `video` | Path to the video, the same one the caller sees. `null` when `reference` is used |
| `subtitle`, `filename` | The subtitle as base64, and its file name. The extension says which format it is |
| `reference`, `reference_filename` | Another subtitle, as base64, to line up against instead of the video. The video is not looked at then |
| `audio_track`, `sub_track`, `no_embedded` | `--audio-track`, `--sub-track` and `--no-embedded`. Optional |

A body that cannot be used gets `400` and `{"error": "..."}`. A video that is not in the container gets `404` and the same kind of answer. Anything else is `200` with `application/x-ndjson`, one JSON object per line as the sync goes along. When nothing has been sent for a second an empty line is, so skip those. It shows the connection is alive, and it is how a caller that left is noticed:

```
{"progress": 40, "phase": "Listening to the audio"}
{"done": {...}, "subtitle": "<base64>"}
```

`done` is the `--json` line of the sync, and `subtitle` is the result as base64, or `null` when LAPSE did not write one (`unsure`, `nothing`, or too few lines). A sync that failed ends in `{"error": "..."}` instead. If the caller hangs up, the sync is stopped straight away. Nothing is written beside the media or kept after the answer, and several syncs can run at once.

## Settings

| Variable | Default | What it does |
|---|---|---|
| `MEDIA_ROOT` | `/media` | Where to look. Comma separated for more than one library |
| `DB_PATH` | `/data/lapse.db` | Where the record of finished work is kept |
| `PUID` / `PGID` | unset | Run as this user and group |
| `MODE` | `auto` | `auto`, `nosplit`, `ols` or `split` |
| `PENALTY` | `6` | Split penalty, only used in split mode |
| `SCAN` | `1` | Set to `0` to leave the library alone: no scan on start, no watching for new files and no rescans. The container then only answers [requests from other programs](#syncing-for-other-programs). Rescans you ask for in the web interface still run, and cache cleanup still runs every `SCAN_INTERVAL` |
| `SCAN_INTERVAL` | `900` | Seconds between full rescans. `0` turns them off |
| `MIN_CONFIDENCE` | `0` | Put the original back when a result stands out less than this, on the same scale as `CONFIDENCE`. `0` keeps everything |
| `MAX_ATTEMPTS` | `3` | How many times a failing pair is tried before it is left alone |
| `TIMEOUT` | `1800` | Seconds a single sync may take |
| `POLLING` | `0` | Set to `1` on network shares where file events do not arrive |
| `CACHE_DAYS` | `30` | Days a cached scan of a video is kept after it was last used. `0` keeps them forever |

## Engine settings

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
| `WHISPER` | `--whisper` | Path inside the container to a whisper.cpp model such as `ggml-base.en.bin`. Needs `whisper-cli` mounted in too, see [Listening for words](command-line.md#listening-for-words) |

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

## Translation

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

## How subtitles are paired with videos

Subtitles are paired with videos by file name, inside the same folder. If the subtitles sit in a folder of their own, like the `Subs` folder many releases have, the folder above is checked too.

Language and tag words such as `en`, `danish`, `forced` and `sdh` are ignored when names are compared, so one video can have several subtitles. Episode numbers have to match, so `S01E01` never gets the subtitle for `S01E02`. When a folder has only one video, every subtitle in it belongs to that video.

A new file is left alone until it stops changing, so a download that is still being written is not touched. Full rescans catch anything the file events missed, which is what `SCAN_INTERVAL` and `POLLING` are for on network storage.

Every sync is recorded in the database, so the same file is never done twice. Replacing a subtitle with a new file gets it synced again.
