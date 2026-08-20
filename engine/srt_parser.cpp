// LAPSE - Language-Agnostic subtitle synchronization engine
// Copyright (C) 2026 Rasmus Stisen Jensen (r-stisen)
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

#include "srt_parser.h"
#include "charset.h"
#include "log.h"

// A utf-8 BOM sits in front of the very first line and would otherwise trip up
// whatever we try to read out of it
static void strip_bom(std::string& line) {
    if (line.size() >= 3 &&
        (unsigned char)line[0] == 0xEF &&
        (unsigned char)line[1] == 0xBB &&
        (unsigned char)line[2] == 0xBF)
        line.erase(0, 3);
}

std::string load_text(const std::string& path, Charset* was) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open subtitle: " + path);

    in.seekg(0, std::ios::end);
    std::streamoff bytes = in.tellg();
    if (bytes > (std::streamoff)MAX_SUBTITLE_BYTES)
        throw std::runtime_error("That file is " + std::to_string((long long)(bytes / (1024 * 1024))) +
                                 "MB, which is not a subtitle: " + path);
    in.seekg(0, std::ios::beg);

    std::string raw((std::istreambuf_iterator<char>(in)), {});
    Charset how = sniff(raw);
    if (was) *was = how;
    return decode(raw, how);
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Reads a timestamp starting at "from" and returns it in ms, or -1 if there
// isnt one. We just walk the digits instead of counting characters - files in
// the wild write 0:00:01.00, 00:00:01,000 and 00:01.000 and all of them are
// supposed to work
int parse_timestamp(const std::string& line, size_t from) {
    std::vector<long long> parts;
    long long value = 0;
    int digits = 0;
    bool started = false;
    bool fraction = false;

    size_t i = from;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;

    for (; i < line.size(); i++) {
        char c = line[i];
        if (c >= '0' && c <= '9') {
            if (digits < 9) value = value * 10 + (c - '0');
            digits++;
            started = true;
        } else if (started && (c == ':' || c == ',' || c == '.')) {
            if (parts.size() >= 3) return -1;
            parts.push_back(value);
            fraction = (c == ',' || c == '.');
            value = 0;
            digits = 0;
        } else {
            break;  // whitespace or vtt cue settings - the timestamp ended here
        }
    }
    if (!started) return -1;
    parts.push_back(value);

    // Last group is the fraction. Three digits means milliseconds, two means
    // the centiseconds that ass files use
    long long frac = 0;
    if (fraction) {
        frac = parts.back();
        parts.pop_back();
        if (digits == 2) frac *= 10;
        else if (digits == 1) frac *= 100;
        else for (int d = digits; d > 3; d--) frac /= 10;
    }

    if (parts.size() < 2 || parts.size() > 3) return -1;
    long long sec = parts.back(); parts.pop_back();
    long long min = parts.back(); parts.pop_back();
    long long hour = parts.empty() ? 0 : parts.back();

    long long ms = hour * 3600000 + min * 60000 + sec * 1000 + frac;
    if (ms < 0 || ms > MAX_TIME_MS) return -1;
    return (int)ms;
}

// A MicroDVD line is {start}{end}text, both frame numbers. Gives back where the
// text begins so the writer can put the same line back together
bool sub_frames(const std::string& line, long long& a, long long& b, size_t& text_from) {
    if (line.empty() || line[0] != '{') return false;
    size_t one = line.find('}');
    if (one == std::string::npos || one + 1 >= line.size() || line[one + 1] != '{') return false;
    size_t two = line.find('}', one + 2);
    if (two == std::string::npos) return false;

    a = atoll(line.substr(1, one - 1).c_str());
    b = atoll(line.substr(one + 2, two - one - 2).c_str());
    text_from = two + 1;
    return true;
}

// What the video runs at, once somebody has looked it up. That beats anything
// the subtitle claims about itself, since the frames get counted against that
// video in the end
static double known_fps = 0;

void set_sub_fps(double fps) {
    known_fps = (fps >= 10 && fps <= 120) ? fps : 0;
}

// Frames only mean something once we know the rate. MicroDVD files declare it
// as the text of a {1}{1} line at the top. Zero when nobody ever said, and
// then we are not going to invent one
double sub_fps(const std::string& text) {
    if (known_fps > 0) return known_fps;


    std::istringstream ss(text);
    std::string line;
    while (getline(ss, line)) {
        strip_bom(line);
        if (trim(line).empty()) continue;

        long long a, b;
        size_t text_from;
        if (!sub_frames(line, a, b, text_from)) break;
        double fps = atof(trim(line.substr(text_from)).c_str());
        if (a <= 1 && b <= 1 && fps >= 10 && fps <= 120) return fps;
        break;
    }
    return 0;
}

int frames_to_ms(long long frame, double fps) {
    if (frame < 0 || fps <= 0) return -1;
    long long ms = (long long)(frame * 1000.0 / fps + 0.5);
    if (ms > MAX_TIME_MS) return -1;
    return (int)ms;
}

std::vector<std::pair<int,int>> read_subtitle(const std::string& path) {
    if (path.ends_with(".srt"))
        return read_srt(path.c_str());
    if (path.ends_with(".ass") || path.ends_with(".ssa"))
        return read_ass(path.c_str());
    if (path.ends_with(".vtt"))
        return read_vtt(path.c_str());
    if (path.ends_with(".sub"))
        return read_sub(path.c_str());
    if (path.ends_with(".sup"))
        return read_sup(path.c_str());
    throw std::runtime_error("Unsupported subtitle format: " + path);
}

// Every line holding "-->" gets an entry even when we cannot read it - the
// writers walk the same lines later and would drift out of step otherwise
std::vector<std::pair<int, int>> read_srt(const char* filename) {
    std::vector<std::pair<int, int>> timestamps;
    std::string line {};
    std::istringstream read_file(load_text(filename));
    bool first = true;
    while (getline (read_file, line)) {
        if (first) { strip_bom(line); first = false; }

        size_t arrow = line.find("-->");
        if (arrow == std::string::npos) continue;

        if ((int)timestamps.size() >= MAX_CUES) {
            say() << "Stopping at " << MAX_CUES << " cues, the rest of this file is left where it is\n";
            break;
        }

        int start_ms = parse_timestamp(line, 0);
        int end_ms   = parse_timestamp(line, arrow + 3);

        if (start_ms < 0 || end_ms < 0)
            timestamps.push_back({0, 0});   // dropped later, keeps the count right
        else
            timestamps.push_back(std::make_pair(start_ms, end_ms));
    }
    return timestamps;
}

// Positions of the commas on a Dialogue or Format line
std::vector<size_t> ass_commas(const std::string& line) {
    std::vector<size_t> commas;
    for (size_t i = 0; i < line.size(); i++)
        if (line[i] == ',') commas.push_back(i);
    return commas;
}

// Field 0 is the one right after the colon. Only works for fields that have a
// comma after them, which is fine since we never touch the text field
bool ass_field(const std::string& line, const std::vector<size_t>& commas, int index, size_t& from, size_t& len) {
    if (index < 0 || index >= (int)commas.size()) return false;
    size_t colon = line.find(':');
    if (colon == std::string::npos) return false;

    from = (index == 0) ? colon + 1 : commas[index - 1] + 1;
    if (from > commas[index]) return false;
    len = commas[index] - from;
    return true;
}

// The Format line says which columns hold the times. Almost every file puts
// them at 1 and 2 but the spec lets them sit anywhere
std::pair<int,int> ass_time_columns(const std::string& format_line) {
    std::vector<size_t> commas = ass_commas(format_line);
    int start_col = -1;
    int end_col = -1;

    for (int i = 0; i < (int)commas.size(); i++) {
        size_t from, len;
        if (!ass_field(format_line, commas, i, from, len)) continue;
        std::string name = trim(format_line.substr(from, len));
        if (name == "Start") start_col = i;
        if (name == "End")   end_col = i;
    }
    return {start_col, end_col};
}

std::vector<std::pair<int, int>> read_ass(const char* filename) {
    std::vector<std::pair<int, int>> timestamps;
    std::string line {};
    std::istringstream read_file(load_text(filename));
    int start_col = 1;
    int end_col = 2;
    bool first = true;

    while (getline(read_file, line)) {
        if (first) { strip_bom(line); first = false; }

        if (line.rfind("Format:", 0) == 0) {
            auto [s, e] = ass_time_columns(line);
            if (s >= 0 && e >= 0) { start_col = s; end_col = e; }
            continue;
        }
        if (line.rfind("Dialogue:", 0) != 0) continue;

        if ((int)timestamps.size() >= MAX_CUES) {
            say() << "Stopping at " << MAX_CUES << " cues, the rest of this file is left where it is\n";
            break;
        }

        std::vector<size_t> commas = ass_commas(line);
        size_t from, len;
        int start_ms = -1;
        int end_ms = -1;

        if (ass_field(line, commas, start_col, from, len))
            start_ms = parse_timestamp(line.substr(from, len), 0);
        if (ass_field(line, commas, end_col, from, len))
            end_ms = parse_timestamp(line.substr(from, len), 0);

        if (start_ms < 0 || end_ms < 0)
            timestamps.push_back({0, 0});
        else
            timestamps.push_back({start_ms, end_ms});
    }
    return timestamps;
}

// vtt cues look just like srt ones once we stop counting characters
std::vector<std::pair<int,int>> read_vtt(const char* filename) {
    return read_srt(filename);
}

std::vector<std::pair<int,int>> read_sub(const char* filename) {
    std::vector<std::pair<int,int>> timestamps;
    std::string text = load_text(filename);
    double fps = sub_fps(text);
    std::istringstream read_file(text);
    std::string line {};
    bool first = true;

    while (getline(read_file, line)) {
        if (first) { strip_bom(line); first = false; }

        long long a, b;
        size_t text_from;
        if (!sub_frames(line, a, b, text_from)) continue;

        if ((int)timestamps.size() >= MAX_CUES) {
            say() << "Stopping at " << MAX_CUES << " cues, the rest of this file is left where it is\n";
            break;
        }

        int start_ms = frames_to_ms(a, fps);
        int end_ms   = frames_to_ms(b, fps);

        if (start_ms < 0 || end_ms < 0)
            timestamps.push_back({0, 0});
        else
            timestamps.push_back({start_ms, end_ms});
    }
    return timestamps;
}

std::vector<std::pair<int,int>> read_sup(const char* filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open subtitle: " + std::string(filename));
    std::string data((std::istreambuf_iterator<char>(in)), {});
    const unsigned char* b = (const unsigned char*)data.data();
    size_t n = data.size();

    std::vector<std::pair<int,bool>> marks;
    size_t pos = 0;
    while (pos + 13 <= n) {
        if (b[pos] != 'P' || b[pos + 1] != 'G') break;
        unsigned int pts = ((unsigned int)b[pos+2] << 24) | ((unsigned int)b[pos+3] << 16) | ((unsigned int)b[pos+4] << 8) | b[pos+5];
        unsigned char type = b[pos + 10];
        unsigned int size = ((unsigned int)b[pos+11] << 8) | b[pos+12];
        size_t payload = pos + 13;
        if (payload + size > n) break;

        if (type == 0x16) {
            bool show = size >= 11 && b[payload + 10] > 0;
            marks.push_back({(int)(pts / 90), show});
        }
        pos = payload + size;
    }

    std::vector<std::pair<int,int>> timestamps;
    for (size_t i = 0; i < marks.size(); i++) {
        if (!marks[i].second) continue;
        if ((int)timestamps.size() >= MAX_CUES) {
            say() << "Stopping at " << MAX_CUES << " cues, the rest of this file is left where it is\n";
            break;
        }
        int start = marks[i].first;
        int end = (i + 1 < marks.size()) ? marks[i + 1].first : start + 2000;
        if (end <= start) end = start + 2000;
        if (end - start > MAX_CUE_MS) end = start + MAX_CUE_MS;
        timestamps.push_back({start, end});
    }
    return timestamps;
}


// Cues that sit on top of each other normally get glued into one span - they
// are the same moment on screen and counting them twice only skews the score.
// Split mode asks for them separately though: when a file jumps halfway through
// the two halves overlap each other once sorted, and merging across that jump
// welds cues from either side of it together so they can never come apart again
std::pair<std::vector<std::pair<int,int>>, std::vector<int>> process_spans(const std::vector<std::pair<int, int>>& timestamps, bool merge, bool sort_by_time) {
    // Sort the cues by time but remember where each one sat in the file the
    // writers go through the file top to bottom and look their span back up
    std::vector<std::pair<std::pair<int,int>, int>> ys;
    for (int i = 0; i < (int)timestamps.size(); i++) {
        int start = timestamps[i].first;
        int end   = timestamps[i].second;
        if (end < start) std::swap(start, end);
        if (end == start) continue;    // empty or unreadable cue, nothing to line up
        ys.push_back({{start, end}, i});
    }

    // Split mode wants them in the order they appear in the file. Everywhere
    // else that is the same order, but where a file has been recut the two
    // disagree, and there the file is the one telling the truth
    if (sort_by_time) std::sort(ys.begin(), ys.end());

    std::vector<std::pair<int,int>> spans;
    std::vector<int> mapping(timestamps.size(), 0);
    std::vector<bool> kept(timestamps.size(), false);

    for (int i = 0; i < (int)ys.size(); i++) {
        if (!merge || spans.empty() || ys[i].first.first >= spans.back().second) {
            spans.push_back(ys[i].first);
        } else {
            spans.back().second = std::max(ys[i].first.second, spans.back().second);
        }
        mapping[ys[i].second] = (int)spans.size() - 1;
        kept[ys[i].second] = true;
    }

    // a sign left up for forty seconds used to outweigh a dozen real lines
    // just for being long. only the scoring sees this, the file keeps its times
    for (auto& s : spans)
        if (s.second - s.first > MAX_CUE_MS) s.second = s.first + MAX_CUE_MS;

    // The cues we threw out still take up a line in the file point them at
    // the span before them so the writer keeps shifting them along with it
    int last = 0;
    for (int i = 0; i < (int)mapping.size(); i++) {
        if (kept[i]) last = mapping[i];
        else mapping[i] = last;
    }

    return {spans, mapping};
}



// Build some sort of activity profile that checks every 10ms for dialogue
std::vector<int> activity(const std::vector<std::pair<int, int>>& spans) {
    if (spans.empty()) return {};
    std::vector<int> activity_profile = {};
    int j = 0;
    for (int i = 0; i <= spans.back().second; i += 10) {
        while (j < (int)spans.size() && i > spans[j].second) {
            j++;
        }
        if (j < (int)spans.size() && i >= spans[j].first && i <= spans[j].second) {
            activity_profile.push_back(1);
        } else {
            activity_profile.push_back(0);
        }
    }
    return activity_profile;
}

// Turns the vad output back into spans. One entry is one 10ms frame so the index has to be scaled before anything compares this to subtitle timestamps
std::pair<std::vector<std::pair<int, int>>, std::vector<float>> reference_spans(const std::vector<float>& probability) {
    std::vector<std::pair<int, int>> raw;
    std::vector<float> raw_weight;
    bool check = false;
    int start_ms = 0;
    double total = 0;
    int frames = 0;

    for (int i = 0; i < (int)probability.size(); i++) {
        bool speech = probability[i] >= SPEECH_THRESHOLD;

        if (speech && !check) {
            check = true;
            start_ms = i * 10;
            total = 0;
            frames = 0;
        }
        if (speech) {
            total += probability[i];
            frames++;
        }
        if ((!speech && check) || (i == (int)probability.size() - 1 && check)) {
            check = false;
            raw.push_back({start_ms, i * 10});
            raw_weight.push_back(frames > 0 ? (float)(total / frames) : 0.0f);
        }
    }

    // The vad flickers on and off inside a sentence, so glue spans that are only a breath apart together
    //and drop whatever is left too short to be speech single frames of noise otherwise fill the reference with junk
    std::vector<std::pair<int, int>> spans;
    std::vector<float> weights;
    for (int i = 0; i < (int)raw.size(); i++) {
        if (!spans.empty() && raw[i].first - spans.back().second < 200) {
            int a = spans.back().second - spans.back().first;
            int b = raw[i].second - raw[i].first;
            if (a + b > 0)
                weights.back() = (weights.back() * a + raw_weight[i] * b) / (a + b);
            spans.back().second = raw[i].second;
        } else {
            spans.push_back(raw[i]);
            weights.push_back(raw_weight[i]);
        }
    }

    std::vector<std::pair<int, int>> out_spans;
    std::vector<float> out_weights;
    for (int i = 0; i < (int)spans.size(); i++) {
        // nobody says anything in a fifth of a second - below that it is a
        // door, a cough, or the vad twitching
        if (spans[i].second - spans[i].first >= MIN_SPEECH_MS) {
            out_spans.push_back(spans[i]);
            out_weights.push_back(weights[i]);
        }
    }

    return {out_spans, out_weights};
}
