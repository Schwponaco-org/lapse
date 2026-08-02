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

#include "srt_parser.h"

std::vector<std::pair<int,int>> read_subtitle(const std::string& path) {
    if (path.ends_with(".srt"))
        return read_srt(path.c_str());
    if (path.ends_with(".ass") || path.ends_with(".ssa"))
        return read_ass(path.c_str());
    if (path.ends_with(".vtt"))
        return read_vtt(path.c_str());
    throw std::runtime_error("Unsupported subtitle format: " + path);
}

std::vector<std::pair<int, int>> read_srt(const char* filename) {
    std::vector<std::pair<int, int>> timestamps;
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
    return timestamps;
}

std::vector<std::pair<int, int>> read_ass(const char* filename) {
    std::vector<std::pair<int, int>> timestamps;
    std::string line {};
    std::ifstream read_file(filename);
    while (getline(read_file, line)) {
        if (line.find("Dialogue:") != 0) continue;

        // Format: Dialogue: Layer,Start,End,...
        int first_comma = line.find(',');
        int second_comma = line.find(',', first_comma + 1);
        int third_comma = line.find(',', second_comma + 1);

        std::string start_str = line.substr(first_comma + 1, second_comma - first_comma - 1);
        std::string end_str = line.substr(second_comma + 1, third_comma - second_comma - 1);

        auto parse_ass_time = [](const std::string& t) {
            int h = std::stoi(t.substr(0, 1));
            int m = std::stoi(t.substr(2, 2));
            int s = std::stoi(t.substr(5, 2));
            int cs = std::stoi(t.substr(8, 2));
            return h * 3600000 + m * 60000 + s * 1000 + cs * 10;
        };

        timestamps.push_back({parse_ass_time(start_str), parse_ass_time(end_str)});
    }
    return timestamps;
}

std::vector<std::pair<int,int>> read_vtt(const char* filename) {
    std::vector<std::pair<int,int>> timestamps;
    std::string line;
    std::ifstream f(filename);
    while (std::getline(f, line)) {
        if (line.find("-->") == std::string::npos) continue;
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

            timestamps.push_back({start, end});
        } catch (...) {}
    }
    return timestamps;
}


std::pair<std::vector<std::pair<int,int>>, std::vector<int>> process_spans(std::vector<std::pair<int, int>> timestamps) {
    std::vector<int> mapping;
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

