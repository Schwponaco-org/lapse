// LAPSE - Language-Agnostic subtitle synchronization engine
// Copyright (C) 2026 Rasmus Stisen Jensen (rs-jensen)
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "log.h"
#include <iostream>
#include <streambuf>
#include <mutex>
#ifdef _WIN32
    #include <io.h>
    #define isatty _isatty
    #define fileno _fileno
#else
    #include <unistd.h>
#endif

bool quiet = false;

namespace {
struct Sink : std::streambuf {
    int overflow(int c) override { return c; }
};
Sink sink;
std::ostream nowhere(&sink);

std::mutex one_at_a_time;
bool half_a_line = false;
int last_tenth = -1;

bool on_a_terminal() {
    static bool yes = isatty(fileno(stderr)) != 0;
    return yes;
}
}

std::ostream& say() {
    if (quiet) return nowhere;
    if (half_a_line) {
        std::cerr << '\n';
        half_a_line = false;
    }
    return std::cerr;
}

void progress(const char* what, int done, int total) {
    if (quiet || total <= 0) return;
    int percent = (int)((long long)done * 100 / total);

    std::lock_guard<std::mutex> hold(one_at_a_time);
    if (on_a_terminal()) {
        std::cerr << '\r' << what << " " << percent << "%   " << std::flush;
        half_a_line = true;
    } else if (percent / 10 != last_tenth) {
        last_tenth = percent / 10;
        std::cerr << what << " " << percent << "%\n";
    }
}

void progress_done() {
    std::lock_guard<std::mutex> hold(one_at_a_time);
    last_tenth = -1;
    if (quiet || !half_a_line) return;
    std::cerr << '\n';
    half_a_line = false;
}
