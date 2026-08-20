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

#include "write_subtitle.h"
#include "srt_parser.h"


void backup_file(const char* path) {
    std::string backup = std::string(path) + ".bak";
    if (std::filesystem::exists(backup)) return;
    std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing);
}

// How a timestamp gets moved. Either one line for the whole file, or a lookup per cue, and both together when the file drifts and was also cut about
struct Shift {
    double slope = 0;
    double intercept_s = 0;
    std::vector<int> offsets;
    std::vector<int> mapping;

    int apply(int ms, int cue) const {
        double stretched = ms * (1.0 + slope) + intercept_s * 1000.0;
        if (mapping.empty()) return (int)stretched;
        if (cue < 0 || cue >= (int)mapping.size()) return (int)stretched;
        int span = mapping[cue];
        if (span < 0 || span >= (int)offsets.size()) return (int)stretched;
        return (int)stretched + offsets[span];
    }
};

// goes back out the way it came in - utf-16 used to come back as utf-8
static Charset came_as = Charset::Legacy;

static std::string load_file(const char* path) {
    return load_text(path, &came_as);
}

// temp file then rename, so a throw halfway leaves the old file whole
static void save_file(const char* output_path, const std::string& text) {
    std::string temp_path = std::string(output_path) + ".tmp";
    std::ofstream out(temp_path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write subtitle: " + std::string(output_path));

    std::string bytes = encode(text, came_as);
    out.write(bytes.data(), bytes.size());
    out.close();
    std::filesystem::rename(temp_path, output_path);
}

static std::string ms_to_ts(int ms, char ms_sep) {
    if (ms < 0) ms = 0;
    int h  = ms / 3600000; ms %= 3600000;
    int m  = ms / 60000;   ms %= 60000;
    int sc = ms / 1000;    ms %= 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d%c%03d", h, m, sc, ms_sep, ms);
    return buf;
}

static std::string ms_to_ass_ts(int ms) {
    if (ms < 0) ms = 0;
    int h  = ms / 3600000; ms %= 3600000;
    int m  = ms / 60000;   ms %= 60000;
    int sc = ms / 1000;    ms %= 1000;
    int cs = ms / 10;
    char buf[16];
    snprintf(buf, sizeof(buf), "%01d:%02d:%02d.%02d", h, m, sc, cs);
    return buf;
}

static std::string ms_to_frames(int ms, double fps) {
    if (ms < 0) ms = 0;
    return std::to_string((long long)(ms * fps / 1000.0 + 0.5));
}

// srt and vtt are the same file with a different character in front of the
// milliseconds, so they go through here together. We write to a temp file and
// move it into place at the end if something throws halfway the subtitle the user already had is still whole
static void write_cues(const char* input_path, const char* output_path, char ms_sep, const Shift& shift) {
    std::string text = load_file(input_path);
    bool ends_clean = !text.empty() && text.back() == '\n';
    std::istringstream ss(text);

    std::string out;

    std::string line;
    int cue = 0;
    const char* eol = "\n";
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
            eol = "\r\n";
        }

        size_t arrow = line.find("-->");
        if (arrow != std::string::npos) {
            int start_ms = parse_timestamp(line, 0);
            int end_ms   = parse_timestamp(line, arrow + 3);

            if (start_ms >= 0 && end_ms >= 0) {
                // vtt hangs cue settings on the end of the line, keep them
                std::string tail;
                size_t after = line.find_first_not_of(" \t", arrow + 3);
                if (after != std::string::npos) {
                    size_t space = line.find_first_of(" \t", after);
                    if (space != std::string::npos) tail = line.substr(space);
                }
                line = ms_to_ts(shift.apply(start_ms, cue), ms_sep) + " --> " + ms_to_ts(shift.apply(end_ms, cue), ms_sep) + tail;
            }
            cue++;       // counted even when it did not parse, the reader did the same
        }

        out += line;
        if (ends_clean || !ss.eof()) out += eol;
    }

    save_file(output_path, out);
}

static void write_sbv(const char* input_path, const char* output_path, const Shift& shift) {
    std::string text = load_file(input_path);
    bool ends_clean = !text.empty() && text.back() == '\n';
    std::istringstream ss(text);

    std::string out;

    std::string line;
    int cue = 0;
    const char* eol = "\n";
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
            eol = "\r\n";
        }

        std::string t = trim(line);
        if (sbv_time_line(t)) {
            size_t comma = t.find(',');
            std::string left = t.substr(0, comma);
            std::string right = t.substr(comma + 1);
            int start_ms = parse_timestamp(left, 0);
            int end_ms   = parse_timestamp(right, 0);

            if (start_ms >= 0 && end_ms >= 0)
                line = ms_to_ts(shift.apply(start_ms, cue), '.') + "," + ms_to_ts(shift.apply(end_ms, cue), '.');
            cue++;
        }

        out += line;
        if (ends_clean || !ss.eof()) out += eol;
    }

    save_file(output_path, out);
}

static void write_dialogue(const char* input_path, const char* output_path, const Shift& shift) {
    std::string text = load_file(input_path);
    bool ends_clean = !text.empty() && text.back() == '\n';
    std::istringstream ss(text);

    std::string out;

    std::string line;
    int cue = 0;
    int start_col = 1;
    int end_col = 2;
    const char* eol = "\n";

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
            eol = "\r\n";
        }

        if (line.rfind("Format:", 0) == 0) {
            auto [s, e] = ass_time_columns(line);
            if (s >= 0 && e >= 0) { start_col = s; end_col = e; }

        } else if (line.rfind("Dialogue:", 0) == 0) {
            std::vector<size_t> commas = ass_commas(line);
            size_t sf, sl, ef, el;

            if (ass_field(line, commas, start_col, sf, sl) && ass_field(line, commas, end_col, ef, el)) {
                int start_ms = parse_timestamp(line.substr(sf, sl), 0);
                int end_ms   = parse_timestamp(line.substr(ef, el), 0);

                if (start_ms >= 0 && end_ms >= 0) {
                    std::string new_start = ms_to_ass_ts(shift.apply(start_ms, cue));
                    std::string new_end   = ms_to_ass_ts(shift.apply(end_ms, cue));

                    // Put the later field back first so the earlier one keeps
                    // the position we just looked up
                    if (sf < ef) {
                        line.replace(ef, el, new_end);
                        line.replace(sf, sl, new_start);
                    } else {
                        line.replace(sf, sl, new_start);
                        line.replace(ef, el, new_end);
                    }
                }
            }
            cue++;
        }

        out += line;
        if (ends_clean || !ss.eof()) out += eol;
    }

    save_file(output_path, out);
}

// MicroDVD lines get counted in frames, so the times go back out through the
// rate the file was read at. The {1}{1} line at the top is the rate itself and
// has to stay where it is
static void write_frames(const char* input_path, const char* output_path, const Shift& shift) {
    std::string text = load_file(input_path);
    double fps = sub_fps(text);
    bool ends_clean = !text.empty() && text.back() == '\n';
    std::istringstream ss(text);

    std::string out;

    std::string line;
    int cue = 0;
    const char* eol = "\n";
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
            eol = "\r\n";
        }

        long long a, b;
        size_t text_from;
        if (sub_frames(line, a, b, text_from)) {
            int start_ms = frames_to_ms(a, fps);
            int end_ms   = frames_to_ms(b, fps);
            bool rate_line = (cue == 0 && a <= 1 && b <= 1);

            // the frames we are about to write mean this rate, so say so
            if (rate_line) {
                char rate[32];
                snprintf(rate, sizeof(rate), "%.3f", fps);
                line = "{1}{1}" + std::string(rate);
            } else if (start_ms >= 0 && end_ms >= 0)
                line = "{" + ms_to_frames(shift.apply(start_ms, cue), fps) + "}{" +
                       ms_to_frames(shift.apply(end_ms, cue), fps) + "}" + line.substr(text_from);
            cue++;
        }

        out += line;
        if (ends_clean || !ss.eof()) out += eol;
    }

    save_file(output_path, out);
}

static void write_sup(const char* input_path, const char* output_path, const Shift& shift) {
    std::ifstream in(input_path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open subtitle: " + std::string(input_path));
    std::string data((std::istreambuf_iterator<char>(in)), {});
    in.close();

    unsigned char* b = (unsigned char*)&data[0];
    size_t n = data.size();
    size_t pos = 0;
    int cue = -1;

    while (pos + 13 <= n) {
        if (b[pos] != 'P' || b[pos + 1] != 'G') break;
        unsigned int pts = ((unsigned int)b[pos+2] << 24) | ((unsigned int)b[pos+3] << 16) | ((unsigned int)b[pos+4] << 8) | b[pos+5];
        unsigned char type = b[pos + 10];
        unsigned int size = ((unsigned int)b[pos+11] << 8) | b[pos+12];
        size_t payload = pos + 13;
        if (payload + size > n) break;

        bool show = type == 0x16 && size >= 11 && b[payload + 10] > 0;
        if (show) cue++;

        int new_ms = shift.apply((int)(pts / 90), cue);
        if (new_ms < 0) new_ms = 0;
        unsigned int new_pts = (unsigned int)new_ms * 90;
        b[pos+2] = (unsigned char)(new_pts >> 24);
        b[pos+3] = (unsigned char)(new_pts >> 16);
        b[pos+4] = (unsigned char)(new_pts >> 8);
        b[pos+5] = (unsigned char)new_pts;

        pos = payload + size;
    }

    std::string temp_path = std::string(output_path) + ".tmp";
    std::ofstream out(temp_path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write subtitle: " + std::string(output_path));
    out.write(data.data(), (std::streamsize)data.size());
    out.close();
    std::filesystem::rename(temp_path, output_path);
}

static std::string ms_to_idx_ts(int ms) {
    if (ms < 0) ms = 0;
    int h  = ms / 3600000; ms %= 3600000;
    int m  = ms / 60000;   ms %= 60000;
    int sc = ms / 1000;    ms %= 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%03d", h, m, sc, ms);
    return buf;
}

static void write_idx(const char* input_path, const char* output_path, const Shift& shift) {
    std::string text = load_file(input_path);
    bool ends_clean = !text.empty() && text.back() == '\n';
    std::istringstream ss(text);

    std::string out;

    std::string line;
    int cue = 0;
    const char* eol = "\n";
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
            eol = "\r\n";
        }

        size_t vf, vl;
        if (idx_time_line(line, vf, vl)) {
            int ms = idx_time_ms(line.substr(vf, vl));
            if (ms >= 0)
                line = line.substr(0, vf) + ms_to_idx_ts(shift.apply(ms, cue)) + line.substr(vf + vl);
            cue++;
        }

        out += line;
        if (ends_clean || !ss.eof()) out += eol;
    }

    save_file(output_path, out);
}

static Shift one_line(double slope, double intercept_s) {
    Shift shift;
    shift.slope = slope;
    shift.intercept_s = intercept_s;
    return shift;
}

static Shift per_cue(double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    Shift shift;
    shift.slope = slope;
    shift.offsets = offsets;
    shift.mapping = mapping;
    return shift;
}

void write_srt_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    write_cues(input_path, output_path, ',', one_line(slope, intercept_s));
}

void write_vtt_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    write_cues(input_path, output_path, '.', one_line(slope, intercept_s));
}

void write_ass_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    write_dialogue(input_path, output_path, one_line(slope, intercept_s));
}

void write_sub_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    write_frames(input_path, output_path, one_line(slope, intercept_s));
}

void write_sup_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    write_sup(input_path, output_path, one_line(slope, intercept_s));
}

void write_sbv_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    write_sbv(input_path, output_path, one_line(slope, intercept_s));
}

void write_idx_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    write_idx(input_path, output_path, one_line(slope, intercept_s));
}

void write_srt_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    write_cues(input_path, output_path, ',', per_cue(slope, offsets, mapping));
}

void write_vtt_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    write_cues(input_path, output_path, '.', per_cue(slope, offsets, mapping));
}

void write_ass_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    write_dialogue(input_path, output_path, per_cue(slope, offsets, mapping));
}

void write_sub_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    write_frames(input_path, output_path, per_cue(slope, offsets, mapping));
}

void write_sup_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    write_sup(input_path, output_path, per_cue(slope, offsets, mapping));
}

void write_sbv_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    write_sbv(input_path, output_path, per_cue(slope, offsets, mapping));
}

void write_idx_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    write_idx(input_path, output_path, per_cue(slope, offsets, mapping));
}
