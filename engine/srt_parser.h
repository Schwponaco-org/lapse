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

#pragma once
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

int parse_timestamp(const std::string& line, size_t from);
std::string trim(const std::string& s);

std::vector<size_t> ass_commas(const std::string& line);
bool ass_field(const std::string& line, const std::vector<size_t>& commas, int index, size_t& from, size_t& len);
std::pair<int,int> ass_time_columns(const std::string& format_line);

std::vector<std::pair<int,int>> read_subtitle(const std::string& path);
std::vector<std::pair<int, int>> read_srt(const char* filename);
std::vector<std::pair<int, int>> read_ass(const char* filename);
std::vector<std::pair<int,int>> read_vtt(const char* filename);
std::pair<std::vector<std::pair<int,int>>, std::vector<int>> process_spans(const std::vector<std::pair<int, int>>& timestamps, bool merge = true);
std::vector<int> activity(const std::vector<std::pair<int, int>>& spans);
std::vector<std::pair<int, int>> reference_spans(const std::vector<int>& activity_profile);
