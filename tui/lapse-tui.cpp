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

// A terminal interface on top of the engine. It walks a folder, pairs every
// subtitle with its video the way the container does, and syncs them one at
// a time with the engine running inside this program, so the speech model is
// loaded once however many files go through.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <io.h>
    #define isatty _isatty
    #define fileno _fileno
#else
    #include <unistd.h>
#endif

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>

extern "C" {
#include <libavutil/log.h>
}

#include "cli.h"
#include "log.h"
#include "silero.h"
#include "srt_parser.h"

namespace fs = std::filesystem;
using namespace ftxui;
using Clock = std::chrono::steady_clock;

namespace {

// The same lists the container works from, see docker/main.py
const std::set<std::string> VIDEO_EXTS = {
    ".mp4", ".mkv", ".avi", ".mov", ".m4v", ".ts", ".m2ts", ".mts", ".webm",
    ".wmv", ".mpg", ".mpeg", ".flv", ".ogv", ".ogm", ".divx", ".vob", ".rmvb", ".3gp",
};

// Everything we know of as a subtitle. Which of them the engine reads is up
// to SUBTITLE_FORMATS, the rest are counted and left alone
const std::set<std::string> SUBTITLE_EXTS = {
    ".srt", ".ass", ".ssa", ".vtt", ".sub", ".idx", ".sup", ".smi", ".sami",
    ".ttml", ".dfxp", ".sbv", ".mpl2", ".lrc", ".stl", ".scc", ".mcc", ".cap",
};

const std::set<std::string> SKIP_DIRS = {
    "@eaDir", "#recycle", "#snapshot", "lost+found", ".Trash", "@Recycle",
};

const std::set<std::string> LANGUAGE_WORDS = {
    "en", "eng", "english", "da", "dan", "dansk", "danish", "sv", "swe", "swedish",
    "no", "nor", "nb", "norwegian", "fi", "fin", "finnish", "is", "isl", "icelandic",
    "de", "ger", "deu", "german", "fr", "fre", "fra", "french", "es", "spa", "spanish",
    "it", "ita", "italian", "nl", "dut", "nld", "dutch", "pt", "por", "portuguese",
    "pl", "pol", "polish", "ru", "rus", "russian", "cs", "ces", "czech", "tr", "tur",
    "turkish", "ar", "ara", "arabic", "zh", "chi", "chinese", "ja", "jpn", "japanese",
    "ko", "kor", "korean", "hu", "hun", "hungarian", "ro", "ron", "romanian", "el",
    "ell", "greek", "he", "heb", "hebrew", "hi", "hin", "hindi", "th", "tha", "thai",
    "forced", "sdh", "cc", "hearing", "impaired", "full", "default", "sub", "subs",
    "subtitle", "subtitles", "srt", "track",
};

const std::vector<std::string> MODES = {"auto", "ols", "nosplit", "split"};

const std::vector<std::string> SPINNER = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool ends_with(const std::string& s, const std::string& end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

// The engine takes paths as UTF-8 on Windows and as the bytes on disk
// everywhere else
std::string native(const fs::path& path) {
#ifdef _WIN32
    auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
#else
    return path.string();
#endif
}

// Names come off the disk as bytes and are not always UTF-8, and a name with
// an escape in it should not get to move the cursor about
std::string shown(const std::string& raw) {
    std::string out;
    size_t i = 0;
    while (i < raw.size()) {
        unsigned char c = (unsigned char)raw[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool good = len > 0 && i + len <= raw.size();
        uint32_t code = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; good && k < len; k++) {
            unsigned char next = (unsigned char)raw[i + k];
            good = (next >> 6) == 0x2;
            code = (code << 6) | (next & 0x3F);
        }
        if (good && len > 1) {
            static const uint32_t least[] = {0, 0, 0x80, 0x800, 0x10000};
            good = code >= least[len] && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF);
        }
        if (!good) {
            out += "\xEF\xBF\xBD";
            i++;
            continue;
        }
        if (code < 0x20 || code == 0x7F || (code >= 0x80 && code < 0xA0))
            out += code == '\t' ? ' ' : '?';
        else
            out.append(raw, i, len);
        i += len;
    }
    return out;
}

int cells(const std::string& s) {
    return string_width(s);
}

// each glyph with the cells it takes, a wide one keeps both of its cells
std::vector<std::pair<std::string, int>> glyphs(const std::string& s) {
    std::vector<std::pair<std::string, int>> out;
    for (auto& g : Utf8ToGlyphs(s)) {
        if (g.empty()) {
            if (!out.empty()) out.back().second = 2;
            continue;
        }
        out.push_back({g, 1});
    }
    return out;
}

std::string first_cells(const std::vector<std::pair<std::string, int>>& g, int w) {
    std::string out;
    int used = 0;
    for (auto& [text, width] : g) {
        if (used + width > w) break;
        out += text;
        used += width;
    }
    return out;
}

std::string last_cells(const std::vector<std::pair<std::string, int>>& g, int w) {
    std::string out;
    int used = 0;
    for (auto it = g.rbegin(); it != g.rend(); ++it) {
        if (used + it->second > w) break;
        out = it->first + out;
        used += it->second;
    }
    return out;
}

// cut with an ellipsis at the end
std::string cut(const std::string& s, int w) {
    if (w <= 0) return "";
    if (cells(s) <= w) return s;
    return first_cells(glyphs(s), w - 1) + "…";
}

// cut from the front, for folders where the deepest part says the most
std::string cut_front(const std::string& s, int w) {
    if (w <= 0) return "";
    if (cells(s) <= w) return s;
    return "…" + last_cells(glyphs(s), w - 1);
}

// cut in the middle, a file name says the most at both ends
std::string squeeze(const std::string& s, int w) {
    if (w <= 0) return "";
    if (cells(s) <= w) return s;
    auto g = glyphs(s);
    int tail = (w - 1) / 2;
    std::string end = last_cells(g, tail);
    return first_cells(g, w - 1 - cells(end)) + "…" + end;
}

std::string pad(const std::string& s, int w) {
    std::string out = cut(s, w);
    return out + std::string(std::max(0, w - cells(out)), ' ');
}

std::string pad_left(const std::string& s, int w) {
    std::string out = cut(s, w);
    return std::string(std::max(0, w - cells(out)), ' ') + out;
}

std::vector<std::string> wrap(const std::string& s, int w) {
    std::vector<std::string> lines;
    if (w < 1) return lines;
    auto g = glyphs(s);
    std::string line;
    int used = 0;
    bool gap = false;
    size_t i = 0;
    while (i < g.size()) {
        if (g[i].first == " ") {
            gap = used > 0;
            i++;
            continue;
        }
        size_t j = i;
        int word = 0;
        while (j < g.size() && g[j].first != " ") word += g[j++].second;
        if (used > 0 && used + (gap ? 1 : 0) + word > w) {
            lines.push_back(line);
            line.clear();
            used = 0;
            gap = false;
        }
        if (gap) {
            line += ' ';
            used++;
            gap = false;
        }
        // a word longer than the line is cut where the line ends
        for (size_t k = i; k < j; k++) {
            if (used + g[k].second > w) {
                lines.push_back(line);
                line.clear();
                used = 0;
            }
            line += g[k].first;
            used += g[k].second;
        }
        i = j;
    }
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
}

std::string plural(long long n, const std::string& one, const std::string& many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string offset_text(int ms) {
    char buf[32];
    if (std::abs(ms) < 100000) std::snprintf(buf, sizeof(buf), "%+d ms", ms);
    else std::snprintf(buf, sizeof(buf), "%+.1f s", ms / 1000.0);
    return ms == 0 ? "0 ms" : buf;
}

std::string number_text(double v, const char* how) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), how, v);
    return buf;
}


// Pairing, ported from docker/main.py so both pick the same video

std::set<std::string> normalize(std::string name) {
    for (char& c : name)
        if (c && std::strchr("._-[]()+'", c)) c = ' ';
    std::set<std::string> words;
    std::istringstream in(lower(name));
    std::string word;
    while (in >> word) words.insert(word);
    return words;
}

bool ours(const std::string& base) {
    return ends_with(base, ".lapse-unsure") || ends_with(base, ".lapse");
}

bool digit(const std::string& s, size_t i) {
    return i < s.size() && std::isdigit((unsigned char)s[i]);
}

int number_at(const std::string& s, size_t from, size_t count) {
    return std::stoi(s.substr(from, count));
}

// S01E02, s1.e2 and 1x02, the way main.py reads them
std::optional<std::pair<int, int>> episode_tag(const std::string& name) {
    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] != 's' && name[i] != 'S') continue;
        for (size_t season = 2; season >= 1; season--) {
            size_t at = i + 1;
            bool all = true;
            for (size_t k = 0; k < season; k++) all = all && digit(name, at + k);
            if (!all) continue;
            size_t after = at + season;
            for (int gap = 1; gap >= 0; gap--) {
                size_t e = after;
                if (gap) {
                    if (e >= name.size() || !std::strchr(" ._-", name[e])) continue;
                    e++;
                }
                if (e >= name.size() || (name[e] != 'e' && name[e] != 'E') || !digit(name, e + 1)) continue;
                size_t count = 1;
                while (count < 3 && digit(name, e + 1 + count)) count++;
                return std::make_pair(number_at(name, at, season), number_at(name, e + 1, count));
            }
        }
    }
    for (size_t i = 0; i < name.size(); i++) {
        if (!digit(name, i) || (i > 0 && digit(name, i - 1))) continue;
        for (size_t season = 2; season >= 1; season--) {
            if (!digit(name, i + season - 1)) continue;
            size_t x = i + season;
            if (season == 1 && digit(name, x)) continue;
            if (x >= name.size() || name[x] != 'x') continue;
            if (!digit(name, x + 1) || !digit(name, x + 2) || digit(name, x + 3)) continue;
            return std::make_pair(number_at(name, i, season), number_at(name, x + 1, 2));
        }
    }
    return std::nullopt;
}

struct Named {
    std::string base;
    fs::path path;
};

const Named* match_video(const std::string& sbase, const std::vector<Named>& videos) {
    std::set<std::string> tokens = normalize(sbase);
    for (auto& word : LANGUAGE_WORDS) tokens.erase(word);
    auto stag = episode_tag(sbase);
    const Named* best = nullptr;
    double best_score = 0;
    std::vector<const Named*> usable;

    for (auto& video : videos) {
        auto vtag = episode_tag(video.base);
        // Two different episodes in one folder share nearly every other word,
        // so a tag that disagrees settles it before anything else is looked at
        if (stag && vtag && *stag != *vtag) continue;
        usable.push_back(&video);

        auto vtokens = normalize(video.base);
        if (vtokens.empty()) continue;
        int shared = 0;
        for (auto& word : vtokens) shared += tokens.count(word) ? 1 : 0;
        double score = (double)shared / (double)vtokens.size();
        if (score > best_score) {
            best_score = score;
            best = &video;
        }
    }

    if (best && best_score >= 0.5) return best;
    // nothing lined up by name, but when only one video can be the one the
    // subtitle belongs to, it is that one
    if (usable.size() == 1) return usable[0];
    return nullptr;
}

std::vector<Named> videos_in(const fs::path& dir) {
    std::vector<Named> found;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = native(it->path().filename());
        if (name.empty() || name[0] == '.') continue;
        if (VIDEO_EXTS.count(lower(native(it->path().extension()))))
            found.push_back({native(it->path().stem()), it->path()});
    }
    std::sort(found.begin(), found.end(), [](const Named& a, const Named& b) { return a.path < b.path; });
    return found;
}

struct Found {
    fs::path video, subtitle;
    bool backup = false;
};

struct Walk {
    std::vector<Found> pairs;
    int unreadable = 0;
    int unmatched = 0;
    std::string error;
};

Walk walk(const fs::path& root, const std::set<std::string>& readable) {
    Walk result;
    std::map<std::string, std::vector<Named>> videos, subtitles;
    std::map<std::string, fs::path> folders;

    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) {
        result.error = ec.message();
        return result;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const fs::path& path = it->path();
        std::string name = native(path.filename());
        std::error_code kind;
        if (it->is_directory(kind)) {
            if (name.empty() || name[0] == '.' || SKIP_DIRS.count(name)) it.disable_recursion_pending();
            continue;
        }
        if (name.empty() || name[0] == '.') continue;
        std::string ext = lower(native(path.extension()));
        std::string base = native(path.stem());
        std::string folder = native(path.parent_path());
        if (VIDEO_EXTS.count(ext)) {
            videos[folder].push_back({base, path});
        } else if ((SUBTITLE_EXTS.count(ext) || readable.count(ext)) && !ours(base)) {
            subtitles[folder].push_back({base, path});
            folders[folder] = path.parent_path();
        }
    }

    for (auto& [folder, subs] : subtitles) {
        // a .sub next to an .idx of the same name is the picture half of a
        // VobSub pair, the .idx is the one to sync
        std::set<std::string> vobsub;
        for (auto& sub : subs)
            if (lower(native(sub.path.extension())) == ".idx") vobsub.insert(sub.base);
        subs.erase(std::remove_if(subs.begin(), subs.end(), [&](const Named& sub) {
            return lower(native(sub.path.extension())) == ".sub" && vobsub.count(sub.base);
        }), subs.end());
        std::sort(subs.begin(), subs.end(), [](const Named& a, const Named& b) { return a.path < b.path; });

        std::vector<Named> candidates = videos[folder];
        std::sort(candidates.begin(), candidates.end(), [](const Named& a, const Named& b) { return a.path < b.path; });
        // scene releases put the subtitles in a Subs folder beside the video
        if (candidates.empty()) candidates = videos_in(folders[folder].parent_path());

        for (auto& sub : subs) {
            const Named* video = candidates.empty() ? nullptr : match_video(sub.base, candidates);
            if (!video) {
                result.unmatched++;
                continue;
            }
            if (!readable.count(lower(native(sub.path.extension())))) {
                result.unreadable++;
                continue;
            }
            Found pair{video->path, sub.path};
            std::error_code missing;
            pair.backup = fs::exists(native(sub.path) + ".bak", missing);
            result.pairs.push_back(pair);
        }
    }
    return result;
}


// The engine's --json line is one flat object, so this reads that and no more
struct Json {
    std::map<std::string, std::string> text;
    std::map<std::string, double> number;
    std::map<std::string, bool> flag;
    std::map<std::string, std::vector<double>> list;
};

void put_utf8(std::string& out, uint32_t code) {
    if (code < 0x80) {
        out += (char)code;
    } else if (code < 0x800) {
        out += (char)(0xC0 | (code >> 6));
        out += (char)(0x80 | (code & 0x3F));
    } else {
        out += (char)(0xE0 | (code >> 12));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    }
}

bool read_json(const std::string& all, Json& out) {
    size_t last = all.find_last_not_of(" \t\r\n");
    if (last == std::string::npos) return false;
    size_t first = all.rfind('\n', last);
    first = first == std::string::npos ? 0 : first + 1;
    const std::string s = all.substr(first, last - first + 1);
    size_t i = 0;

    auto skip = [&] {
        while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    };
    auto quoted = [&](std::string& got) {
        if (i >= s.size() || s[i] != '"') return false;
        i++;
        while (i < s.size() && s[i] != '"') {
            if (s[i] != '\\' || i + 1 >= s.size()) {
                got += s[i++];
                continue;
            }
            char e = s[i + 1];
            i += 2;
            if (e == 'n') got += '\n';
            else if (e == 't') got += '\t';
            else if (e == 'u' && i + 4 <= s.size()) {
                put_utf8(got, (uint32_t)std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
                i += 4;
            } else got += e;
        }
        if (i >= s.size()) return false;
        i++;
        return true;
    };
    auto number = [&](double& got) {
        const char* from = s.c_str() + i;
        char* stop = nullptr;
        got = std::strtod(from, &stop);
        if (stop == from) return false;
        i += (size_t)(stop - from);
        return true;
    };

    skip();
    if (i >= s.size() || s[i] != '{') return false;
    i++;
    for (;;) {
        skip();
        if (i < s.size() && s[i] == '}') return true;
        std::string key;
        if (!quoted(key)) return false;
        skip();
        if (i >= s.size() || s[i] != ':') return false;
        i++;
        skip();
        if (i >= s.size()) return false;
        if (s[i] == '"') {
            std::string value;
            if (!quoted(value)) return false;
            out.text[key] = value;
        } else if (s.compare(i, 4, "true") == 0) {
            out.flag[key] = true;
            i += 4;
        } else if (s.compare(i, 5, "false") == 0) {
            out.flag[key] = false;
            i += 5;
        } else if (s.compare(i, 4, "null") == 0) {
            i += 4;
        } else if (s[i] == '[') {
            i++;
            std::vector<double> values;
            for (;;) {
                skip();
                if (i < s.size() && s[i] == ']') {
                    i++;
                    break;
                }
                double v;
                if (!number(v)) return false;
                values.push_back(v);
                skip();
                if (i < s.size() && s[i] == ',') i++;
            }
            out.list[key] = values;
        } else {
            double v;
            if (!number(v)) return false;
            out.number[key] = v;
        }
        skip();
        if (i < s.size() && s[i] == ',') {
            i++;
            continue;
        }
        return i < s.size() && s[i] == '}';
    }
}


// Catches what the engine says on std::cerr while it works for us, a line at
// a time. Progress comes as "Listening to the audio 50%" ending in \r on a
// terminal, so \r ends a line as well as \n
class Capture : public std::streambuf {
  public:
    using Heard = std::function<void(const std::string& line, bool whole)>;

    explicit Capture(Heard heard) : heard_(std::move(heard)) {}

    void finish() {
        std::lock_guard<std::mutex> hold(lock_);
        end_line();
    }

  protected:
    int overflow(int c) override {
        if (c == traits_type::eof()) return traits_type::not_eof(c);
        char ch = (char)c;
        take(&ch, 1);
        return c;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        take(s, n);
        return n;
    }

  private:
    void take(const char* s, std::streamsize n) {
        std::lock_guard<std::mutex> hold(lock_);
        for (std::streamsize i = 0; i < n; i++) {
            if (s[i] == '\n' || s[i] == '\r') end_line();
            else pending_ += s[i];
        }
        // a progress line is not finished until the next one comes along, so
        // the gauge gets to see it before then
        if (!pending_.empty()) heard_(pending_, false);
    }

    void end_line() {
        if (pending_.empty()) return;
        heard_(pending_, true);
        pending_.clear();
    }

    Heard heard_;
    std::mutex lock_;
    std::string pending_;
};

std::atomic<Capture*> listening{nullptr};

// FFmpeg writes its warnings straight to stderr, which is the screen here,
// so they go to the log of the file they are about
void ffmpeg_said(void* avcl, int level, const char* fmt, va_list args) {
    if (level > av_log_get_level()) return;
    Capture* to = listening.load();
    if (!to) return;
    thread_local int prefix = 1;
    char line[1024];
    av_log_format_line2(avcl, level, fmt, args, line, sizeof(line), &prefix);
    to->sputn(line, (std::streamsize)std::strlen(line));
}

bool progress_line(const std::string& line, std::string& step, int& percent) {
    size_t space = line.rfind(' ');
    if (space == std::string::npos || space == 0) return false;
    std::string tail = line.substr(space + 1);
    if (tail.size() < 2 || tail.size() > 4 || tail.back() != '%') return false;
    for (size_t i = 0; i + 1 < tail.size(); i++)
        if (!std::isdigit((unsigned char)tail[i])) return false;
    if (!std::isalpha((unsigned char)line[0])) return false;
    percent = std::min(100, std::atoi(tail.c_str()));
    step = line.substr(0, space);
    return true;
}


enum class Tone { Plain, Dim, Head, Good, Warn, Bad, Accent };

// lit is for text on the highlighted row, which sits on blue
Decorator tint(Tone tone, bool lit = false) {
    switch (tone) {
        case Tone::Good:   return color(lit ? Color::GreenLight : Color::Green);
        case Tone::Warn:   return color(lit ? Color::YellowLight : Color::Yellow);
        case Tone::Bad:    return color(lit ? Color::RedLight : Color::Red);
        case Tone::Accent: return color(lit ? Color::CyanLight : Color::Cyan);
        case Tone::Dim:    return lit ? color(Color::GrayLight) : Decorator(dim);
        case Tone::Head:   return lit ? color(Color::White) | Decorator(bold) : Decorator(bold);
        default:           return lit ? color(Color::White) : Decorator(nothing);
    }
}

struct Line {
    std::string text;
    Tone tone = Tone::Plain;
};

// what the last run on a file came back with
struct Outcome {
    std::string status;   // solid, unsure, nothing, few, same, error, undone, shifted
    bool dry = false;
    bool numbers = false;
    std::string mode, verdict, output, why;
    int offset = 0;
    double ratio = 1.0;
    double sigma = 0;
    int parts = 1;
    std::vector<int> offsets;
    bool written = false;
    bool unchanged = false;
};

struct Pair {
    std::string key;
    std::string video_path, subtitle_path;
    std::string name, video_name, folder, search;
    bool backup = false;
    bool selected = false;
    enum { Idle, Queued, Running } state = Idle;
    Outcome last;
    std::string step;
    int percent = -1;
    std::string said;
    std::deque<Line> log;
};
using PairRef = std::shared_ptr<Pair>;

struct Options {
    std::string mode = "auto";
    bool dry = false;
    bool force = false;
    bool no_backup = false;
};

enum class Kind { Sync, Undo, Shift };

struct Job {
    Kind kind = Kind::Sync;
    PairRef pair;
    Options options;
    std::string shift;
};

const size_t LOG_LINES = 400;

void note(Pair& pair, const std::string& text, Tone tone) {
    pair.log.push_back({text, tone});
    while (pair.log.size() > LOG_LINES) pair.log.pop_front();
}

std::pair<std::string, Tone> label_of(const Outcome& o) {
    std::string label;
    Tone tone = Tone::Plain;
    if (o.status == "solid") label = "solid", tone = Tone::Good;
    else if (o.status == "unsure") label = "unsure", tone = Tone::Warn;
    else if (o.status == "nothing") label = "nothing", tone = Tone::Bad;
    else if (o.status == "few") label = "too few", tone = Tone::Bad;
    else if (o.status == "error") return {"error", Tone::Bad};
    else if (o.status == "same") label = "in sync", tone = Tone::Dim;
    else if (o.status == "undone") label = "undone", tone = Tone::Accent;
    else if (o.status == "shifted") label = "shifted", tone = Tone::Accent;
    if (o.dry && !label.empty()) return {"(" + label + ")", Tone::Dim};
    return {label, tone};
}

std::string file_name(const std::string& path) {
    return shown(native(fs::path(path).filename()));
}

// one sentence on what happened to the file
std::string what_happened(const Outcome& o, const Pair& p) {
    if (o.status == "error") return "It did not finish: " + (o.why.empty() ? "the engine gave no answer" : o.why);
    if (o.status == "few") return "Too few lines to go on, nothing was written";
    if (o.status == "undone") return "Put back from the .bak";
    if (o.status == "shifted") {
        if (o.dry) return "Dry run, every line would move by " + offset_text(o.offset);
        return "Every line moved by " + offset_text(o.offset);
    }
    if (o.status == "same") return "Already in sync, left as it was";
    if (o.dry) {
        if (o.written && o.output != p.subtitle_path)
            return "Dry run, nothing was written. The guess would have gone to " + file_name(o.output);
        return "Dry run, nothing was written";
    }
    if (o.written && o.output == p.subtitle_path)
        return p.backup ? "Written over the original, the old one is the .bak" : "Written over the original";
    if (o.written) return "The original is left alone, the guess is in " + file_name(o.output);
    return "Nothing was written" + (o.why.empty() ? std::string() : ", " + o.why);
}

std::string short_result(const Outcome& o) {
    Outcome real = o;
    real.dry = false;
    std::string out = label_of(real).first;
    if (o.numbers && o.status != "error" && o.status != "few") out += ", " + offset_text(o.offset);
    if (o.numbers && o.status != "shifted" && o.status != "error" && o.sigma > 0)
        out += ", sigma " + number_text(o.sigma, "%.1f");
    if (o.dry && o.status != "error") out += " (dry run)";
    return out;
}


class Tui {
  public:
    Tui(fs::path root, std::set<std::string> readable, ScreenInteractive& screen)
        : root_(std::move(root)), readable_(std::move(readable)), screen_(screen) {}

    Component component() {
        auto view = Renderer([this] { return render(); });
        return CatchEvent(view, [this](Event event) { return on_event(event); });
    }

    void start() {
        worker_ = std::thread([this] { work(); });
        ticker_ = std::thread([this] { tick(); });
        std::lock_guard<std::mutex> hold(lock_);
        rescan();
    }

    // after the screen has gone, returns what main should
    int finish() {
        ticking_ = false;
        ticker_.join();

        PairRef left_running;
        bool busy;
        {
            std::lock_guard<std::mutex> hold(lock_);
            for (auto& job : queue_) job.pair->state = Pair::Idle;
            queue_.clear();
            left_running = running_;
            busy = running_ || scanning_;
            working_ = false;
        }
        wake_.notify_all();
        std::string summary = summarise();

        if (busy) {
            // run() cannot be stopped half way, so the process goes with it.
            // Subtitles are written to a .tmp and renamed, so all that can be
            // left is the .tmp
            if (left_running) {
                std::error_code ec;
                fs::remove(left_running->subtitle_path + ".tmp", ec);
            }
            if (!summary.empty()) std::cout << summary << '\n';
            std::cout.flush();
            std::_Exit(0);
        }

        worker_.join();
        if (scanner_.joinable()) scanner_.join();
        silero_close();
        if (!summary.empty()) std::cout << summary << '\n';
        return 0;
    }

  private:
    // Everything from here to the next comment is shared with the worker and
    // the scanner, and only touched with lock_ held
    std::mutex lock_;
    std::condition_variable wake_;
    std::vector<PairRef> pairs_;
    std::deque<Job> queue_;
    PairRef running_;
    Kind running_kind_ = Kind::Sync;
    Clock::time_point running_since_;
    bool working_ = true;
    bool scanning_ = false;
    bool scanned_ = false;
    std::optional<Walk> walked_;
    std::string flash_;
    Tone flash_tone_ = Tone::Plain;
    Clock::time_point flash_at_;
    int unreadable_ = 0;
    int unmatched_ = 0;
    std::string walk_error_;

    // the main thread's own
    std::vector<size_t> shown_;
    int cursor_ = 0;
    int top_ = 0;
    int page_ = 10;
    std::string filter_;
    enum class Typing { None, Filter, Shift } typing_ = Typing::None;
    std::string typed_;
    PairRef shift_for_;
    bool help_ = false;
    struct Ask {
        std::string title;
        std::vector<std::string> lines;
        std::function<void()> yes;
    };
    std::optional<Ask> ask_;
    Options options_;

    const fs::path root_;
    const std::set<std::string> readable_;
    ScreenInteractive& screen_;
    std::atomic<bool> dirty_{true};
    std::atomic<bool> ticking_{true};
    std::thread worker_, ticker_, scanner_;


    // Redraws are asked for from here rather than from the threads doing the
    // work, a few times a second at most and only while something changes
    void tick() {
        while (ticking_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            bool busy;
            {
                std::lock_guard<std::mutex> hold(lock_);
                busy = running_ || scanning_;
            }
            if (ticking_ && (dirty_.exchange(false) || busy)) screen_.PostEvent(Event::Custom);
        }
    }

    void tell(const std::string& text, Tone tone = Tone::Plain) {
        flash_ = text;
        flash_tone_ = tone;
        flash_at_ = Clock::now();
    }

    void rescan() {
        if (scanning_) {
            tell("Still looking through the folder");
            return;
        }
        if (scanner_.joinable()) scanner_.join();
        scanning_ = true;
        scanner_ = std::thread([this] {
            Walk found = walk(root_, readable_);
            std::lock_guard<std::mutex> hold(lock_);
            walked_ = std::move(found);
            scanning_ = false;
            dirty_ = true;
        });
    }

    PairRef make_pair(const Found& found) {
        auto p = std::make_shared<Pair>();
        p->video_path = native(found.video);
        p->subtitle_path = native(found.subtitle);
        p->key = p->video_path + '\n' + p->subtitle_path;
        p->name = shown(native(found.subtitle.filename()));
        p->video_name = shown(native(found.video.filename()));
        fs::path rel = found.subtitle.parent_path().lexically_relative(root_);
        p->folder = rel.empty() || rel == "." ? "" : shown(native(rel));
        p->search = lower(p->folder + "/" + p->name + " " + p->video_name);
        return p;
    }

    // a rescan keeps what this session already knows about each pair
    void take_walk() {
        Walk found = std::move(*walked_);
        walked_.reset();
        std::map<std::string, PairRef> known;
        for (auto& p : pairs_) known[p->key] = p;
        PairRef here = current();

        pairs_.clear();
        for (auto& f : found.pairs) {
            PairRef p = make_pair(f);
            auto it = known.find(p->key);
            if (it != known.end()) p = it->second;
            p->backup = f.backup;
            pairs_.push_back(p);
        }
        unreadable_ = found.unreadable;
        unmatched_ = found.unmatched;
        walk_error_ = found.error;
        bool first = !scanned_;
        scanned_ = true;
        refilter();
        if (here) {
            for (size_t i = 0; i < shown_.size(); i++)
                if (pairs_[shown_[i]] == here) cursor_ = (int)i;
        }
        if (!walk_error_.empty()) tell("Could not look through the folder: " + walk_error_, Tone::Bad);
        else if (!first) tell("Looked again, " + plural((long long)pairs_.size(), "subtitle", "subtitles") + " with a video");
    }

    void refilter() {
        shown_.clear();
        std::string want = lower(filter_);
        for (size_t i = 0; i < pairs_.size(); i++)
            if (want.empty() || pairs_[i]->search.find(want) != std::string::npos) shown_.push_back(i);
        cursor_ = std::clamp(cursor_, 0, std::max(0, (int)shown_.size() - 1));
    }

    PairRef current() const {
        if (shown_.empty()) return nullptr;
        return pairs_[shown_[std::clamp(cursor_, 0, (int)shown_.size() - 1)]];
    }

    void move(int by) {
        if (shown_.empty()) return;
        cursor_ = std::clamp(cursor_ + by, 0, (int)shown_.size() - 1);
    }

    void enqueue(const PairRef& p, Kind kind, const std::string& shift = "") {
        Job job{kind, p, options_, shift};
        // undo and shift take no time, so they go ahead of the syncs
        if (kind == Kind::Sync) queue_.push_back(job);
        else queue_.push_front(job);
        p->state = Pair::Queued;
        wake_.notify_one();
    }

    static bool done(const Pair& p) {
        return !p.last.status.empty() && !p.last.dry && p.last.status != "error";
    }

    void sync_picked() {
        std::vector<PairRef> picked;
        for (auto& p : pairs_)
            if (p->selected) picked.push_back(p);
        if (picked.empty() && current()) picked.push_back(current());
        if (picked.empty()) {
            tell("Nothing here to sync");
            return;
        }
        int added = 0;
        for (auto& p : picked) {
            p->selected = false;
            if (p->state != Pair::Idle) continue;
            enqueue(p, Kind::Sync);
            added++;
        }
        if (added == 0) tell("Already waiting its turn");
        else if (added > 1) tell("Syncing " + std::to_string(added) + " subtitles, one after the other");
    }

    void sync_rest() {
        int added = 0;
        for (size_t i : shown_) {
            auto& p = pairs_[i];
            if (p->state != Pair::Idle || done(*p)) continue;
            enqueue(p, Kind::Sync);
            added++;
        }
        if (added == 0) tell("Everything shown has been done already");
        else tell("Syncing " + plural(added, "subtitle", "subtitles") + " that had not been done");
    }

    void clear_queue() {
        int dropped = (int)queue_.size();
        for (auto& job : queue_) job.pair->state = Pair::Idle;
        queue_.clear();
        if (dropped == 0) tell(running_ ? "Nothing is waiting, the one running carries on" : "Nothing is waiting");
        else tell("Dropped " + plural(dropped, "file", "files") + " from the queue" + (running_ ? ", the one running carries on" : ""));
    }

    void ask_undo() {
        PairRef p = current();
        if (!p) return;
        if (p->state != Pair::Idle) {
            tell("Wait for it to finish first");
            return;
        }
        if (!p->backup) {
            tell("There is no .bak beside " + p->name + " to put back", Tone::Warn);
            return;
        }
        ask_ = Ask{"Undo", {"Put " + p->name + ".bak back over " + p->name + "?",
                            "The synced version is thrown away."},
                   [this, p] { enqueue(p, Kind::Undo); }};
    }

    void ask_shift() {
        PairRef p = current();
        if (!p) return;
        if (p->state != Pair::Idle) {
            tell("Wait for it to finish first");
            return;
        }
        shift_for_ = p;
        typed_.clear();
        typing_ = Typing::Shift;
    }

    // a number of ms, with a sign and a fraction if wanted
    static bool ms_number(const std::string& s) {
        size_t i = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
        int digits = 0, dots = 0;
        for (; i < s.size(); i++) {
            if (std::isdigit((unsigned char)s[i])) digits++;
            else if (s[i] == '.' && dots++ == 0) continue;
            else return false;
        }
        return digits > 0;
    }

    void take_shift() {
        if (!ms_number(typed_)) {
            tell("That is not a number of ms", Tone::Warn);
            return;
        }
        typing_ = Typing::None;
        if (shift_for_ && shift_for_->state == Pair::Idle) enqueue(shift_for_, Kind::Shift, typed_);
        shift_for_.reset();
    }

    void leave() {
        ticking_ = false;
        screen_.Exit();
    }

    void quit() {
        if (!running_ && queue_.empty()) {
            leave();
            return;
        }
        std::vector<std::string> lines;
        if (running_) {
            std::string doing = running_kind_ == Kind::Sync ? "synced" : "written";
            lines.push_back(running_->name + " is still being " + doing +
                            (queue_.empty() ? "." : ", and " + plural((long long)queue_.size(), "more is", "more are") + " waiting."));
            lines.push_back("Quit anyway? It is left as it was.");
        } else {
            lines.push_back(plural((long long)queue_.size(), "file is", "files are") + " still waiting.");
            lines.push_back("Quit anyway?");
        }
        ask_ = Ask{"Quit", lines, [this] { leave(); }};
    }

    static bool is(const Event& e, const char* c) {
        return e.is_character() && e.character() == c;
    }

    bool moved(const Event& e) {
        if (e == Event::ArrowUp) move(-1);
        else if (e == Event::ArrowDown) move(1);
        else if (e == Event::PageUp) move(-std::max(1, page_ - 1));
        else if (e == Event::PageDown) move(std::max(1, page_ - 1));
        else if (e == Event::Home) move(-(int)shown_.size());
        else if (e == Event::End) move((int)shown_.size());
        else return false;
        return true;
    }

    bool typing(const Event& e) {
        if (e == Event::Escape) {
            if (typing_ == Typing::Filter) {
                filter_.clear();
                refilter();
            }
            typing_ = Typing::None;
            shift_for_.reset();
            return true;
        }
        if (e == Event::Return) {
            if (typing_ == Typing::Shift) take_shift();
            else typing_ = Typing::None;
            return true;
        }
        if (e == Event::Backspace) {
            auto g = glyphs(typed_);
            if (!g.empty()) g.pop_back();
            typed_.clear();
            for (auto& [text, width] : g) typed_ += text;
        } else if (e.is_character()) {
            std::string c = e.character();
            if (typing_ == Typing::Shift) {
                if (c.size() != 1 || !std::strchr("0123456789+-.", c[0]) || typed_.size() >= 12) return true;
            }
            typed_ += c;
        } else if (typing_ == Typing::Filter && moved(e)) {
            return true;
        } else {
            return true;
        }
        if (typing_ == Typing::Filter) {
            filter_ = typed_;
            refilter();
        }
        return true;
    }

    bool on_event(Event e) {
        std::lock_guard<std::mutex> hold(lock_);
        if (e == Event::Custom) {
            if (walked_) take_walk();
            return true;
        }
        if (e.is_mouse()) return false;
        if (help_) {
            help_ = false;
            return true;
        }
        if (ask_) {
            if (is(e, "y") || is(e, "Y")) {
                auto yes = ask_->yes;
                ask_.reset();
                yes();
            } else if (is(e, "n") || is(e, "N") || e == Event::Escape || is(e, "q")) {
                ask_.reset();
            }
            return true;
        }
        if (typing_ != Typing::None) return typing(e);

        if (e == Event::CtrlC || is(e, "q")) quit();
        else if (moved(e)) {}
        else if (is(e, " ")) {
            if (auto p = current()) p->selected = !p->selected;
            move(1);
        }
        else if (e == Event::Return) sync_picked();
        else if (is(e, "a")) sync_rest();
        else if (is(e, "x")) clear_queue();
        else if (is(e, "u")) ask_undo();
        else if (is(e, "s")) ask_shift();
        else if (is(e, "d")) {
            options_.dry = !options_.dry;
            tell(options_.dry ? "Dry run is on, nothing will be written" : "Dry run is off", options_.dry ? Tone::Warn : Tone::Plain);
        }
        else if (is(e, "f")) {
            options_.force = !options_.force;
            tell(options_.force ? "Force is on, the answer is written whatever the verdict" : "Force is off",
                 options_.force ? Tone::Warn : Tone::Plain);
        }
        else if (is(e, "b")) {
            options_.no_backup = !options_.no_backup;
            tell(options_.no_backup ? "No .bak is kept of what gets overwritten" : "A .bak is kept of what gets overwritten",
                 options_.no_backup ? Tone::Warn : Tone::Plain);
        }
        else if (is(e, "m")) {
            auto at = std::find(MODES.begin(), MODES.end(), options_.mode);
            options_.mode = (at == MODES.end() || at + 1 == MODES.end()) ? MODES[0] : *(at + 1);
            static const std::map<std::string, std::string> what = {
                {"auto", "works out what the file needs"},
                {"ols", "one offset and a stretch, for frame rate drift"},
                {"nosplit", "one offset for the whole file"},
                {"split", "lets the file be cut into parts with their own offsets"},
            };
            tell("Mode " + options_.mode + ", " + what.at(options_.mode));
        }
        else if (is(e, "r")) rescan();
        else if (is(e, "/")) {
            typing_ = Typing::Filter;
            typed_ = filter_;
        }
        else if (is(e, "?")) help_ = true;
        else if (e == Event::Escape && !filter_.empty()) {
            filter_.clear();
            refilter();
        }
        else return false;
        return true;
    }


    // The worker, which runs the engine one job at a time

    void work() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> hold(lock_);
                wake_.wait(hold, [this] { return !working_ || !queue_.empty(); });
                if (!working_) return;
                job = queue_.front();
                queue_.pop_front();
                running_ = job.pair;
                running_kind_ = job.kind;
                running_since_ = Clock::now();
                Pair& p = *job.pair;
                p.state = Pair::Running;
                p.step.clear();
                p.percent = -1;
                p.said.clear();
                note(p, heading(job), Tone::Head);
            }
            dirty_ = true;

            int code = 1;
            std::string answer = perform(job, code);

            {
                std::lock_guard<std::mutex> hold(lock_);
                settle(job, code, answer);
                job.pair->state = Pair::Idle;
                job.pair->step.clear();
                job.pair->percent = -1;
                running_.reset();
            }
            dirty_ = true;
        }
    }

    static std::string heading(const Job& job) {
        if (job.kind == Kind::Undo) return "Undo";
        std::string out = job.kind == Kind::Shift ? "Shift by " + job.shift + " ms" : "Sync, " + job.options.mode;
        if (job.options.dry) out += ", dry run";
        if (job.options.force && job.kind == Kind::Sync) out += ", force";
        if (job.options.no_backup) out += ", no backup";
        return out;
    }

    // runs the engine with its stderr and its answer caught, returns the answer
    std::string perform(const Job& job, int& code) {
        const Pair& p = *job.pair;
        std::vector<std::string> args = {"lapse"};
        if (job.kind == Kind::Undo) {
            args.insert(args.end(), {"--undo", p.subtitle_path});
        } else {
            if (job.kind == Kind::Shift) args.insert(args.end(), {"--shift", job.shift, p.subtitle_path});
            else args.insert(args.end(), {p.video_path, p.subtitle_path, job.options.mode});
            args.insert(args.end(), {"--json", "--progress"});
            if (job.options.dry) args.push_back("--dry-run");
            if (job.options.force && job.kind == Kind::Sync) args.push_back("--force");
            if (job.options.no_backup) args.push_back("--no-backup");
        }
        std::vector<const char*> argv;
        for (auto& a : args) argv.push_back(a.c_str());

        PairRef pair = job.pair;
        Capture capture([this, pair](const std::string& line, bool whole) { heard(pair, line, whole); });
        std::ostringstream answer;
        std::streambuf* cerr_was = std::cerr.rdbuf(&capture);
        std::streambuf* clog_was = std::clog.rdbuf(&capture);
        answer_to(&answer);
        listening = &capture;

        code = 1;
        try {
            code = run((int)argv.size(), argv.data());
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
        } catch (...) {
            std::cerr << "The engine stopped on something it did not expect\n";
        }

        capture.finish();
        listening = nullptr;
        answer_to(nullptr);
        std::cerr.rdbuf(cerr_was);
        std::clog.rdbuf(clog_was);
        return answer.str();
    }

    void heard(const PairRef& pair, const std::string& text, bool whole) {
        std::string line = text;
        while (!line.empty() && std::isspace((unsigned char)line.back())) line.pop_back();
        if (line.empty()) return;
        std::string step;
        int percent = 0;
        bool progress = progress_line(line, step, percent);
        if (!progress && !whole) return;

        std::lock_guard<std::mutex> hold(lock_);
        if (progress) {
            pair->step = step;
            pair->percent = percent;
        } else {
            pair->said = shown(line);
            note(*pair, pair->said, Tone::Plain);
        }
        dirty_ = true;
    }

    void settle(const Job& job, int code, const std::string& answer) {
        Pair& p = *job.pair;
        std::error_code ec;
        p.backup = fs::exists(p.subtitle_path + ".bak", ec);

        if (job.kind == Kind::Undo) {
            if (code == 0) {
                p.last = Outcome();
                p.last.status = "undone";
                note(p, "Put the original back from the .bak", Tone::Good);
                tell(p.name + " is back the way it was", Tone::Good);
            } else {
                note(p, "Could not put it back", Tone::Bad);
                tell("Could not put " + p.name + " back, the log says why", Tone::Bad);
            }
            return;
        }

        Json j;
        bool parsed = read_json(answer, j);
        Outcome o;
        o.dry = job.options.dry;
        if (parsed) {
            o.numbers = true;
            o.mode = j.text["mode"];
            o.verdict = j.text["verdict"];
            o.output = j.text["output"];
            o.why = j.text["why"];
            o.offset = (int)std::lround(j.number["offset_ms"]);
            o.ratio = j.number.count("ratio") ? j.number["ratio"] : 1.0;
            o.sigma = j.number["sigma"];
            o.parts = j.number.count("parts") ? (int)j.number["parts"] : 1;
            for (double v : j.list["offsets"]) o.offsets.push_back((int)std::lround(v));
            o.written = j.flag["written"];
            o.unchanged = j.flag["unchanged"];
        }

        if (!parsed || code == 1 || code < 0) o.status = "error";
        else if (job.kind == Kind::Shift) o.status = o.unchanged ? "same" : "shifted";
        else if (code == 2) o.status = "few";
        else if (o.unchanged) o.status = "same";
        else if (o.verdict == "solid" || o.verdict == "unsure" || o.verdict == "nothing") o.status = o.verdict;
        else o.status = "error";
        if (o.status == "error" && o.why.empty()) o.why = p.said;
        p.last = o;

        auto [label, tone] = label_of(o);
        if (o.dry && o.status != "error") tone = Tone::Plain;
        note(p, short_result(o), tone);
        note(p, what_happened(o, p), Tone::Plain);
        tell(p.name + ": " + short_result(o), tone);
    }


    // Drawing. Everything is cut to fit by hand, so a long name never pushes
    // a column out of line, and the list only builds the rows that show

    static Element pane(Element title, Element content, Color edge) {
        auto line = [&](const char* c) { return separatorCharacter(c) | color(edge); };
        return vbox({
            hbox({text("╭─") | color(edge), std::move(title), line("─") | flex, text("╮") | color(edge)}),
            hbox({line("│"), std::move(content) | flex, line("│")}) | flex,
            hbox({text("╰") | color(edge), line("─") | flex, text("╯") | color(edge)}),
        });
    }

    std::string spin(Clock::time_point now) const {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return SPINNER[(size_t)(ms / 100) % SPINNER.size()];
    }

    std::pair<std::string, Tone> status_of(const Pair& p, Clock::time_point now) const {
        if (p.state == Pair::Running) {
            std::string out = spin(now);
            if (p.percent >= 0) out += pad_left(std::to_string(p.percent) + "%", 5);
            return {out, Tone::Accent};
        }
        if (p.state == Pair::Queued) return {"queued", Tone::Dim};
        return label_of(p.last);
    }

    Element row(const Pair& p, bool lit, int w, Clock::time_point now) const {
        const int status_w = 9;
        const int offset_w = w >= 70 ? 10 : 0;
        int rest = w - 3 - 1 - status_w - offset_w;
        int name_w = rest, video_w = 0;
        if (rest >= 28) {
            name_w = rest * 11 / 20;
            video_w = rest - name_w - 1;
        }

        auto [label, tone] = status_of(p, now);
        std::string offset;
        if (offset_w && p.state == Pair::Idle && p.last.numbers && p.last.status != "error" && p.last.status != "few")
            offset = offset_text(p.last.offset);

        Elements parts = {
            text(p.selected ? "✓" : " ") | tint(Tone::Warn, lit) | bold,
            text(p.backup ? "• " : "  ") | tint(Tone::Accent, lit),
            text(pad(squeeze(p.name, name_w), name_w)) | (p.selected ? tint(Tone::Warn, lit) | Decorator(bold) : tint(lit ? Tone::Head : Tone::Plain, lit)),
        };
        if (video_w) parts.push_back(text(" " + pad(squeeze(p.video_name, video_w), video_w)) | tint(Tone::Dim, lit));
        parts.push_back(text(" " + pad(label, status_w)) | tint(tone, lit));
        if (offset_w) parts.push_back(text(pad_left(offset, offset_w)) | tint(p.last.dry ? Tone::Dim : Tone::Plain, lit));
        Element out = hbox(std::move(parts));
        if (lit) out = out | bgcolor(Color::Blue);
        return out;
    }

    Element list_pane(int w, int h, Clock::time_point now) const {
        int inner = w - 2;
        Elements rows;
        auto say = [&](const std::string& s, Tone tone = Tone::Dim) {
            for (auto& l : wrap(s, inner - 2)) rows.push_back(text(" " + l) | tint(tone));
        };
        if (!scanned_) {
            say("Looking through " + shown(native(root_)) + " " + spin(now));
        } else if (pairs_.empty()) {
            say("No subtitle here has a video to go with it.");
            if (unmatched_) say(plural(unmatched_, "subtitle has", "subtitles have") + " no video beside it.");
            if (unreadable_) say(plural(unreadable_, "subtitle is", "subtitles are") + " in a format the engine does not read.");
            say("Press r to look again, or q to quit.");
        } else if (shown_.empty()) {
            say("Nothing matches " + filter_ + ". Esc clears the filter.");
        } else {
            for (int i = top_; i < (int)shown_.size() && i < top_ + h; i++)
                rows.push_back(row(*pairs_[shown_[i]], i == cursor_, inner, now));
        }

        std::string title = " Subtitles ";
        std::string where;
        if (!filter_.empty()) title += "matching " + filter_ + " ";
        if (shown_.size() > (size_t)h) where = " " + std::to_string(cursor_ + 1) + " of " + std::to_string(shown_.size()) + " ";
        title = cut(title, inner - 2 - cells(where));
        Element head = hbox({text(title) | bold, text(where) | dim});
        return pane(head, vbox(std::move(rows)), Color::Cyan);
    }

    // a label in the details pane and its value, wrapped under itself
    static void field(Elements& out, const std::string& label, const std::string& value, int w,
                      Decorator style = nothing, size_t most = 3) {
        const int label_w = 8;
        auto lines = wrap(value, std::max(4, w - label_w));
        if (lines.size() > most) {
            lines.resize(most);
            lines.back() = cut(lines.back() + "…", std::max(4, w - label_w));
        }
        for (size_t i = 0; i < lines.size(); i++)
            out.push_back(hbox({text(" " + pad(i == 0 ? label : "", label_w)) | dim, text(lines[i]) | style}));
    }

    Element details_pane(int w, int h, Clock::time_point now) const {
        int inner = w - 2;
        int dw = inner - 2;
        Elements lines;
        PairRef p = current();
        auto say = [&](const std::string& s, Decorator style = nothing) {
            for (auto& l : wrap(s, dw)) lines.push_back(text(" " + l) | style);
        };

        if (!p) {
            say(scanned_ ? "Nothing to show." : "Looking for subtitles…", dim);
            if (scanned_ && (unmatched_ || unreadable_)) {
                lines.push_back(text(""));
                if (unmatched_) say(plural(unmatched_, "subtitle", "subtitles") + " with no video found for it.", dim);
                if (unreadable_) say(plural(unreadable_, "subtitle", "subtitles") + " in a format the engine does not read.", dim);
            }
            return pane(text(" Details ") | bold, vbox(std::move(lines)), Color::GrayDark);
        }

        auto name = wrap(p->name, dw);
        if (name.size() > 2) {
            name.resize(2);
            name[1] = cut_front(p->name, dw);
        }
        for (auto& l : name) lines.push_back(text(" " + l) | bold);
        lines.push_back(text(" " + cut_front(p->folder.empty() ? "in the folder itself" : p->folder, dw)) | dim);
        lines.push_back(text(""));
        field(lines, "Video", p->video_name, dw, nothing, 2);
        field(lines, "Backup", p->backup ? "a .bak, u puts it back" : "none", dw, nothing, 2);
        lines.push_back(text(""));

        if (p->state == Pair::Running) {
            auto took = std::chrono::duration_cast<std::chrono::seconds>(now - running_since_).count();
            std::string step = p->step.empty() ? "Starting" : p->step;
            say(cut(step, dw - 6) + "  " + std::to_string(took) + "s", nothing);
            float done = p->percent < 0 ? 0.f : (float)p->percent / 100.f;
            lines.push_back(hbox({
                text(" "),
                gauge(done) | color(Color::Cyan) | bgcolor(Color::GrayDark) | flex,
                text(pad_left(p->percent < 0 ? "" : std::to_string(p->percent) + "%", 5)),
                text(" "),
            }));
        } else if (p->state == Pair::Queued) {
            say("Waiting its turn.", dim);
        } else if (!p->last.status.empty()) {
            const Outcome& o = p->last;
            Outcome real = o;
            real.dry = false;
            auto [label, tone] = label_of(real);
            field(lines, "Result", label, dw, tint(tone) | Decorator(bold), 1);
            if (o.dry) lines.back() = hbox({lines.back(), text("  dry run") | dim});
            if (o.numbers && o.status != "error" && o.status != "few") {
                field(lines, "Offset", offset_text(o.offset), dw);
                if (std::fabs(o.ratio - 1.0) > 1e-6) field(lines, "Stretch", number_text(o.ratio, "%.5f"), dw);
                if (o.parts > 1) {
                    std::string each;
                    for (size_t i = 0; i < o.offsets.size(); i++) each += (i ? ", " : "") + offset_text(o.offsets[i]);
                    field(lines, "Parts", std::to_string(o.parts) + (each.empty() ? "" : ": " + each), dw, nothing, 2);
                }
                if (o.status != "shifted") {
                    field(lines, "Sigma", number_text(o.sigma, "%.1f"), dw);
                    field(lines, "Mode", o.mode, dw);
                }
            }
            say(what_happened(o, *p), nothing);
        } else {
            say("Not synced yet. Enter syncs it, Space selects it to sync with others.", dim);
        }

        // the log takes whatever height is left, newest at the bottom
        int room = h - (int)lines.size() - 2;
        if (room >= 1 && !p->log.empty()) {
            lines.push_back(text(""));
            lines.push_back(hbox({text(" Log ") | dim, separatorCharacter("─") | dim | flex, text(" ")}));
            // whole entries only, unless the newest one alone is too long
            std::vector<Element> tail;
            for (auto it = p->log.rbegin(); it != p->log.rend(); ++it) {
                auto wrapped = wrap(it->text, dw);
                if (!tail.empty() && (int)(tail.size() + wrapped.size()) > room) break;
                for (auto l = wrapped.rbegin(); l != wrapped.rend() && (int)tail.size() < room; ++l)
                    tail.push_back(text(" " + *l) | tint(it->tone));
            }
            std::reverse(tail.begin(), tail.end());
            for (auto& e : tail) lines.push_back(e);
        }
        return pane(text(" Details ") | bold, vbox(std::move(lines)), Color::GrayDark);
    }

    Element header(int cols) const {
        std::map<std::string, int> count;
        int selected = 0;
        for (auto& p : pairs_) {
            if (p->selected) selected++;
            if (!p->last.dry) count[p->last.status]++;
        }
        struct Bit {
            std::string text;
            Tone tone;
        };
        std::vector<Bit> bits;
        if (!scanned_) {
            bits.push_back({"looking through the folder", Tone::Dim});
        } else {
            std::string total = plural((long long)pairs_.size(), "subtitle", "subtitles");
            if (shown_.size() != pairs_.size()) total = std::to_string(shown_.size()) + " of " + total;
            bits.push_back({total, Tone::Plain});
        }
        if (selected) bits.push_back({std::to_string(selected) + " selected", Tone::Warn});
        if (count["solid"]) bits.push_back({std::to_string(count["solid"]) + " solid", Tone::Good});
        if (count["same"]) bits.push_back({std::to_string(count["same"]) + " in sync", Tone::Plain});
        if (count["unsure"]) bits.push_back({std::to_string(count["unsure"]) + " unsure", Tone::Warn});
        int bad = count["nothing"] + count["few"] + count["error"];
        if (bad) bits.push_back({std::to_string(bad) + " failed", Tone::Bad});
        if (scanned_ && unmatched_) bits.push_back({std::to_string(unmatched_) + " without a video", Tone::Dim});

        const std::string badge = " LAPSE ";
        int folder_min = std::min(cells(shown(native(root_))), 16);
        auto width = [&] {
            int w = 0;
            for (size_t i = 0; i < bits.size(); i++) w += cells(bits[i].text) + (i ? 3 : 0);
            return w;
        };
        while (bits.size() > 1 && cells(badge) + 2 + folder_min + 2 + width() + 1 > cols) bits.pop_back();

        Elements right;
        for (size_t i = 0; i < bits.size(); i++) {
            if (i) right.push_back(text(" · ") | dim);
            right.push_back(text(bits[i].text) | tint(bits[i].tone));
        }
        int folder_w = cols - cells(badge) - 2 - width() - 2;
        return hbox({
            text(badge) | bold | color(Color::Black) | bgcolor(Color::Cyan),
            text(" " + cut_front(shown(native(root_)), std::max(4, folder_w)) + " ") | bold,
            filler(),
            hbox(std::move(right)),
            text(" "),
        });
    }

    Element options_bar(bool compact) const {
        Elements out = {text(" "), text(options_.mode) | bold | color(Color::Cyan)};
        auto flag = [&](const std::string& name, bool on, Color c) {
            if (on) out.push_back(hbox({text(" "), text(" " + name + " ") | bold | color(Color::Black) | bgcolor(c)}));
            else if (!compact) out.push_back(text("  " + name) | dim);
        };
        flag("dry run", options_.dry, Color::Yellow);
        flag("force", options_.force, Color::Magenta);
        flag("no backup", options_.no_backup, Color::Red);
        out.push_back(text(" "));
        return hbox(std::move(out));
    }

    Element status_line(int cols, Clock::time_point now) const {
        bool compact = cols < 100 && (running_ || typing_ == Typing::Filter);
        Element options = options_bar(compact);
        options->ComputeRequirement();
        int left_w = cols - options->requirement().min_x;

        Element left;
        if (typing_ == Typing::Filter) {
            left = hbox({text(" Filter ") | bold | color(Color::Black) | bgcolor(Color::Cyan), text(" "),
                         text(cut_front(typed_, std::max(1, left_w - 34))), text(" ") | inverted,
                         text("  Enter keeps it, Esc clears it") | dim});
        } else if (running_) {
            const Pair& p = *running_;
            std::string verb = running_kind_ == Kind::Sync ? "" : running_kind_ == Kind::Undo ? "undo " : "shift ";
            std::string waiting = queue_.empty() ? "" : "  +" + std::to_string(queue_.size()) + " waiting";
            int gauge_w = std::clamp(left_w / 6, 6, 20);
            int room = left_w - 3 - gauge_w - 6 - cells(waiting) - 2;
            std::string name = squeeze(verb + p.name, std::max(6, std::min(cells(verb + p.name), room / 2)));
            std::string step = cut(p.step, std::max(0, room - cells(name)));
            float done = p.percent < 0 ? 0.f : (float)p.percent / 100.f;
            left = hbox({
                text(" " + spin(now) + " ") | color(Color::Cyan),
                text(name) | bold,
                text(" "),
                gauge(done) | color(Color::Cyan) | bgcolor(Color::GrayDark) | size(WIDTH, EQUAL, gauge_w),
                text(pad_left(p.percent < 0 ? "" : std::to_string(p.percent) + "%", 5) + " "),
                text(step) | dim,
                text(waiting) | dim,
            });
        } else if (!flash_.empty() && now - flash_at_ < std::chrono::seconds(8)) {
            left = text(" " + cut(flash_, left_w - 2)) | tint(flash_tone_);
        } else if (scanning_) {
            left = text(" Looking through the folder " + spin(now)) | dim;
        } else {
            left = text(" " + cut("Enter syncs, Space selects, ? shows every key", left_w - 2)) | dim;
        }
        return hbox({left | size(WIDTH, EQUAL, left_w), options});
    }

    Element key_bar(int cols) const {
        struct Key {
            std::string key, label;
            int keep;
        };
        std::vector<Key> keys = {
            {"↑↓", "Move", 1}, {"Space", "Select", 5}, {"Enter", "Sync", 10}, {"a", "All", 8},
            {"u", "Undo", 7}, {"s", "Shift", 6}, {"d", "Dry run", 6}, {"f", "Force", 4},
            {"b", "No .bak", 3}, {"m", "Mode", 4}, {"/", "Filter", 3}, {"r", "Rescan", 2},
            {"x", "Clear", 1}, {"?", "Help", 9}, {"q", "Quit", 10},
        };
        auto width = [&] {
            int w = 1;
            for (auto& k : keys) w += cells(k.key) + 1 + cells(k.label) + 2;
            return w;
        };
        while (width() > cols && keys.size() > 1) {
            size_t drop = 0;
            for (size_t i = 0; i < keys.size(); i++)
                if (keys[i].keep <= keys[drop].keep) drop = i;
            keys.erase(keys.begin() + (long)drop);
        }
        Elements out = {text(" ")};
        for (auto& k : keys) {
            out.push_back(text(k.key) | bold | color(Color::Cyan));
            out.push_back(text(" " + k.label + "  "));
        }
        return hbox(std::move(out));
    }

    Element boxed(const std::string& title, Elements lines, Color edge = Color::Cyan) const {
        return pane(text(" " + title + " ") | bold, vbox(std::move(lines)), edge);
    }

    Element help_box(int cols) const {
        static const std::vector<std::pair<std::string, std::string>> keys = {
            {"↑ ↓ PgUp PgDn", "move through the list"},
            {"Home End", "jump to the first or the last"},
            {"Space", "select or deselect"},
            {"Enter", "sync the selected ones, or this one"},
            {"a", "sync every one shown not done yet"},
            {"u", "put the .bak back"},
            {"s", "move every line by hand, in ms"},
            {"d f b", "dry run, force, no backup: on or off"},
            {"m", "mode: auto, ols, nosplit or split"},
            {"x", "empty the queue"},
            {"/", "filter by name, Esc clears it"},
            {"r", "look through the folder again"},
            {"q", "quit"},
        };
        int w = std::min(56, cols - 4);
        int key_w = 15;
        Elements lines;
        for (auto& [key, what] : keys)
            lines.push_back(hbox({text(" " + pad(key, key_w)) | bold | color(Color::Cyan), text(cut(what, w - key_w - 4) + " ")}));
        lines.push_back(text(""));
        lines.push_back(hbox({text(" ✓") | bold | color(Color::Yellow), text(" selected   "),
                              text("•") | color(Color::Cyan), text(" has a .bak")}));
        lines.push_back(hbox({text(" solid") | color(Color::Green), text("  "), text("unsure") | color(Color::Yellow),
                              text("  "), text("nothing") | color(Color::Red), text("  "), text("(solid)") | dim,
                              text(" dry run")}));
        lines.push_back(text(""));
        lines.push_back(text(" Any key closes this") | dim);
        return boxed("Keys", std::move(lines)) | size(WIDTH, EQUAL, w);
    }

    Element ask_box(int cols) const {
        int w = std::min(60, cols - 4);
        Elements lines;
        for (auto& l : ask_->lines)
            for (auto& part : wrap(l, w - 4)) lines.push_back(text(" " + part + " "));
        lines.push_back(text(""));
        lines.push_back(hbox({text(" y") | bold | color(Color::Cyan), text(" yes   "),
                              text("n") | bold | color(Color::Cyan), text(" no")}));
        return boxed(ask_->title, std::move(lines), Color::Yellow) | size(WIDTH, EQUAL, w);
    }

    Element shift_box(int cols) const {
        int w = std::min(56, cols - 4);
        Elements lines;
        for (auto& part : wrap("Move every line of " + (shift_for_ ? shift_for_->name : std::string()) + " by", w - 4))
            lines.push_back(text(" " + part));
        lines.push_back(hbox({text(" "), text(typed_) | bold, text(" ") | inverted, text(" ms")}));
        lines.push_back(text(""));
        for (auto& part : wrap("Negative is earlier. Enter moves it, Esc leaves it alone.", w - 4))
            lines.push_back(text(" " + part) | dim);
        if (options_.dry) lines.push_back(text(" Dry run is on, nothing will be written.") | color(Color::Yellow));
        return boxed("Shift", std::move(lines)) | size(WIDTH, EQUAL, w);
    }

    Element render() {
        std::lock_guard<std::mutex> hold(lock_);
        auto now = Clock::now();
        auto size_now = Terminal::Size();
        int cols = size_now.dimx, rows = size_now.dimy;
        if (cols < 50 || rows < 12)
            return vbox({filler(), text("Make the window a little bigger") | bold | hcenter,
                         text("lapse-tui wants 50 by 12 at least") | dim | hcenter, filler()});

        int left_w = std::clamp(cols * 2 / 3, 36, cols - 28);
        int body_h = rows - 3;
        int inner_h = body_h - 2;
        page_ = inner_h;
        if (cursor_ < top_) top_ = cursor_;
        if (cursor_ >= top_ + inner_h) top_ = cursor_ - inner_h + 1;
        top_ = std::clamp(top_, 0, std::max(0, (int)shown_.size() - inner_h));

        Element body = hbox({
            list_pane(left_w, inner_h, now) | size(WIDTH, EQUAL, left_w),
            details_pane(cols - left_w, inner_h, now) | flex,
        }) | size(HEIGHT, EQUAL, body_h);

        Element screen = vbox({header(cols), body, status_line(cols, now), key_bar(cols)});
        if (help_) return dbox({screen, help_box(cols) | clear_under | center});
        if (ask_) return dbox({screen, ask_box(cols) | clear_under | center});
        if (typing_ == Typing::Shift) return dbox({screen, shift_box(cols) | clear_under | center});
        return screen;
    }

    std::string summarise() {
        std::lock_guard<std::mutex> hold(lock_);
        std::map<std::string, int> count;
        for (auto& p : pairs_)
            if (!p->last.dry && !p->last.status.empty()) count[p->last.status]++;
        std::vector<std::string> parts;
        if (count["solid"]) parts.push_back(std::to_string(count["solid"]) + " synced");
        if (count["same"]) parts.push_back(std::to_string(count["same"]) + " already in sync");
        if (count["unsure"]) parts.push_back(std::to_string(count["unsure"]) + " unsure");
        if (count["nothing"]) parts.push_back(std::to_string(count["nothing"]) + " with nothing to go on");
        if (count["few"] + count["error"]) parts.push_back(std::to_string(count["few"] + count["error"]) + " failed");
        if (count["shifted"]) parts.push_back(std::to_string(count["shifted"]) + " shifted");
        if (count["undone"]) parts.push_back(std::to_string(count["undone"]) + " put back");
        if (parts.empty()) return "";
        std::string out = "lapse-tui: ";
        for (size_t i = 0; i < parts.size(); i++) out += (i ? ", " : "") + parts[i];
        return out;
    }
};

void usage(std::ostream& out) {
    out << "Usage: lapse-tui [folder]\n"
           "Pairs every subtitle in the folder, and the folders under it, with its video\n"
           "and syncs them from a list. The folder is the current one when none is given.\n"
           "Press ? inside for the keys.\n";
}

}  // namespace

int main(int argc, const char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::string folder;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(std::cout);
            return 0;
        }
        if (arg == "--version") {
            std::cout << LAPSE_VERSION << '\n';
            return 0;
        }
        if ((arg.size() > 1 && arg[0] == '-') || !folder.empty()) {
            std::cerr << "lapse-tui takes one folder and nothing else, not " << arg << "\n";
            usage(std::cerr);
            return 1;
        }
        folder = arg;
    }
    if (folder.empty()) folder = ".";

    std::error_code ec;
    fs::path root = fs::absolute(fs::path(folder), ec).lexically_normal();
    if (!root.has_filename() && root.has_relative_path()) root = root.parent_path();
    if (ec || !fs::is_directory(root, ec)) {
        std::cerr << "Not a folder: " << folder << '\n';
        return 1;
    }
    if (!isatty(fileno(stdin)) || !isatty(fileno(stdout))) {
        std::cerr << "lapse-tui draws on a terminal. For scripts, use lapse itself\n";
        return 1;
    }

    std::set<std::string> readable(SUBTITLE_FORMATS.begin(), SUBTITLE_FORMATS.end());
    av_log_set_callback(ffmpeg_said);

    auto screen = ScreenInteractive::Fullscreen();
    screen.TrackMouse(false);
    screen.ForceHandleCtrlC(false);
    Tui tui(root, readable, screen);
    tui.start();
    screen.Loop(tui.component());
    return tui.finish();
}
