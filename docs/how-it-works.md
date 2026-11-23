# How it works

1. **Listening.** The audio is decoded in pieces, several at once, and run through [Silero VAD](https://github.com/snakers4/silero-vad), a small speech detection model, through onnxruntime. If onnxruntime is missing, LAPSE uses libfvad instead. On a 5.1 track it tries the centre channel, the front pair and a plain downmix, and keeps whichever sounds most like speech.
2. **Lining up.** The subtitle and the speech are turned into two signals each, one for where talking is and one for where it starts, and cross correlated with FFTW. A film that talks a lot in one half and very little in the other would pull the answer towards the busy half, so slow changes in how much talking there is are taken out of both signals first.
3. **Deciding what is wrong.** The subtitle is also checked in eight slices. When they all agree, it is a shift. When they lie on a line, it drifts. When they land in groups, it was cut, and a dynamic programming search finds where the cuts are. Every split has to pay a price that grows with the size of the file, so a file is only cut up when that is clearly better.
4. **Judging the answer.** The answer gets a sigma, which says how far it stands out from the noise. It is only written over your file when it stands out clearly and enough of the slices back it up.
5. **Remembering.** The speech found in a video is cached. The next subtitle for the same video starts from it and takes about a second.

It can sync against the sound of the video, against a subtitle track inside the video, or against another subtitle file that is already right. Sound effects in brackets, music notes, and the credit lines subtitle sites add, such as `Synced and corrected by`, are left out of the measuring. They still move with everything around them.
