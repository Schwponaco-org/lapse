# Command line

```
lapse <video or reference subtitle> [subtitle] [mode] [penalty] [options]
```

The first argument is what the subtitle should be lined up against. A video is listened to, or its own subtitle track is used when it has a usable one. A subtitle file that is already right works as well, and is quicker and more exact than the sound:

```bash
./lapse movie.mkv movie.da.srt
./lapse movie.en.srt movie.da.srt
```

## Modes

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

## Options

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
| `--threads N` | Listen to the video with at most `N` threads. The default is every core, up to 8 |
| `--json` | Print one line of JSON on stdout and everything else on stderr |
| `--quiet` | Print only errors |

And a few that do one thing and stop:

| Command | What it does |
|---|---|
| `lapse --shift <ms> <subtitle>` | Move every line by `ms`, earlier when it is negative. No video is needed, and `--output`, `--no-backup`, `--encoding`, `--fps` and `--dry-run` work as usual |
| `lapse --shift <ms> --ratio <R> <subtitle>` | Stretch every time by `R` and then move it by `ms`, so a line at `t` lands at `t * R + ms`. Two lines you know the right times for give both: `R` is how far apart they should be over how far apart they are, and `ms` is where the first should be minus its time times `R` |
| `lapse --undo <subtitle>` | Put the `.bak` back and delete it |
| `lapse --version` | Print the version |
| `lapse --formats` | List the subtitle extensions this copy reads and writes |
| `lapse --vad` | Print `silero` or `libfvad`, and exit non-zero for `libfvad` |
| `lapse --batch` | Read jobs from stdin, see [Batch mode](#batch-mode) |

## Where the result goes

| Result | Options |
|---|---|
| Overwrite, keep a backup (default) | none |
| Overwrite, no backup | `--no-backup` |
| New file, original left alone | `--output out.srt --no-backup` |
| New file, and a backup of the original | `--output out.srt` |

An existing `.bak` is never overwritten, so the first backup is always the file as it was before LAPSE ever touched it.

A subtitle that is already in sync is not written again. The file keeps its modified time and no `.bak` is made, and `unchanged` in the `--json` line says so. Measuring speech is good to a few hundredths of a second, so syncing a file that is already right can still nudge it by 30 or 40 ms, and doing that again and again walks it about. `--min-shift 80` leaves it alone unless something would move by 80 ms or more. A file that drifts, or a result written to another path with `--output`, is always written.

## Verdicts

Every run ends with a verdict, and the verdict decides what happens to your file.

| Verdict | What it means | What happens | Exit code |
|---|---|---|---|
| `solid` | The answer stands out clearly and the slices of the file back it up | Your subtitle is overwritten and a `.bak` kept | 0 |
| `unsure` | There is an answer, but it is not proven | Your file is left alone and the answer goes to `name.lapse-unsure.srt` | 3 |
| `nothing` | Nothing in the sound backs up any offset | Your file is left alone and the best guess goes to `name.lapse-unsure.srt` | 3 |

A result short of `solid` is never written to the `--output` path itself. It goes beside it, so `--output out.srt` gives `out.lapse-unsure.srt` and never a guess named like the real thing, and the original gets no `.bak` for it.

`--confidence` moves the line for `solid`. `--force` writes over your file whatever the verdict. `--strict` and `--no-sidecar` write nothing when the verdict is not `solid`. The exit code follows the verdict either way, so an `unsure` that was not written still exits 3, with `written` set to `false` in the `--json` line.

The other exit codes: 2 for a subtitle with fewer than 10 lines to go on (it still gets a `--json` line saying so), 1 when something could not be read or written, and 255 for options it does not understand.

## Output for scripts

`--json` prints one line on stdout and sends every other message to stderr:

```json
{"mode":"auto/shifted","reference":"vad","offset_ms":22,"ratio":1,"confidence":0.455,
 "margin":0.12,"sigma":12.3,"agreement":0.75,"verdict":"solid","coverage":1,
 "cues":1578,"ignored_cues":1,"parts":1,"snapped":0,"written":true,"unchanged":false,"output":"...","splits":[],"offsets":[22]}
```

Set `LAPSE_PROGRESS=1` to get the progress lines on stderr as well, such as `Listening to the audio 40%`.

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

## Batch mode

Starting LAPSE loads onnxruntime and the Silero model from disk. That is fine for one file and slow for a library. `--batch` reads jobs from stdin instead, one per line, with the same arguments you would give on the command line, and keeps the model loaded for as long as stdin is open:

```bash
echo 'video1.mkv subs1.srt' >> jobs.txt
echo 'video2.mkv subs2.srt ols' >> jobs.txt
./lapse --batch < jobs.txt
```

Every line gets exactly one line of JSON back, in the order the jobs came in, whether the job worked or not. A job that failed before it could report gets `{"error":true}`. `--json` is implied. Paths with spaces in them need double quotes. Inside double quotes `\"` stands for a `"` in the path, and every other character, a lone backslash too, is taken as it is. Words without quotes are taken as they are.

## Moving a subtitle to another release

A subtitle that is right for one copy of a film is often wrong for another: the TV broadcast against the Blu-ray, PAL against NTSC, a copy with a scene missing. If you still have the copy the subtitle was made for, the two soundtracks show exactly how the copies differ. `--from` names that copy:

```bash
./lapse bluray.mkv tv.srt --from tv-recording.ts
```

Both videos are listened to, and the speech of the old one is lined up against the speech of the new one with everything LAPSE does for a subtitle: one offset, a frame rate stretch, parts cut out or put in. Every line of the subtitle then moves the way the speech around it moved. Both videos are cached, so a second subtitle for the same pair is quick. On a test film played 4.27% faster, every line landed within 10 ms, where syncing the same subtitle against the speech directly was 145 ms out.

## Listening for words

The speech detector hears voices. It cannot tell whether a voice is saying anything, so singing, a crowd, or shouting over gunfire all count as speech, and in a film with a lot of that the answer stands out less. `--whisper` also runs the audio through [whisper.cpp](https://github.com/ggml-org/whisper.cpp) and keeps only the stretches of speech it heard words in:

```bash
./lapse video.mkv subtitles.srt --whisper ~/models/ggml-base.en.bin
```

whisper.cpp is not included. LAPSE runs `whisper-cli` from the `PATH`, or wherever `LAPSE_WHISPER_CLI` points. The model is any ggml file from the whisper.cpp project, with the name they gave it, so LAPSE can tell which one it is. `ggml-base.en.bin` is plenty for English and `ggml-base.bin` for other languages. The words are cached with the speech, so only the first run on a film pays for the transcription. That is about two minutes for a two hour film on a recent laptop, and a lot longer on a small server without a GPU.

The timing of each line still comes from the speech detector. Whisper only decides which speech counts, so a good answer does not move. It stands out more on a busy soundtrack: with music under the speech at the same level, sigma went from 18 to 19. It is off unless you ask for it, and when `whisper-cli` cannot be found or hears fewer than 50 words, LAPSE says so and uses all the speech.

## Snapping to picture cuts

Subtitlers who time to picture put a line up on the cut. Moving the whole file by one number keeps every line the same distance from the cut it was timed against, so a sync that is right to within a few frames can still look a little late.

`--snap` takes that last step. Once the offset is settled, a line that starts within 120 ms of a picture cut is moved onto it, and its end moves with it so it stays on screen as long as before. An end that then sits within the same distance of a cut is moved onto that cut too, so a line does not hang over into the next shot. An end is never pulled in to less than half a second after its start, and never pushed past the start of the next line:

```bash
./lapse video.mkv subtitles.srt --snap
./lapse video.mkv subtitles.srt --snap 60
```

The cuts come from the keyframes the encoder already put in the file, so finding them only means reading the video packets, with no decoding. Encoders put a keyframe where the picture changes, and also on a timer when it does not. The timer ones sit exactly the encoder's longest gap after the one before, so they are left out. A file where every keyframe is evenly spaced was encoded without scene detection, so there is nothing to snap to, and LAPSE says so and leaves it. It also does nothing when the reference is a subtitle file.

## Encodings

A subtitle is written back in the encoding it came in, whether that is UTF-8, UTF-8 with a BOM, UTF-16 or an old codepage. `--encoding` writes it in the one you name instead, which is a quick way to get a library of mixed files down to one encoding. The file is written in that encoding even when nothing in it had to move. `utf16` is taken as `utf16le`, and `iso-8859-1` and `cp1252` as `latin1`.

When a file in an old codepage is converted, LAPSE works out which codepage it is from the letters in it. It knows Windows-1252 for Western European languages, Windows-1250 and ISO-8859-2 for Polish, Czech, Slovak, Hungarian, Croatian and Romanian, and Windows-1251 for Cyrillic. A file with only a line or two of accented text can still be guessed wrong. CJK codepages are not recognised and come out with the wrong letters. With `--encoding latin1`, a character Windows-1252 does not have becomes `?`. `.sup` files have no text, so `--encoding` does nothing to them.

## The cache

The speech found in a video, and the timings of its subtitle tracks, are saved so the next run on the same video skips the decoding. The cache lives in `$LAPSE_CACHE` if that is set, otherwise in `$XDG_CACHE_HOME/lapse` or `~/.cache/lapse`, and in `%LOCALAPPDATA%\lapse\cache` on Windows. An entry belongs to one video file, so changing the file starts over. `--no-cache` skips it.
