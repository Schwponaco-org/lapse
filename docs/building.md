# Building from source

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
