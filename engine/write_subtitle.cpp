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

#include "write_subtitle.h"
#include "srt_parser.h"

// Writes the original file as .bak, but never on top of one we already made -
// running lapse twice used to replace the untouched original with the shifted
// version and then there was no way back
void backup_file(const char* path) {
    std::string backup = std::string(path) + ".bak";
    if (std::filesystem::exists(backup)) return;
    std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing);
}

// How a timestamp gets moved. Either one line for the whole file, or a lookup
// per cue for nosplit and split
struct Shift {
    bool ols = false;
    double slope = 0;
    double intercept_s = 0;
    std::vector<int> offsets;
    std::vector<int> mapping;

    int apply(int ms, int cue) const {
        if (ols) return (int)(ms * (1.0 + slope) + intercept_s * 1000.0);
        if (cue < 0 || cue >= (int)mapping.size()) return ms;
        int span = mapping[cue];
        if (span < 0 || span >= (int)offsets.size()) return ms;
        return ms + offsets[span];
    }
};

// Reads the file in one go and drops a utf-8 BOM if it is there
static std::string load_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open subtitle: " + std::string(path));

    std::string raw((std::istreambuf_iterator<char>(in)), {});

    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE)
        throw std::runtime_error("File looks like utf-16, convert it to utf-8 first: " + std::string(path));
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFE && (unsigned char)raw[1] == 0xFF)
        throw std::runtime_error("File looks like utf-16, convert it to utf-8 first: " + std::string(path));

    if (raw.size() >= 3 &&
        (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF)
        return raw.substr(3);

    return raw;
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

// srt and vtt are the same file with a different character in front of the
// milliseconds, so they go through here together. We write to a temp file and
// move it into place at the end if something throws halfway the subtitle the user already had is still whole
static void write_cues(const char* input_path, const char* output_path, char ms_sep, const Shift& shift) {
    std::istringstream ss(load_file(input_path));

    std::string temp_path = std::string(output_path) + ".tmp";
    std::ofstream out(temp_path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write subtitle: " + std::string(output_path));

    std::string line;
    int cue = 0;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

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
                line = ms_to_ts(shift.apply(start_ms, cue), ms_sep) + " --> " +
                       ms_to_ts(shift.apply(end_ms, cue), ms_sep) + tail;
            }
            cue++;       // counted even when it did not parse, the reader did the same
        }

        out << line << "\n";
    }

    out.close();
    std::filesystem::rename(temp_path, output_path);
}

static void write_dialogue(const char* input_path, const char* output_path, const Shift& shift) {
    std::istringstream ss(load_file(input_path));

    std::string temp_path = std::string(output_path) + ".tmp";
    std::ofstream out(temp_path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write subtitle: " + std::string(output_path));

    std::string line;
    int cue = 0;
    int start_col = 1;
    int end_col = 2;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

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

        out << line << "\n";
    }

    out.close();
    std::filesystem::rename(temp_path, output_path);
}

void write_srt_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    Shift shift;
    shift.ols = true;
    shift.slope = slope;
    shift.intercept_s = intercept_s;
    write_cues(input_path, output_path, ',', shift);
}

void write_vtt_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    Shift shift;
    shift.ols = true;
    shift.slope = slope;
    shift.intercept_s = intercept_s;
    write_cues(input_path, output_path, '.', shift);
}

void write_ass_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    Shift shift;
    shift.ols = true;
    shift.slope = slope;
    shift.intercept_s = intercept_s;
    write_dialogue(input_path, output_path, shift);
}

void write_srt_split(const char* input_path, const char* output_path, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    Shift shift;
    shift.offsets = offsets;
    shift.mapping = mapping;
    write_cues(input_path, output_path, ',', shift);
}

void write_vtt_split(const char* input_path, const char* output_path, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    Shift shift;
    shift.offsets = offsets;
    shift.mapping = mapping;
    write_cues(input_path, output_path, '.', shift);
}

void write_ass_split(const char* input_path, const char* output_path, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    Shift shift;
    shift.offsets = offsets;
    shift.mapping = mapping;
    write_dialogue(input_path, output_path, shift);
}
