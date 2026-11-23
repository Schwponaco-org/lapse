# Subtitle formats

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
