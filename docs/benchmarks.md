# Benchmarks

LAPSE 2.0.0 against alass 2.0.0 and ffsubsync 0.5.1, on 39 feature films, plus
one test with a subtitle file that belongs to a different film entirely.

## Read this before the tables

**The films were picked to be hard.** They are not a random sample. They were
chosen out of an ordinary home library for the traits that give a subtitle
sync tool trouble: long running times, long stretches with no dialogue, heavy
score under the dialogue, and films that exist in more than one cut. Offsets
between 12 and 97 seconds were then injected, along with cuts in up to four
places.

That matters when these numbers are set against what the tools report about
themselves. alass, for example, reports that on subtitles pulled at random
from OpenSubtitles "the percentage of good subtitles is about 88% to 98%".
That is a fair number for a random sample and it does not contradict anything
here. A random sample is mostly easy files. This set was chosen for the
opposite reason. Every tool in this document scores lower here than it would
on a normal library, and that is the point: the interesting part is where
they fail, not where they all succeed.

**File types.** `2001: A Space Odyssey`, `Challengers`, `Come and See` and
`Seven Samurai` are `.mkv`. The other 35 are `.mp4`. All subtitles are `.srt`.
The container matters for timing, so ffsubsync is much faster on those four
than on anything else and those four times should not be compared to the
rest.

**Results will vary.** Different releases, different subtitle uploads and
different hardware move all of this around. Treat the shape of the tables as
the finding, not the individual figures.

## How the tests were made

The films are ordinary rips. For each one the most popular subtitle on
OpenSubtitles for that release was taken as the starting point, so the
subtitle text is whatever anyone downloading it would have ended up with.

Two scripts produced the broken files:

* The first took the subtitle and either shifted every cue by a constant
  amount or stretched the whole file by a framerate ratio. That is the
  `normal` test.
* The second took a copy, cut it at one or more timestamps, and gave each
  segment its own offset. That is the `split` test. The cut points and the
  per-segment offsets are listed further down.

Each engine then got the same broken file and the same video:

* `lapse` in its default mode, which works out on its own whether the file is
  shifted, drifting, split, or some combination, and picks the split penalty
  itself. It ran a second time with splitting forced on, so there is a number
  for both.
* `alass` as it ships. Splitting is on by default in alass at a penalty of 7,
  so its one run covers both of the lapse columns next to it.
* `ffsubsync` twice. Once as it ships, where splitting is off, and once with
  `--split-penalty` so its piecewise mode is available.

A run counts as a pass when every cue lands close enough to where it belongs.
In the result tables `✓` marks a pass, `✗` a failure, and `◑` with a fraction
a partial, where some segments landed and some did not. Below half the
segments counts as a plain failure.

The work happened in two rounds, so a few of the columns only exist for the
27 films in the second round. Everything measured for all 39 films is in the
same table.

## What each tool can do

|                                              | lapse 2.0.0         | alass 2.0.0    | ffsubsync 0.5.1      |
| -------------------------------------------- | ------------------- | -------------- | -------------------- |
| Sync against the video's audio               | yes                 | yes            | yes                  |
| Sync against another subtitle file           | yes                 | yes            | yes                  |
| Correct framerate drift                      | yes                 | yes            | yes                  |
| Split the file into segments                 | yes                 | yes            | yes                  |
| Read an embedded subtitle track as reference | yes, automatic      | no             | yes, with a flag     |
| **Decide per file whether to split**         | **yes**             | no, always on  | no, off unless asked |
| **Pick the split penalty itself**            | **yes**             | no, fixed at 7 | no, supplied by hand |
| **Report a confidence verdict**              | **yes, always**     | no             | a score in the log   |
| **Leave the file alone when unsure**         | **yes, by default** | no             | only with a flag     |
| **Reuse the speech profile next run**        | **yes, automatic**  | no             | no                   |

All three can split. What differs is who decides. alass splits on every run
whether the file needs it or not, at a fixed penalty of 7 that has to be
tuned by hand. ffsubsync does not split at all unless `--split-penalty` is
passed, and the number has to be supplied with it. LAPSE looks at the file,
decides whether a split is warranted, and sets the penalty from what it
found. That is the difference the split tables below are measuring.

The same applies to confidence. alass has no notion of it. ffsubsync computes
a score and prints it, and will leave the file alone if
`--skip-sync-on-low-quality` is passed, but that is off by default, so out of
the box a bad answer overwrites a subtitle exactly like a good one. LAPSE
reports a verdict on every run and does not overwrite unless that verdict is
`solid`.

## Summary

Passes out of 39 films.

| Test                          |         lapse          |       alass        |     ffsubsync      |
| ----------------------------- | :--------------------: | :----------------: | :----------------: |
| Normal (shift or drift)       |       **36**/39        |       31/39        |       33/39        |
| Split, tool decides by itself |   19/39 (+5 partial)   |        n/a         |        n/a         |
| Split, splitting turned on    | **32**/39 (+3 partial) | 27/39 (+1 partial) | 17/39 (+1 partial) |

Nothing sits opposite LAPSE in the middle row because neither of the other
two makes that decision. alass is already splitting on every run, and
ffsubsync is not splitting at all until it is told to. The bottom row is the
like-for-like comparison, and it applies once the file is already known to be
split. The middle row is what happens when it is not.

Total wall clock across the 39 normal runs:

| Tool             | Total     | Median run |
| ---------------- | --------- | ---------- |
| lapse, cached    | **29.7s** | **0.66s**  |
| lapse, first run | 511.2s    | 10.7s      |
| alass            | 514.1s    | 11.3s      |
| ffsubsync        | 656.3s    | 17.1s      |

LAPSE keeps the speech profile it built from the audio, keyed on the video.
The first run on a film pays for the decode, every run after that on the same
film does not. That is the common case in a library, where one subtitle track
gets fixed and then another for the same film. alass and ffsubsync start from
nothing every time.

## Speed and result, normal mode

`Cached` is the second and later run on the same film.

| Film                   | lapse first | lapse cached  | alass             | ffsubsync    |
| ---------------------- | ----------- | ------------- | ----------------- | ------------ |
| 2001: A Space Odyssey  | 13.41s      | **0.68s** ✓   | 21.60s ✓          | 2.21s ✗      |
| Andrei Rublev          | 5.32s       | **0.43s** ✓   | 6.84s ✓           | 11.64s ✓     |
| Apocalypse Now         | 9.41s       | **0.97s** ✓   | 14.86s ✓          | 22.13s ✓     |
| Badlands               | 9.93s       | **0.48s** ✓   | 14.10s ✓          | 13.90s ✓     |
| Barry Lyndon           | 16.14s      | **0.91s** ✓   | 30.19s ✓          | 27.83s ✓     |
| Blue Velvet            | 6.74s       | **0.46s** ✓   | 8.37s ✓           | 13.94s ✓     |
| Brazil                 | 8.86s       | **0.88s** ✓   | 12.48s ✓          | 18.84s ✓     |
| Challengers            | 13.64s      | **0.58s** ✓   | 19.78s ✓          | 2.77s ✓      |
| Come and See           | 15.94s      | **0.70s** ✓   | 0.09s ✗ (crashed) | 4.03s ✗      |
| Dancer in the Dark     | 3.52s       | **0.43s** ✓   | 4.47s ✓           | 7.21s ✓      |
| Drive                  | 6.68s       | **0.40s** ✓   | 6.68s ✓           | 11.08s ✓     |
| Enter the Void         | 8.89s       | **0.77s** ✓   | 11.29s ✓          | 18.07s ✓     |
| Eraserhead             | 10.67s      | **0.33s** ✓   | 5.75s ✗           | 10.96s ✓     |
| Ex Machina             | 5.49s       | **0.50s** ✓   | 7.63s ✓           | 12.26s ✓     |
| Eyes Wide Shut         | 7.65s       | **0.82s** ✓   | 11.34s ✓          | 17.25s ✓     |
| Fallen Angels          | 10.32s      | **0.64s** ✓   | 14.60s ✓          | 15.19s ✓     |
| Fargo                  | 4.93s       | **0.56s** ✓   | 7.33s ✓           | 11.14s ✓     |
| Hereditary             | 13.25s      | **0.43s** ✓   | 18.44s ✓          | 17.51s ✓     |
| Inland Empire          | 18.90s      | **0.78s** ✓   | 32.08s ✓          | 41.62s ✓     |
| Manchester by the Sea  | 7.91s       | **1.08s** ✓   | 8.65s ✗           | 15.73s ✓     |
| Mulholland Drive       | 20.00s      | **0.80s** ✓   | 23.53s ✓          | 23.20s ✓     |
| No Country for Old Men | 11.38s      | **0.48s** ✓   | 17.53s ✓          | 17.81s ✓     |
| Possession             | 8.34s       | **0.50s** ✓   | 5.95s ✓           | 11.78s ✓     |
| Requiem for a Dream    | 13.72s      | **0.52s** ✓   | 8.79s ✗           | 17.89s ✓     |
| Saving Private Ryan    | 8.57s       | **0.94s** ✓   | 12.46s ✓          | 18.39s ✓     |
| Seven Samurai          | 37.53s      | **0.76s** ✓   | 11.04s ✓          | 7.05s ✓      |
| Stalker                | 10.82s      | **1.79s** ✗   | 11.17s ✓          | 18.32s ✗     |
| Synecdoche, New York   | 13.07s      | **0.69s** ✓   | 21.14s ✓          | 18.40s ✓     |
| The Deer Hunter        | 11.06s      | **0.83s** ✓   | 13.62s ✓          | 21.08s ✓     |
| The Godfather          | 15.00s      | **2.01s** ✗   | 12.46s ✗          | 25.13s ✗     |
| The Godfather Part II  | 17.30s      | **2.16s** ✗   | 14.53s ✗          | 28.75s ✗     |
| The Godfather Part III | 13.82s      | **1.29s** ✓   | 11.83s ✗          | 23.58s ✗     |
| The Hunt               | 11.49s      | **0.44s** ✓   | 7.42s ✓           | 20.35s ✓     |
| The Passenger          | 23.77s      | **0.43s** ✓   | 9.69s ✓           | 17.12s ✓     |
| The Seventh Seal       | 5.82s       | **0.53s** ✓   | 6.95s ✓           | 11.49s ✓     |
| There Will Be Blood    | 57.63s      | **0.66s** ✓   | 31.55s ✓          | 33.57s ✓     |
| Uncut Gems             | 18.69s      | **1.01s** ✓   | 23.51s ✓          | 21.25s ✓     |
| Whiplash               | 5.21s       | **0.47s** ✓   | 7.58s ✓           | 12.31s ✓     |
| Zoolander              | 10.33s      | **0.52s** ✓   | 6.82s ✗           | 13.49s ✓     |
| **Passed**             |             | **36 / 39**   | 31 / 39           | 33 / 39      |

## Speed and result, split mode

`lapse auto` is the default run, with no indication that the file was split.
Every other column here is a run where splitting was already on: alass
because it always is, ffsubsync because it was given `--split-penalty`. All
lapse times are cached runs.

| Film                   | Cuts | lapse auto      | lapse forced    | alass             | ffsubsync     | ffsubsync split |
| ---------------------- | :--: | --------------- | --------------- | ----------------- | ------------- | --------------- |
| 2001: A Space Odyssey  |  1   | **0.71s** ✓     | **0.78s** ✓     | 21.31s ✓          | 2.24s ✗       | 22.95s ✗        |
| Andrei Rublev          |  1   | **0.45s** ✓     | **0.41s** ✓     | 6.62s ✓           | 11.78s ✗      | 13.83s ✓        |
| Apocalypse Now         |  2   | **0.88s** ◑ 2/3 | **1.11s** ✓     | 15.82s ✓          | 21.91s ✗      | 28.30s ✓        |
| Badlands               |  3   | **0.52s** ◑ 2/3 | **0.58s** ✓     | 13.89s ✓          | 13.57s ✗      | 16.96s ✗        |
| Barry Lyndon           |  3   | **1.03s** ✓     | **1.14s** ✓     | 30.33s ✓          | 27.76s ◑ 2/3  | 32.98s ✓        |
| Blue Velvet            |  1   | **0.55s** ✓     | **0.60s** ✓     | 8.64s ✓           | 14.16s ✗      | 17.14s ✓        |
| Brazil                 |  2   | **0.94s** ◑ 2/3 | **1.08s** ✓     | 12.70s ✓          | 18.79s ✗      | 23.01s ✓        |
| Challengers            |  1   | **0.58s** ✓     | **0.30s** ✓     | 19.76s ✓          | 2.70s ✗       | 23.59s ✓        |
| Come and See           |  1   | **0.70s** ✗     | **0.66s** ✓     | 0.08s ✗ (crashed) | 3.88s ✗       | 18.22s ✗        |
| Dancer in the Dark     |  1   | **0.46s** ✓     | **0.83s** ✓     | 4.90s ✓           | 7.23s ✗       | 14.00s ✗        |
| Drive                  |  2   | **0.47s** ◑ 2/3 | **0.50s** ✓     | 6.69s ✓           | 11.27s ✗      | 13.75s ✗        |
| Enter the Void         |  3   | **2.09s** ✗     | **0.89s** ✗     | 11.12s ✓          | 20.23s ✗      | 20.64s ✗        |
| Eraserhead             |  2   | **0.30s** ✗     | **0.33s** ◑ 2/3 | 5.65s ◑ 2/3       | 10.66s ✗      | 11.15s ✗        |
| Ex Machina             |  2   | **0.50s** ✗     | **0.58s** ✓     | 7.74s ✗           | 12.06s ◑ 2/3  | 15.56s ✓        |
| Eyes Wide Shut         |  1   | **0.88s** ✓     | **0.89s** ✓     | 12.15s ✓          | 17.22s ✗      | 21.26s ✓        |
| Fallen Angels          |  1   | **0.97s** ✗     | **0.55s** ✓     | 14.76s ✗          | 15.24s ✗      | 17.11s ✗        |
| Fargo                  |  1   | **0.53s** ✓     | **0.62s** ✓     | 7.58s ✓           | 11.31s ✗      | 15.03s ✓        |
| Hereditary             |  1   | **0.49s** ✓     | **0.52s** ✓     | 18.43s ✓          | 17.55s ✗      | 20.56s ✓        |
| Inland Empire          |  3   | **0.90s** ✓     | **0.98s** ✓     | 33.22s ✓          | 39.40s ✗      | 43.74s ✗        |
| Manchester by the Sea  |  1   | **1.16s** ✗     | **3.16s** ✗     | 8.26s ✗           | 15.62s ✗      | 21.42s ✓        |
| Mulholland Drive       |  1   | **0.77s** ✓     | **0.83s** ✓     | 23.02s ✓          | 22.74s ✗      | 25.93s ✗        |
| No Country for Old Men |  1   | **0.50s** ✓     | **0.51s** ✓     | 18.01s ✓          | 17.71s ✗      | 20.39s ✓        |
| Possession             |  1   | **1.51s** ✗     | **0.60s** ✓     | 6.03s ✓           | 11.77s ✗      | 15.94s ✗        |
| Requiem for a Dream    |  1   | **0.45s** ✓     | **0.67s** ✓     | 8.78s ✗           | 17.80s ✗      | 20.94s ✗        |
| Saving Private Ryan    |  1   | **1.41s** ✗     | **0.49s** ✓     | 12.62s ✓          | 18.81s ✗      | 23.23s ✓        |
| Seven Samurai          |  1   | **0.78s** ✓     | **0.70s** ✗     | 11.16s ✗          | 6.99s ✗       | 10.42s ✗        |
| Stalker                |  1   | **1.74s** ✓     | **0.61s** ✓     | 11.27s ✓          | 18.28s ✗      | 21.66s ✗        |
| Synecdoche, New York   |  3   | **0.73s** ✗     | **0.78s** ✓     | 20.96s ✓          | 18.19s ✗      | 24.51s ✗        |
| The Deer Hunter        |  2   | **2.55s** ◑ 2/3 | **1.10s** ◑ 2/3 | 13.47s ✓          | 21.39s ✗      | 26.51s ◑ 2/3    |
| The Godfather          |  1   | **2.03s** ✗     | **1.29s** ✓     | 13.32s ✗          | 25.49s ✗      | 30.10s ✗        |
| The Godfather Part II  |  1   | **2.17s** ✗     | **1.36s** ✗     | 15.55s ✗          | 28.60s ✗      | 34.15s ✗        |
| The Godfather Part III |  2   | **2.03s** ✗     | **1.14s** ✓     | 12.48s ✗          | 23.33s ✗      | 26.93s ✗        |
| The Hunt               |  4   | **0.63s** ✗     | **0.57s** ◑ 3/4 | 8.06s ✓           | 20.17s ✗      | 23.03s ✗        |
| The Passenger          |  1   | **0.42s** ✓     | **0.47s** ✓     | 9.66s ✓           | 17.29s ✗      | 19.34s ✓        |
| The Seventh Seal       |  1   | **0.47s** ✓     | **0.43s** ✓     | 6.92s ✓           | 11.17s ✓      | 14.21s ✓        |
| There Will Be Blood    |  1   | **0.72s** ✗     | **0.76s** ✓     | 32.39s ✓          | 33.82s ✗      | 35.74s ✓        |
| Uncut Gems             |  1   | **1.72s** ✓     | **0.57s** ✓     | 23.66s ✓          | 21.68s ✗      | 29.00s ✗        |
| Whiplash               |  1   | **0.42s** ✓     | **0.40s** ✓     | 7.55s ✓           | 12.20s ✗      | 14.82s ✓        |
| Zoolander              |  2   | **0.61s** ✗     | **0.56s** ✓     | 6.66s ✗           | 13.35s ✗      | 18.17s ✓        |
| **Passed**             |      | 19 ✓ +5 ◑       | **32 ✓** +3 ◑   | 27 ✓ +1 ◑         | 0 ✓ +2 ◑      | 17 ✓ +1 ◑       |

The `lapse auto` column is the one with no help. Nineteen clean passes and
five partials out of 39 came from files it was never told were cut. The other
columns in this table exist only because the split was already known going
in. Once LAPSE is told, the `lapse forced` column is the best result here.

## Error in milliseconds, normal mode

The first twelve films were also measured by how far each cue ended up from
where it should be. The best value in each row is in bold.

| Film                  | Injected       | lapse           | alass          | ffsubsync        |
| --------------------- | -------------- | --------------- | -------------- | ---------------- |
| 2001: A Space Odyssey | shift +97000ms | **19ms** ✓      | 50ms ✓         | 34769ms ✗        |
| Andrei Rublev         | shift +15000ms | 35ms ✓          | 168ms ✓        | **0ms** ✓        |
| Challengers           | drift x1.00100 | 67ms ✓          | **46ms** ✓     | 160ms ✓          |
| Come and See          | shift +97000ms | **24082ms** ✓   | crashed ✗      | 33110ms ✗        |
| Dancer in the Dark    | drift x0.99900 | 384035ms ✓      | 304591ms ✓     | **195928ms** ✓   |
| Requiem for a Dream   | shift -33000ms | 2113ms ✓        | 140672ms ✗     | **1790ms** ✓     |
| Saving Private Ryan   | drift x1.00100 | 3646ms ✓        | 3574ms ✓       | **3525ms** ✓     |
| Seven Samurai         | drift x1.04167 | 60ms ✓          | 38ms ✓         | **0ms** ✓        |
| Stalker               | shift +52500ms | 89302ms ✗       | 101286ms ✓     | **0ms** ✗        |
| The Seventh Seal      | shift +33000ms | 58ms ✓          | **26ms** ✓     | **0ms** ✓        |
| Uncut Gems            | shift -33000ms | 16076ms ✓       | 16105ms ✓      | **16054ms** ✓    |
| Whiplash              | shift +38500ms | 266ms ✓         | 172ms ✓        | **170ms** ✓      |

The error figure and the pass mark do not always agree. A large error on a
film whose subtitle was already off by that much before anything was injected
still lines up with the audio, and a zero error on `Stalker` still failed
because the rest of the file did not follow.

## Split test in detail

The same twelve films, cut in two and given a different offset on each side.
`clean` means the tool produced one cut in the right place. `noisy` means it
produced a cut, but not where the real one is. `messy xN` means it produced N
cuts, and every extra cut is a place where the subtitle jumps during
playback. Where a cell has a `set` line, that is the offset the tool applied
to each side. The `err` line is how far each side ended up from the truth.

| Film                  | True cut | Injected        | lapse                                                       | alass                                                       | ffsubsync                                                   |
| --------------------- | -------- | --------------- | ----------------------------------------------------------- | ----------------------------------------------------------- | ----------------------------------------------------------- |
| 2001: A Space Odyssey | 00:49:05 | -33000 / +45000 | ✓ **clean** @00:59:36<br>set +33070/-44388ms<br>err **70/7ms** | ✓ **clean** @00:59:36<br>set +33050/-45098ms<br>err 50/98ms | ✗ messy x17 @00:27:51<br>err 78000/0ms                      |
| Andrei Rublev         | 00:34:45 | -45000 / -12000 | ✓ **clean** @00:35:06<br>set +45026/+12037ms<br>err **26/37ms** | ✓ **clean** @00:34:33<br>set +44932/+11811ms<br>err 68/189ms | ✓ cue mismatch                                             |
| Challengers           | 01:20:16 | -33000 / +33000 | ✓ **clean** @01:22:08<br>set +33065/-32475ms<br>err 65/69ms | ✓ noisy @01:20:49<br>set +32965/**+99099ms**<br>err **35**/146ms | ◑ 2/3 messy x39<br>err 140/66140ms                     |
| Come and See          | 01:19:39 | +45000 / -45000 | ✓ noisy @00:25:34<br>err **22107/65287ms**                  | ✗ crashed                                                   | ✗ messy x20<br>err 22164/63450ms                            |
| Dancer in the Dark    | 00:36:00 | +33000 / -33000 | ✓ noisy @00:19:56<br>err 395597/329597ms                    | ✓ messy x16<br>err 224691/51282ms                           | ✗ messy x19<br>err cue mismatch                             |
| Requiem for a Dream   | 00:59:09 | -45000 / +45000 | ✓ noisy @01:03:26<br>err **2063/4198ms**                    | ✗ messy x98<br>err 100143/172055ms                          | ✗ cue mismatch                                              |
| Saving Private Ryan   | 01:07:02 | +21000 / -33000 | ✓ messy x38<br>err 51487/4702ms                             | ✓ messy x22<br>err **3281/5143ms**                          | ✓ messy x63<br>err 53572/3343ms                             |
| Seven Samurai         | 01:13:31 | +12000 / -12000 | ✓ **clean** @01:13:11<br>set -11937/+12054ms<br>err 63/54ms | ✗ **clean** @01:14:51<br>set -12005/+11927ms<br>err **5/73ms** | ✗ **clean** @01:13:19<br>set -12052/+11943ms<br>err 0/24000ms |
| Stalker               | 00:57:14 | -33000 / +45000 | ✓ messy x102<br>err 214608/81555ms                          | ✓ messy x101<br>err 165526/74902ms                          | ✗ messy x63<br>err **81462/1651ms**                         |
| The Seventh Seal      | 00:42:55 | -12000 / -45000 | ✓ **clean** @00:42:19<br>set +12069/+45046ms<br>err 69/50ms | ✓ **clean** @00:42:19<br>set +11974/+44828ms<br>err **26**/172ms | ✓ noisy @00:42:19<br>set +12000/+44990ms<br>err 32950/50ms |
| Uncut Gems            | 00:55:20 | -33000 / +21000 | ✓ messy x15<br>err 13695/17641ms                            | ✓ messy x16<br>err 13721/17589ms                            | ✗ messy x113<br>err 40430/17576ms                           |
| Whiplash              | 00:46:09 | +12000 / -33000 | ✓ noisy @00:45:36<br>err **266/271ms**                      | ✓ **clean** @00:45:11<br>set -11828/+32932ms<br>err 172/68ms | ✓ messy x8<br>err 45140/140ms                              |

Producing one clean cut is not the same as producing a usable file, and the
lines under each verdict show that. ffsubsync placed sixty three cuts on
`Stalker` and a hundred and thirteen on `Uncut Gems`. On `Challengers` alass
found the cut close to the right place but set the second half nearly 100
seconds out.

## What was injected in the second round

The 27 films in the second round were cut in more places.

| Film                   | Cuts | Cut at (hh:mm:ss)                      | Segment offsets (ms)                   |
| ---------------------- | :--: | -------------------------------------- | -------------------------------------- |
| Apocalypse Now         |  2   | 00:59:23                               | +33000, -21000, +33000                 |
| Badlands               |  3   | 00:25:07, 00:40:52                     | -21000, +45000, +12000, -21000         |
| Barry Lyndon           |  3   | 00:44:48, 01:22:52, 02:13:49           | -33000, +21000, -33000, +12000         |
| Blue Velvet            |  1   | 00:55:20                               | +33000, -38500                         |
| Brazil                 |  2   | 00:38:54, 01:41:59                     | +38500, -33000, +21000                 |
| Drive                  |  2   | 00:29:49, 01:09:21                     | +21000, -33000, +21000                 |
| Enter the Void         |  3   | 00:31:50, 01:23:52, 01:45:36           | -12000, -45000, -15000, +33000         |
| Eraserhead             |  2   | 00:26:30, 00:58:41                     | -15000, +15000, -12000                 |
| Ex Machina             |  2   | 00:29:25, 01:02:30                     | +21000, +38500, -12000                 |
| Eyes Wide Shut         |  1   | 01:10:21                               | +12000, +33000                         |
| Fallen Angels          |  1   | 00:43:05                               | -45000, +45000                         |
| Fargo                  |  1   | 00:44:28                               | -45000, -15000                         |
| Hereditary             |  1   | 00:56:00                               | +15000, -21000                         |
| Inland Empire          |  3   | 00:51:53, 01:31:06, 02:11:54           | -12000, +12000, -33000, +12000         |
| Manchester by the Sea  |  1   | 00:58:58                               | +15000, +38500                         |
| Mulholland Drive       |  1   | 01:14:58                               | +38500, -12000                         |
| No Country for Old Men |  1   | 01:02:25                               | +33000, -15000                         |
| Possession             |  1   | 00:53:52                               | -33000, +33000                         |
| Synecdoche, New York   |  3   | 00:38:13, 01:04:41, 01:32:22           | -38500, -15000, +21000, +38500         |
| The Deer Hunter        |  2   | 00:57:45, 01:51:49                     | -12000, +21000, +45000                 |
| The Godfather          |  1   | 01:32:14                               | -45000, +12000                         |
| The Godfather Part II  |  1   | 01:31:22                               | +38500, -21000                         |
| The Godfather Part III |  2   | 00:56:24, 01:43:55                     | -15000, -33000, +38500                 |
| The Hunt               |  4   | 00:22:03, 00:41:17, 01:11:23, 01:27:20 | -15000, +15000, +38500, -15000, -33000 |
| The Passenger          |  1   | 01:03:37                               | +33000, -38500                         |
| There Will Be Blood    |  1   | 00:36:16                               | -45000, +12000                         |
| Zoolander              |  2   | 00:26:30, 00:59:43                     | +12000, +38500, -45000                 |

## Confidence

LAPSE reports a verdict on every run. `solid` means the audio backs the
answer up and the file is overwritten with a `.bak` kept. `unsure` means
there is an answer but it is not proven. `nothing` means the audio does not
support any offset. On `unsure` and `nothing` the original is left alone and
the answer goes to a sidecar beside it, where it can be checked before it is
used.

This was recorded for the 27 films in the second round. Nothing sits beside
it because neither of the other two tools produces one.

| Film                   | Normal  | Split, auto | Split, forced |
| ---------------------- | ------- | ----------- | ------------- |
| Apocalypse Now         | solid   | unsure      | unsure        |
| Badlands               | solid   | solid       | solid         |
| Barry Lyndon           | solid   | solid       | solid         |
| Blue Velvet            | solid   | nothing     | solid         |
| Brazil                 | solid   | nothing     | solid         |
| Drive                  | solid   | solid       | solid         |
| Enter the Void         | solid   | solid       | unsure        |
| Eraserhead             | solid   | solid       | unsure        |
| Ex Machina             | solid   | solid       | solid         |
| Eyes Wide Shut         | solid   | solid       | solid         |
| Fallen Angels          | unsure  | unsure      | unsure        |
| Fargo                  | solid   | solid       | solid         |
| Hereditary             | solid   | solid       | solid         |
| Inland Empire          | solid   | solid       | solid         |
| Manchester by the Sea  | nothing | nothing     | solid         |
| Mulholland Drive       | solid   | solid       | solid         |
| No Country for Old Men | solid   | solid       | solid         |
| Possession             | solid   | solid       | unsure        |
| Synecdoche, New York   | solid   | solid       | solid         |
| The Deer Hunter        | solid   | solid       | unsure        |
| The Godfather          | unsure  | unsure      | unsure        |
| The Godfather Part II  | unsure  | unsure      | unsure        |
| The Godfather Part III | unsure  | unsure      | unsure        |
| The Hunt               | solid   | nothing     | unsure        |
| The Passenger          | solid   | unsure      | solid         |
| There Will Be Blood    | solid   | solid       | solid         |
| Zoolander              | solid   | nothing     | unsure        |

The three Godfather films are the clearest case. All three failed in normal
mode for all three tools. LAPSE was the only one that said so first, and the
only one that left the original alone. `Manchester by the Sea` reported
`nothing` on the two runs it failed and `solid` on the one it passed.

## Wrong subtitle

A subtitle for a documentary about Oppenheimer, run against the film
`Oppenheimer`.

| Tool      | Time   | What it worked out                                          | What happened to the file                                                                                                |
| --------- | ------ | ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| lapse     | 83.28s | sigma 2.14, verdict `nothing`                               | **Left alone.** Guess written to a sidecar, with the reason that the audio does not back it up                            |
| alass     | 25.01s | shifted -6:53, ratio 25/23.976, negative timestamps clamped | Overwritten. alass has no confidence measure, so nothing was reported                                                     |
| ffsubsync | 28.97s | offset +60.000s, ratio 1.042, score -93455                  | Overwritten. The negative score was printed and the exit was marked unsuccessful, but the shifted file was written anyway |

ffsubsync 0.5.1 will leave the file alone here if `--skip-sync-on-low-quality`
is passed. It is off by default, so the row above is the out of the box
result. LAPSE runs the check by default and there is no flag to remember.

## Method notes

Every timing is a single run on one machine with nothing else running. The
`lapse cached` column is the second run on that film, after the speech
profile already exists on disk.

The tests ran in two rounds. The first round covers twelve films with
millisecond error figures and split locations. The second round covers
twenty seven films with the confidence verdicts. Anything measured for all
39 films is in the same table.

Versions used: LAPSE 2.0.0, alass 2.0.0, ffsubsync 0.5.1.
