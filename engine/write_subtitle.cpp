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

// writes the original file as .bak
void backup_file(const char* path) {
    std::filesystem::copy_file(path, std::string(path) + ".bak", std::filesystem::copy_options::overwrite_existing);
}


void write_srt_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    
    backup_file(input_path);

    std::ifstream in(input_path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open SRT: " + std::string(input_path));

    std::string raw((std::istreambuf_iterator<char>(in)), {});
    in.close();

    // strip UTF-8 BOM if present
    size_t bom_start = 0;
    if (raw.size() >= 3 &&
        (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF)
        bom_start = 3;

    std::istringstream ss(raw.substr(bom_start));
    std::ofstream out(output_path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write SRT: " + std::string(output_path));

    auto ts_to_ms = [](const std::string& s, int offset) -> int {
        int h  = std::stoi(s.substr(offset, 2));
        int m  = std::stoi(s.substr(offset + 3, 2));
        int sc = std::stoi(s.substr(offset + 6, 2));
        int ms = std::stoi(s.substr(offset + 9, 3));
        return h * 3600000 + m * 60000 + sc * 1000 + ms;
    };

    auto ms_to_ts = [](int ms) -> std::string {
        if (ms < 0) ms = 0;
        int h  = ms / 3600000; ms %= 3600000;
        int m  = ms / 60000;   ms %= 60000;
        int sc = ms / 1000;    ms %= 1000;
        char buf[13];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d,%03d", h, m, sc, ms);
        return buf;
    };

    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

            if (line.find("-->") != std::string::npos && line.size() >= 29) {
                try {
                    int start_ms  = ts_to_ms(line, 0);
                    int end_ms    = ts_to_ms(line, 17);
                    int new_start = (int)(start_ms * (1.0 + slope) + intercept_s * 1000.0);
                    int new_end   = (int)(end_ms   * (1.0 + slope) + intercept_s * 1000.0);
                    line = ms_to_ts(new_start) + " --> " + ms_to_ts(new_end);
                } catch (...) {}
            }

        out << line << "\n";
    }
}

void write_ass_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    
    backup_file(input_path);

    std::ifstream in(input_path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open ASS or SSA: " + std::string(input_path));

    std::string raw((std::istreambuf_iterator<char>(in)), {});
    in.close();

    // strip UTF-8 BOM if present
    size_t bom_start = 0;
    if (raw.size() >= 3 &&
        (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF)
        bom_start = 3;

    std::istringstream ss(raw.substr(bom_start));
    std::ofstream out(output_path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write ASS or SSA: " + std::string(output_path));

    auto ts_to_ms = [](const std::string& s) -> int {
        int h  = std::stoi(s.substr(0, 1));
        int m  = std::stoi(s.substr(2, 2));
        int sc = std::stoi(s.substr(5, 2));
        int cs = std::stoi(s.substr(8, 2));
        return h * 3600000 + m * 60000 + sc * 1000 + cs * 10;
    };

    auto ms_to_ts = [](int ms) -> std::string {
        if (ms < 0) ms = 0;
        int h  = ms / 3600000; ms %= 3600000;
        int m  = ms / 60000;   ms %= 60000;
        int sc = ms / 1000;    ms %= 1000;
        int cs = ms / 10;     ms %= 10;
        char buf[13];
        snprintf(buf, sizeof(buf), "%01d:%02d:%02d.%02d", h, m, sc, cs);
        return buf;
    };

    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

            if (line.find("Dialogue:") != std::string::npos) {
                size_t c1 = line.find(',');
                size_t c2 = line.find(',', c1 + 1);
                size_t c3 = line.find(',', c2 + 1);

                std::string start_str = line.substr(c1 + 1, c2 - c1 - 1);
                std::string end_str   = line.substr(c2 + 1, c3 - c2 - 1);
                std::string rest      = line.substr(c3);

                try {
                    int start_ms  = ts_to_ms(start_str);
                    int end_ms    = ts_to_ms(end_str);
                    int new_start = (int)(start_ms * (1.0 + slope) + intercept_s * 1000.0);
                    int new_end   = (int)(end_ms   * (1.0 + slope) + intercept_s * 1000.0);
                    line = line.substr(0, c1 + 1) + ms_to_ts(new_start) + "," + ms_to_ts(new_end) + rest;
                } catch (...) {}
            }

        out << line << "\n";
    }
}

void write_vtt_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    backup_file(input_path);

    std::ifstream in(input_path);
    std::ofstream out(output_path);

    auto ms_to_ts = [](int ms) -> std::string {
        if (ms < 0) ms = 0;
        int h  = ms / 3600000; ms %= 3600000;
        int m  = ms / 60000;   ms %= 60000;
        int s  = ms / 1000;    ms %= 1000;
        char buf[13];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", h, m, s, ms);
        return buf;
    };

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find("-->") != std::string::npos && line.size() >= 29) {
            try {
                int h1  = std::stoi(line.substr(0, 2));
                int m1  = std::stoi(line.substr(3, 2));
                int s1  = std::stoi(line.substr(6, 2));
                int ms1 = std::stoi(line.substr(9, 3));
                int start = h1*3600000 + m1*60000 + s1*1000 + ms1;

                int h2  = std::stoi(line.substr(17, 2));
                int m2  = std::stoi(line.substr(20, 2));
                int s2  = std::stoi(line.substr(23, 2));
                int ms2 = std::stoi(line.substr(26, 3));
                int end = h2*3600000 + m2*60000 + s2*1000 + ms2;

                int new_start = (int)(start * (1.0 + slope) + intercept_s * 1000.0);
                int new_end   = (int)(end   * (1.0 + slope) + intercept_s * 1000.0);
                line = ms_to_ts(new_start) + " --> " + ms_to_ts(new_end);
            } catch (...) {}
        }
        out << line << "\n";
    }
}

void write_srt_split(const char* input_path, const char* output_path, std::vector<int> offsets, std::vector<int> mapping) {
    backup_file(input_path);

    std::ifstream in(input_path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open SRT: " + std::string(input_path));

    std::string raw((std::istreambuf_iterator<char>(in)), {});
    in.close();

    size_t bom_start = 0;
    if (raw.size() >= 3 &&
        (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF)
        bom_start = 3;

    std::istringstream ss(raw.substr(bom_start));
    std::ofstream out(output_path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write SRT: " + std::string(output_path));

    auto ts_to_ms = [](const std::string& s, int offset) -> int {
        int h  = std::stoi(s.substr(offset, 2));
        int m  = std::stoi(s.substr(offset + 3, 2));
        int sc = std::stoi(s.substr(offset + 6, 2));
        int ms = std::stoi(s.substr(offset + 9, 3));
        return h * 3600000 + m * 60000 + sc * 1000 + ms;
    };

    auto ms_to_ts = [](int ms) -> std::string {
        if (ms < 0) ms = 0;
        int h  = ms / 3600000; ms %= 3600000;
        int m  = ms / 60000;   ms %= 60000;
        int sc = ms / 1000;    ms %= 1000;
        char buf[13];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d,%03d", h, m, sc, ms);
        return buf;
    };

    std::string line;
    int line_index = 0;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.find("-->") != std::string::npos && line.size() >= 29) {
            try {
                int span_index = mapping[line_index];
                int offset_ms = offsets[span_index];
                int start_ms  = ts_to_ms(line, 0) + offset_ms;
                int end_ms    = ts_to_ms(line, 17) + offset_ms;
                line = ms_to_ts(start_ms) + " --> " + ms_to_ts(end_ms);
                line_index++;
            } catch (...) {}
        }

        out << line << "\n";
    }
}
void write_ass_split(const char* input_path, const char* output_path, std::vector<int> offsets, std::vector<int> mapping) {
    backup_file(input_path);

    std::ifstream in(input_path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open ASS or SSA: " + std::string(input_path));

    std::string raw((std::istreambuf_iterator<char>(in)), {});
    in.close();

    size_t bom_start = 0;
    if (raw.size() >= 3 &&
        (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF)
        bom_start = 3;

    std::istringstream ss(raw.substr(bom_start));
    std::ofstream out(output_path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write ASS or SSA: " + std::string(output_path));

    auto ts_to_ms = [](const std::string& s) -> int {
        int h  = std::stoi(s.substr(0, 1));
        int m  = std::stoi(s.substr(2, 2));
        int sc = std::stoi(s.substr(5, 2));
        int cs = std::stoi(s.substr(8, 2));
        return h * 3600000 + m * 60000 + sc * 1000 + cs * 10;
    };

    auto ms_to_ts = [](int ms) -> std::string {
        if (ms < 0) ms = 0;
        int h  = ms / 3600000; ms %= 3600000;
        int m  = ms / 60000;   ms %= 60000;
        int sc = ms / 1000;    ms %= 1000;
        int cs = ms / 10;
        char buf[13];
        snprintf(buf, sizeof(buf), "%01d:%02d:%02d.%02d", h, m, sc, cs);
        return buf;
    };

    std::string line;
    int line_index = 0;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.find("Dialogue:") != std::string::npos) {
            try {
                size_t c1 = line.find(',');
                size_t c2 = line.find(',', c1 + 1);
                size_t c3 = line.find(',', c2 + 1);

                std::string start_str = line.substr(c1 + 1, c2 - c1 - 1);
                std::string end_str   = line.substr(c2 + 1, c3 - c2 - 1);
                std::string rest      = line.substr(c3);

                int span_index = mapping[line_index];
                int offset_ms  = offsets[span_index];
                int start_ms   = ts_to_ms(start_str) + offset_ms;
                int end_ms     = ts_to_ms(end_str) + offset_ms;
                line = line.substr(0, c1 + 1) + ms_to_ts(start_ms) + "," + ms_to_ts(end_ms) + rest;
                line_index++;
            } catch (...) {}
        }

        out << line << "\n";
    }
}

void write_vtt_split(const char* input_path, const char* output_path, std::vector<int> offsets, std::vector<int> mapping) {
    backup_file(input_path);

    std::ifstream in(input_path);
    std::ofstream out(output_path);

    auto ms_to_ts = [](int ms) -> std::string {
        if (ms < 0) ms = 0;
        int h  = ms / 3600000; ms %= 3600000;
        int m  = ms / 60000;   ms %= 60000;
        int s  = ms / 1000;    ms %= 1000;
        char buf[13];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", h, m, s, ms);
        return buf;
    };

    std::string line;
    int line_index = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.find("-->") != std::string::npos && line.size() >= 29) {
            try {
                int h1  = std::stoi(line.substr(0, 2));
                int m1  = std::stoi(line.substr(3, 2));
                int s1  = std::stoi(line.substr(6, 2));
                int ms1 = std::stoi(line.substr(9, 3));
                int start = h1*3600000 + m1*60000 + s1*1000 + ms1;

                int h2  = std::stoi(line.substr(17, 2));
                int m2  = std::stoi(line.substr(20, 2));
                int s2  = std::stoi(line.substr(23, 2));
                int ms2 = std::stoi(line.substr(26, 3));
                int end = h2*3600000 + m2*60000 + s2*1000 + ms2;

                int span_index = mapping[line_index];
                int offset_ms  = offsets[span_index];
                line = ms_to_ts(start + offset_ms) + " --> " + ms_to_ts(end + offset_ms);
                line_index++;
            } catch (...) {}
        }

        out << line << "\n";
    }
}