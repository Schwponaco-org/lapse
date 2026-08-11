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

#pragma once
#include <vector>
#include <utility>

struct Hit {
    int offset = 0;
    double z = 0; // how far the peak sticks out of the noise floor
    double runner = 0; // best rival, 0..1 of the winner
    double score = 0; // plain overlap score, same units as before
};

void align_setup(const std::vector<std::pair<int,int>>& spans, const std::vector<float>& weights);
void align_drop();
bool align_ready();
int align_reach();

std::vector<Hit> align_peaks(const std::vector<std::pair<int,int>>& cues, int want);
double align_score(const std::vector<std::pair<int,int>>& cues, int off);
int align_refine(const std::vector<std::pair<int,int>>& cues, int off, int reach = 400);


double onset_z(const std::vector<std::pair<int,int>>& cues, int off, double* rate = nullptr);
