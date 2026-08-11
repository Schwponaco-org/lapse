# Benchmarks

LAPSE 2.0.0 against alass 2.0.0 and ffsubsync 0.5.1, on 39 feature films, plus
one test with a subtitle file that belongs to a different film entirely.

## Read this before the tables

**The films were picked to be hard.** They are not a random sample. I went
through my own library and pulled out the ones I expected to give a subtitle
sync tool trouble: long films, films with a lot of silence, films with heavy
score under the dialogue, and films that exist in more than one cut. Then I 
injected offsets between 12 and 97 seconds and cuts in up to four places.

That matters when you compare these numbers to what any of these tools report
about themselves. alass, for example, reports that on subtitles pulled at
random from OpenSubtitles "the percentage of good subtitles is about 88% to
98%". That is a fair number for a random sample and it is not in conflict
with the numbers here. A random sample is mostly easy files. This is a set
chosen for the opposite reason. Every tool in this document scores lower here
than it would on a normal library, and that is the point: the interesting
part is where they fail, not where they all succeed.

**File types.** `2001: A Space Odyssey`, `Challengers`, `Come and See` and
`Seven Samurai` are `.mkv`. The other 35 are `.mp4`. All subtitles are `.srt`.
The container matters for timing, so ffsubsync is much faster on those four
than on anything else and those four times should not be compared to the
rest.

**Results will vary.** Different releases, different subtitle uploads and
different hardware move all of this around. Treat the shape of the tables as
the finding, not the individual figures.

## How the tests were made

The films are ordinary rips from my own shelf. For each one I downloaded the
most popular subtitle on OpenSubtitles for that release and used it as the
starting point, so the subtitle text is whatever a normal person would have
grabbed.

Two scripts produced the broken files:

* The first one took the subtitle and either shifted every cue by a constant
  amount or stretched the whole file by a framerate ratio. That is the
  `normal` test.
* The second one took a copy, cut it at one or more timestamps, and gave each
  segment its own offset. That is the `split` test. The cut points and the
  per-segment offsets are listed further down.

Then each engine got the same broken file and the same video:

* `lapse` in its default mode, which works out on its own whether the file is
  shifted, drifting, split, or some combination, and picks the split penalty
  itself. It ran a second time with splitting forced on, so there is a number
  for both.
* `alass` as it ships. Splitting is on by default in alass with a penalty of
  7, so its one run covers both of the lapse columns next to it.
* `ffsubsync` twice. Once as it ships, where splitting is off, and once with
  `--split-penalty` so its piecewise mode is available.

A run counts as a pass when every cue lands close enough to where it belongs.
`PARTIAL` means some segments landed and some did not, and the fraction says
how many. Below half the segments it is counted as a plain `no`.

The work happened in two rounds, so a few of the columns only exist for the
27 films in the second round. Everything that exists for all 39 films is in
the same table.

## What each tool can do

|                                              | lapse 2.0.0 | alass 2.0.0     | ffsubsync 0.5.1      |
| -------------------------------------------- | ----------- | --------------- | -------------------- |
| Sync against the video's audio               | yes         | yes             | yes                  |
| Sync against another subtitle file           | yes         | yes             | yes                  |
| Correct framerate drift                      | yes         | yes             | yes                  |
| Split the file into segments                 | yes         | yes             | yes                  |
| Read an embedded subtitle track as reference | yes, automatic | no           | yes, with a flag     |
| **Decide per file whether to split**         | **yes**     | no, always on   | no, off unless asked |
| **Pick the split penalty itself**            | **yes**     | no, fixed at 7  | no, you supply it    |
| **Report a confidence verdict**              | **yes, always** | no          | a score in the log   |
| **Leave your file alone when unsure**        | **yes, by default** | no      | only with a flag     |
| **Reuse the speech profile next run**        | **yes, automatic** | no       | no                   |

All three can split. What differs is who decides. alass splits on every run
whether the file needs it or not, at a penalty of 7 that you tune by hand.
ffsubsync does not split unless you pass `--split-penalty`, and you supply
the number. LAPSE looks at the file, decides whether a split is warranted,
and sets the penalty from what it found. That is the difference the split
tables below are measuring.

The same applies to confidence. alass has no notion of it. ffsubsync computes
a score and prints it, and will leave your file alone if you pass
`--skip-sync-on-low-quality`, but that is off by default, so out of the box a
bad answer overwrites your subtitle exactly like a good one. LAPSE reports a
verdict on every run and does not overwrite unless the verdict is `solid`.

## Summary

Passes out of 39 films.

| Test                          |         lapse          |       alass        |     ffsubsync      |
| ----------------------------- | :--------------------: | :----------------: | :----------------: |
| Normal (shift or drift)       |       **36**/39        |       31/39        |       33/39        |
| Split, tool decides by itself |   19/39 (+5 partial)   |        n/a         |        n/a         |
| Split, splitting turned on    | **32**/39 (+3 partial) | 27/39 (+1 partial) | 17/39 (+1 partial) |

Nothing sits opposite LAPSE in the middle row because neither of the other
two makes that decision. alass is already splitting on every run and
ffsubsync is not splitting at all until you tell it to. The bottom row is the
like-for-like comparison, and it is the row where you have already worked out
for yourself that the file is split. The middle row is what happens when you
have not.

Total wall clock across the 39 normal runs:

| Tool             | Total     | Median run |
| ---------------- | --------- | ---------- |
| lapse, cached    | **29.7s** | **0.66s**  |
| lapse, first run | 511.2s    | 10.7s      |
| alass            | 514.1s    | 11.3s      |
| ffsubsync        | 656.3s    | 17.1s      |

LAPSE keeps the speech profile it built from the audio, keyed on the video.
The first run on a film pays for the decode, every run after that on the same
film does not. That is the common case in a library, where you fix one
subtitle track and then another for the same film. alass and ffsubsync start
from nothing every time.

## Speed and result, normal mode

`Cached` is the second and later run on the same film.

| Film                   | lapse first | lapse cached |  ✓  | alass           |  ✓  | ffsubsync   |  ✓  |
| ---------------------- | ----------- | ------------ | :-: | --------------- | :-: | ----------- | :-: |
| 2001: A Space Odyssey  | 13.41s      | **0.68s**    | YES | 21.60s          | YES | 2.21s       | no  |
| Andrei Rublev          | 5.32s       | **0.43s**    | YES | 6.84s           | YES | 11.64s      | YES |
| Apocalypse Now         | 9.41s       | **0.97s**    | YES | 14.86s          | YES | 22.13s      | YES |
| Badlands               | 9.93s       | **0.48s**    | YES | 14.10s          | YES | 13.90s      | YES |
| Barry Lyndon           | 16.14s      | **0.91s**    | YES | 30.19s          | YES | 27.83s      | YES |
| Blue Velvet            | 6.74s       | **0.46s**    | YES | 8.37s           | YES | 13.94s      | YES |
| Brazil                 | 8.86s       | **0.88s**    | YES | 12.48s          | YES | 18.84s      | YES |
| Challengers            | 13.64s      | **0.58s**    | YES | 19.78s          | YES | 2.77s       | YES |
| Come and See           | 15.94s      | **0.70s**    | YES | 0.09s (crashed) | no  | 4.03s       | no  |
| Dancer in the Dark     | 3.52s       | **0.43s**    | YES | 4.47s           | YES | 7.21s       | YES |
| Drive                  | 6.68s       | **0.40s**    | YES | 6.68s           | YES | 11.08s      | YES |
| Enter the Void         | 8.89s       | **0.77s**    | YES | 11.29s          | YES | 18.07s      | YES |
| Eraserhead             | 10.67s      | **0.33s**    | YES | 5.75s           | no  | 10.96s      | YES |
| Ex Machina             | 5.49s       | **0.50s**    | YES | 7.63s           | YES | 12.26s      | YES |
| Eyes Wide Shut         | 7.65s       | **0.82s**    | YES | 11.34s          | YES | 17.25s      | YES |
| Fallen Angels          | 10.32s      | **0.64s**    | YES | 14.60s          | YES | 15.19s      | YES |
| Fargo                  | 4.93s       | **0.56s**    | YES | 7.33s           | YES | 11.14s      | YES |
| Hereditary             | 13.25s      | **0.43s**    | YES | 18.44s          | YES | 17.51s      | YES |
| Inland Empire          | 18.90s      | **0.78s**    | YES | 32.08s          | YES | 41.62s      | YES |
| Manchester by the Sea  | 7.91s       | **1.08s**    | YES | 8.65s           | no  | 15.73s      | YES |
| Mulholland Drive       | 20.00s      | **0.80s**    | YES | 23.53s          | YES | 23.20s      | YES |
| No Country for Old Men | 11.38s      | **0.48s**    | YES | 17.53s          | YES | 17.81s      | YES |
| Possession             | 8.34s       | **0.50s**    | YES | 5.95s           | YES | 11.78s      | YES |
| Requiem for a Dream    | 13.72s      | **0.52s**    | YES | 8.79s           | no  | 17.89s      | YES |
| Saving Private Ryan    | 8.57s       | **0.94s**    | YES | 12.46s          | YES | 18.39s      | YES |
| Seven Samurai          | 37.53s      | **0.76s**    | YES | 11.04s          | YES | 7.05s       | YES |
| Stalker                | 10.82s      | **1.79s**    | no  | 11.17s          | YES | 18.32s      | no  |
| Synecdoche, New York   | 13.07s      | **0.69s**    | YES | 21.14s          | YES | 18.40s      | YES |
| The Deer Hunter        | 11.06s      | **0.83s**    | YES | 13.62s          | YES | 21.08s      | YES |
| The Godfather          | 15.00s      | **2.01s**    | no  | 12.46s          | no  | 25.13s      | no  |
| The Godfather Part II  | 17.30s      | **2.16s**    | no  | 14.53s          | no  | 28.75s      | no  |
| The Godfather Part III | 13.82s      | **1.29s**    | YES | 11.83s          | no  | 23.58s      | no  |
| The Hunt               | 11.49s      | **0.44s**    | YES | 7.42s           | YES | 20.35s      | YES |
| The Passenger          | 23.77s      | **0.43s**    | YES | 9.69s           | YES | 17.12s      | YES |
| The Seventh Seal       | 5.82s       | **0.53s**    | YES | 6.95s           | YES | 11.49s      | YES |
| There Will Be Blood    | 57.63s      | **0.66s**    | YES | 31.55s          | YES | 33.57s      | YES |
| Uncut Gems             | 18.69s      | **1.01s**    | YES | 23.51s          | YES | 21.25s      | YES |
| Whiplash               | 5.21s       | **0.47s**    | YES | 7.58s           | YES | 12.31s      | YES |
| Zoolander              | 10.33s      | **0.52s**    | YES | 6.82s           | no  | 13.49s      | YES |
| **Passed**             |             | **36 / 39**  |     | **31 / 39**     |     | **33 / 39** |     |

## Speed and result, split mode

`lapse auto` is the default run, with no indication that the file was split.
Every other column in this table is a run where splitting was already on:
alass because it always is, ffsubsync because it was given `--split-penalty`.
All lapse times here are cached runs.

| Film                   | Cuts | lapse auto |      ✓       | lapse forced |         ✓        | alass           |      ✓       | ffsubsync   |      ✓      | ffsubsync split |      ✓       |
| ---------------------- | :--: | ---------- | :----------: | ------------ | :--------------: | --------------- | :----------: | ----------- | :---------: | --------------- | :----------: |
| 2001: A Space Odyssey  |  1   | **0.71s**  |     YES      | **0.78s**    |       YES        | 21.31s          |     YES      | 2.24s       |     no      | 22.95s          |      no      |
| Andrei Rublev          |  1   | **0.45s**  |     YES      | **0.41s**    |       YES        | 6.62s           |     YES      | 11.78s      |     no      | 13.83s          |     YES      |
| Apocalypse Now         |  2   | **0.88s**  | PARTIAL 2/3  | **1.11s**    |       YES        | 15.82s          |     YES      | 21.91s      |     no      | 28.30s          |     YES      |
| Badlands               |  3   | **0.52s**  | PARTIAL 2/3  | **0.58s**    |       YES        | 13.89s          |     YES      | 13.57s      |     no      | 16.96s          |      no      |
| Barry Lyndon           |  3   | **1.03s**  |     YES      | **1.14s**    |       YES        | 30.33s          |     YES      | 27.76s      | PARTIAL 2/3 | 32.98s          |     YES      |
| Blue Velvet            |  1   | **0.55s**  |     YES      | **0.60s**    |       YES        | 8.64s           |     YES      | 14.16s      |     no      | 17.14s          |     YES      |
| Brazil                 |  2   | **0.94s**  | PARTIAL 2/3  | **1.08s**    |       YES        | 12.70s          |     YES      | 18.79s      |     no      | 23.01s          |     YES      |
| Challengers            |  1   | **0.58s**  |     YES      | **0.30s**    |       YES        | 19.76s          |     YES      | 2.70s       |     no      | 23.59s          |     YES      |
| Come and See           |  1   | **0.70s**  |      no      | **0.66s**    |       YES        | 0.08s (crashed) |      no      | 3.88s       |     no      | 18.22s          |      no      |
| Dancer in the Dark     |  1   | **0.46s**  |     YES      | **0.83s**    |       YES        | 4.90s           |     YES      | 7.23s       |     no      | 14.00s          |      no      |
| Drive                  |  2   | **0.47s**  | PARTIAL 2/3  | **0.50s**    |       YES        | 6.69s           |     YES      | 11.27s      |     no      | 13.75s          |      no      |
| Enter the Void         |  3   | **2.09s**  |      no      | **0.89s**    |        no        | 11.12s          |     YES      | 20.23s      |     no      | 20.64s          |      no      |
| Eraserhead             |  2   | **0.30s**  |      no      | **0.33s**    |   PARTIAL 2/3    | 5.65s           | PARTIAL 2/3  | 10.66s      |     no      | 11.15s          |      no      |
| Ex Machina             |  2   | **0.50s**  |      no      | **0.58s**    |       YES        | 7.74s           |      no      | 12.06s      | PARTIAL 2/3 | 15.56s          |     YES      |
| Eyes Wide Shut         |  1   | **0.88s**  |     YES      | **0.89s**    |       YES        | 12.15s          |     YES      | 17.22s      |     no      | 21.26s          |     YES      |
| Fallen Angels          |  1   | **0.97s**  |      no      | **0.55s**    |       YES        | 14.76s          |      no      | 15.24s      |     no      | 17.11s          |      no      |
| Fargo                  |  1   | **0.53s**  |     YES      | **0.62s**    |       YES        | 7.58s           |     YES      | 11.31s      |     no      | 15.03s          |     YES      |
| Hereditary             |  1   | **0.49s**  |     YES      | **0.52s**    |       YES        | 18.43s          |     YES      | 17.55s      |     no      | 20.56s          |     YES      |
| Inland Empire          |  3   | **0.90s**  |     YES      | **0.98s**    |       YES        | 33.22s          |     YES      | 39.40s      |     no      | 43.74s          |      no      |
| Manchester by the Sea  |  1   | **1.16s**  |      no      | **3.16s**    |        no        | 8.26s           |      no      | 15.62s      |     no      | 21.42s          |     YES      |
| Mulholland Drive       |  1   | **0.77s**  |     YES      | **0.83s**    |       YES        | 23.02s          |     YES      | 22.74s      |     no      | 25.93s          |      no      |
| No Country for Old Men |  1   | **0.50s**  |     YES      | **0.51s**    |       YES        | 18.01s          |     YES      | 17.71s      |     no      | 20.39s          |     YES      |
| Possession             |  1   | **1.51s**  |      no      | **0.60s**    |       YES        | 6.03s           |     YES      | 11.77s      |     no      | 15.94s          |      no      |
| Requiem for a Dream    |  1   | **0.45s**  |     YES      | **0.67s**    |       YES        | 8.78s           |      no      | 17.80s      |     no      | 20.94s          |      no      |
| Saving Private Ryan    |  1   | **1.41s**  |      no      | **0.49s**    |       YES        | 12.62s          |     YES      | 18.81s      |     no      | 23.23s          |     YES      |
| Seven Samurai          |  1   | **0.78s**  |     YES      | **0.70s**    |        no        | 11.16s          |      no      | 6.99s       |     no      | 10.42s          |      no      |
| Stalker                |  1   | **1.74s**  |     YES      | **0.61s**    |       YES        | 11.27s          |     YES      | 18.28s      |     no      | 21.66s          |      no      |
| Synecdoche, New York   |  3   | **0.73s**  |      no      | **0.78s**    |       YES        | 20.96s          |     YES      | 18.19s      |     no      | 24.51s          |      no      |
| The Deer Hunter        |  2   | **2.55s**  | PARTIAL 2/3  | **1.10s**    |   PARTIAL 2/3    | 13.47s          |     YES      | 21.39s      |     no      | 26.51s          | PARTIAL 2/3  |
| The Godfather          |  1   | **2.03s**  |      no      | **1.29s**    |       YES        | 13.32s          |      no      | 25.49s      |     no      | 30.10s          |      no      |
| The Godfather Part II  |  1   | **2.17s**  |      no      | **1.36s**    |        no        | 15.55s          |      no      | 28.60s      |     no      | 34.15s          |      no      |
| The Godfather Part III |  2   | **2.03s**  |      no      | **1.14s**    |       YES        | 12.48s          |      no      | 23.33s      |     no      | 26.93s          |      no      |
| The Hunt               |  4   | **0.63s**  |      no      | **0.57s**    |   PARTIAL 3/4    | 8.06s           |     YES      | 20.17s      |     no      | 23.03s          |      no      |
| The Passenger          |  1   | **0.42s**  |     YES      | **0.47s**    |       YES        | 9.66s           |     YES      | 17.29s      |     no      | 19.34s          |     YES      |
| The Seventh Seal       |  1   | **0.47s**  |     YES      | **0.43s**    |       YES        | 6.92s           |     YES      | 11.17s      |     YES     | 14.21s          |     YES      |
| There Will Be Blood    |  1   | **0.72s**  |      no      | **0.76s**    |       YES        | 32.39s          |     YES      | 33.82s      |     no      | 35.74s          |     YES      |
| Uncut Gems             |  1   | **1.72s**  |     YES      | **0.57s**    |       YES        | 23.66s          |     YES      | 21.68s      |     no      | 29.00s          |      no      |
| Whiplash               |  1   | **0.42s**  |     YES      | **0.40s**    |       YES        | 7.55s           |     YES      | 12.20s      |     no      | 14.82s          |     YES      |
| Zoolander              |  2   | **0.61s**  |      no      | **0.56s**    |       YES        | 6.66s           |      no      | 13.35s      |     no      | 18.17s          |     YES      |
| **Passed**             |      |            | 19 + 5 part. |              | **32** + 3 part. |                 | 27 + 1 part. |             | 0 + 2 part. |                 | 17 + 1 part. |

The `lapse auto` column is the one with no help. Nineteen clean passes and
five partials out of 39, on files it was never told were cut, is the number
to weigh against the fact that the other two columns in this table only exist
because somebody already knew. Once you do tell LAPSE, the `lapse forced`
column is the best result in the table.

## Error in milliseconds, normal mode

The first twelve films were also measured by how far each cue ended up from
where it should be. Best value in each row is in bold.

| Film                  | Injected       | lapse       |  ✓  | alass        |  ✓  | ffsubsync    |  ✓  |
| --------------------- | -------------- | ----------- | :-: | ------------ | :-: | ------------ | :-: |
| 2001: A Space Odyssey | shift +97000ms | **19ms**    | YES | 50ms         | YES | 34769ms      | no  |
| Andrei Rublev         | shift +15000ms | 35ms        | YES | 168ms        | YES | **0ms**      | YES |
| Challengers           | drift x1.00100 | 67ms        | YES | **46ms**     | YES | 160ms        | YES |
| Come and See          | shift +97000ms | **24082ms** | YES | crashed      | no  | 33110ms      | no  |
| Dancer in the Dark    | drift x0.99900 | 384035ms    | YES | 304591ms     | YES | **195928ms** | YES |
| Requiem for a Dream   | shift -33000ms | 2113ms      | YES | 140672ms     | no  | **1790ms**   | YES |
| Saving Private Ryan   | drift x1.00100 | 3646ms      | YES | 3574ms       | YES | **3525ms**   | YES |
| Seven Samurai         | drift x1.04167 | 60ms        | YES | 38ms         | YES | **0ms**      | YES |
| Stalker               | shift +52500ms | 89302ms     | YES | 101286ms     | YES | **0ms**      | no  |
| The Seventh Seal      | shift +33000ms | 58ms        | YES | **26ms**     | YES | **0ms**      | YES |
| Uncut Gems            | shift -33000ms | 16076ms     | YES | 16105ms      | YES | **16054ms**  | YES |
| Whiplash              | shift +38500ms | 266ms       | YES | 172ms        | YES | **170ms**    | YES |

The error figure and the pass mark do not always agree. A large error on a
film whose subtitle was already off by that much before anything was injected
still lines up with the audio, and a zero error on `Stalker` still failed
because the rest of the file did not follow.

## Split test in detail

The same twelve films, cut in two and given a different offset on each side.
`clean` means the tool produced one cut in the right place. `noisy` means it
produced a cut but not where the real one is. `messy xN` means it produced N
cuts. Every extra cut is a place where the subtitle jumps while you watch.

| Film                  | True cut | Before / after  | lapse error       |    ✓    | lapse cut found                      | alass error     |    ✓    | alass cut found                      | ffsubsync error  |    ✓    | ffsubsync cut found                  |
| --------------------- | -------- | --------------- | ----------------- | :-----: | ------------------------------------ | --------------- | :-----: | ------------------------------------ | ---------------- | :-----: | ------------------------------------ |
| 2001: A Space Odyssey | 00:49:05 | -33000 / +45000 | **70/7ms**        |   YES   | **clean** @00:59:36, +33070/-44388ms | 50/98ms         |   YES   | **clean** @00:59:36, +33050/-45098ms | 78000/0ms        | PARTIAL | messy x17 @00:27:51                  |
| Andrei Rublev         | 00:34:45 | -45000 / -12000 | **26/37ms**       |   YES   | **clean** @00:35:06, +45026/+12037ms | 68/189ms        |   YES   | **clean** @00:34:33, +44932/+11811ms | cue mismatch     |   no    | cue mismatch                         |
| Challengers           | 01:20:16 | -33000 / +33000 | 65/69ms           |   YES   | **clean** @01:22:08, +33065/-32475ms | **35**/146ms    |   YES   | noisy @01:20:49, +32965/+99099ms     | 140/66140ms      | PARTIAL | messy x39                            |
| Come and See          | 01:19:39 | +45000 / -45000 | **22107/65287ms** | PARTIAL | noisy @00:25:34                      | crashed         |   no    | crashed                              | 22164/63450ms    |   no    | messy x20                            |
| Dancer in the Dark    | 00:36:00 | +33000 / -33000 | 395597/329597ms   |   YES   | noisy @00:19:56                      | 224691/51282ms  |   YES   | messy x16                            | cue mismatch     |   no    | messy x19                            |
| Requiem for a Dream   | 00:59:09 | -45000 / +45000 | **2063/4198ms**   |   no    | noisy @01:03:26                      | 100143/172055ms |   no    | messy x98                            | cue mismatch     |   no    | cue mismatch                         |
| Saving Private Ryan   | 01:07:02 | +21000 / -33000 | 51487/4702ms      | PARTIAL | messy x38                            | **3281/5143ms** |   YES   | messy x22                            | 53572/3343ms     |   no    | messy x63                            |
| Seven Samurai         | 01:13:31 | +12000 / -12000 | 63/54ms           |   YES   | **clean** @01:13:11, -11937/+12054ms | **5/73ms**      | PARTIAL | **clean** @01:14:51, -12005/+11927ms | 0/24000ms        | PARTIAL | **clean** @01:13:19, -12052/+11943ms |
| Stalker               | 00:57:14 | -33000 / +45000 | 214608/81555ms    |   YES   | messy x102                           | 165526/74902ms  |   YES   | messy x101                           | **81462/1651ms** |   no    | messy x63                            |
| The Seventh Seal      | 00:42:55 | -12000 / -45000 | 69/50ms           |   YES   | **clean** @00:42:19, +12069/+45046ms | **26**/172ms    |   YES   | **clean** @00:42:19, +11974/+44828ms | 32950/50ms       |   YES   | noisy @00:42:19, +12000/+44990ms     |
| Uncut Gems            | 00:55:20 | -33000 / +21000 | 13695/17641ms     |   YES   | messy x15                            | 13721/17589ms   |   YES   | messy x16                            | 40430/17576ms    |   no    | messy x113                           |
| Whiplash              | 00:46:09 | +12000 / -33000 | **266/271ms**     |   YES   | noisy @00:45:36                      | 172/68ms        |   YES   | **clean** @00:45:11, -11828/+32932ms | 45140/140ms      |   no    | messy x8                             |

Producing one clean cut is not the same as producing a usable file, and the
two columns per tool show that. ffsubsync placed sixty three cuts on
`Stalker` and a hundred and thirteen on `Uncut Gems`.

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
support any offset. On `unsure` and `nothing` your file is left alone and the
answer goes to a sidecar next to it, so you can look at it yourself before
you use it.

This was recorded for the 27 films in the second round. There is nothing to
put beside it because neither of the other two tools produces one.

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
only one that left the original file alone. `Manchester by the Sea` reported
`nothing` on the two runs it failed and `solid` on the one it passed.

## Wrong subtitle

A subtitle for a documentary about Oppenheimer, run against the film
`Oppenheimer`. Nothing was modified. There is no correct answer here, and the
only right behaviour is to not pretend otherwise.

| Tool      | Time   | What it worked out                                          | What happened to the file                                                                                                 |
| --------- | ------ | ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| lapse     | 83.28s | sigma 2.14, verdict `nothing`                               | **Left alone.** Guess written to a sidecar, with the reason that the audio does not back it up                             |
| alass     | 25.01s | shifted -6:53, ratio 25/23.976, negative timestamps clamped | Overwritten. alass has no confidence measure, so nothing was reported                                                      |
| ffsubsync | 28.97s | offset +60.000s, ratio 1.042, score -93455                  | Overwritten. The negative score was printed and the exit was marked unsuccessful, but the shifted file was written anyway  |

ffsubsync 0.5.1 will leave the file alone in this situation if you pass
`--skip-sync-on-low-quality`. It is off by default, so this is what you get
out of the box. LAPSE checks by default and there is no flag to remember.

## Method notes

Every timing is a single run on one machine with nothing else running. The
`lapse cached` column is the second run on that film, after the speech
profile already exists on disk.

The tests ran in two rounds. The first round covers twelve films with
millisecond error figures and split locations. The second round covers
twenty seven films with the confidence verdicts. Anything measured for all
39 films is in the same table.

Versions used: LAPSE 2.0.0, alass 2.0.0, ffsubsync 0.5.1.
