#include "srt_parser.h"

std::pair<std::vector<std::pair<int,int>>, std::vector<int>> read_srt(const char* filename) {
    std::vector<std::pair<int, int>> timestamps;
    std::vector<int> mapping;
    std::string line {};
    std::ifstream read_file(filename);
    while (getline (read_file, line)) {
        if (line.find("-->") != std::string::npos) {
            int start_ms {};
            int end_ms {};

            int hours =std::stoi(line.substr(0, 2));
            start_ms = hours * 3600000;
            int min =std::stoi(line.substr(3, 2));
            start_ms += min * 60000;
            int sec =std::stoi(line.substr(6, 2));
            start_ms += sec * 1000;
            int mil_sec =std::stoi(line.substr(9, 3));
            start_ms += mil_sec;

            int e_hours =std::stoi(line.substr(17, 2));
            end_ms = e_hours * 3600000;
            int e_min =std::stoi(line.substr(20, 2));
            end_ms += e_min * 60000;
            int e_sec =std::stoi(line.substr(23, 2));
            end_ms += e_sec * 1000;
            int e_mil_sec =std::stoi(line.substr(26, 3));
            end_ms += e_mil_sec;

            timestamps.push_back(std::make_pair(start_ms, end_ms));
        }
    }

    std::vector<std::pair<std::pair<int,int>, int>> ys;
    for (int i = 0; i < timestamps.size(); i++) {
        ys.push_back({timestamps[i], i});
    }

    std::sort(ys.begin(), ys.end());


    for (int i = 0; i < ys.size(); i++) {
        int place = 0;
        if (ys[i].first.first - ys[i].first.second == 0) {
            ys.erase(ys.begin() + i--);
            //ys[i].second = -1;
        }

        if (ys[i].first.second < ys[i].first.first) {
            place = ys[i].first.first;
            ys[i].first.first = ys[i].first.second;
            ys[i].first.second = place;
        }
    }
    std::vector<std::pair<int,int>> spans;
    std::vector<int> original_placement;
    int new_end;
    int n = 0;

    for (int i = 0; i < ys.size(); i++) {
        original_placement.push_back(n);


        if (spans.empty() || ys[i].first.first >= spans.back().second) {
            spans.push_back({ys[i].first.first, ys[i].first.second});
            n += 1;
        } else {
            new_end = std::max(ys[i].first.second, spans.back().second);
            spans.back().second = new_end;
        }
    }

    for (int i = 0; i < ys.size(); i++) {
        mapping.push_back(original_placement[ys[i].second]);
    }

    return {spans, mapping};
}

// Build some sort of activity profile that checks every 10ms for dialogue 
std::vector<int> activity(std::vector<std::pair<int, int>> spans) {
    if (spans.empty()) return {};
    std::vector<int> activity_profile = {};
    int j = 0;
    for (int i = 0; i <= spans.back().second; i += 10) {
        while (j < spans.size() && i > spans[j].second) {
            j++;
        }
        if (i >= spans[j].first && i <= spans[j].second) {
            activity_profile.push_back(1);
        } else {
            activity_profile.push_back(0);
        }
    }
    return activity_profile;
}

void write_srt_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
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