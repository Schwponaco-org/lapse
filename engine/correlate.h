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
#include <vector>
#include <iostream>
#include <limits>
#include <tuple>
#include <utility>
#include <fftw3.h>
#include <algorithm>

// Nothing is ever off by more than this - past it we are looking at the wrong
// film rather than a sync problem, so there is no reason to search that far
const int MAX_OFFSET_MS = 15 * 60 * 1000;

// Split mode searches around the offset nosplit already found. 10ms steps over
// two minutes each way is enough for ad breaks and extended cuts and keeps the
// backtracking table down to something a container can hold
const int SPLIT_WINDOW_MS = 120000;
const int SPLIT_STEP_MS = 10;

// The ratios you actually run into - a subtitle timed for one framerate played
// back at another. Anything outside this list is a transcode gone wrong, and
// that is what the regression is still there for
const double FRAMERATE_RATIOS[] = {
    1.0,
    24.0/23.976, 23.976/24.0,
    25.0/24.0,   24.0/25.0,
    25.0/23.976, 23.976/25.0,
    30.0/29.97,  29.97/30.0,
    30.0/25.0,   25.0/30.0
};

std::pair<double, double> linear_regression(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& w);
std::pair<double, double> fft_crosscorrelate(const std::vector<int>& activity_profile, const std::vector<int>& srt_profile);
double score_calculator(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans, int x);
std::pair<int, double> best_offset(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans);
std::tuple<double, int, double> best_framerate(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans);
std::vector<double> score_curve(const std::pair<int,int>& span, const std::vector<std::pair<int,int>>& reference_spans, int lo, int hi, int step);
std::vector<int> split_alignment(const std::vector<std::pair<int,int>>& read_srt, const std::vector<std::pair<int,int>>& reference_spans, float p, int base_offset);
