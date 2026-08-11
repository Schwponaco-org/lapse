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
#include <iostream>
#include <limits>
#include <tuple>
#include <utility>
#include <fftw3.h>
#include <algorithm>

// Nothing is ever off by more than this - past it we are looking at the wrong
// film rather than a sync problem, so there is no reason to search that far
const int MAX_OFFSET_MS = 15 * 60 * 1000;

// Split mode searches around the offset nosplit already found. The grid is
// coarse on purpose, it only has to work out where the offset changes and each
// piece gets measured properly afterwards, so ten minutes each way still fits
const int SPLIT_WINDOW_MS = 600000;
const int SPLIT_COARSE_MS = 100;

// refining an offset we already trust only has to cover ad breaks and missing
// scenes, so the window shrinks and the grid gets four times finer for free
const int REFINE_WINDOW_MS = 45000;
const int REFINE_STEP_MS = 25;

const int CONCAT_SEARCH_MS = 90 * 60 * 1000;
const int CONCAT_JUMP_MS = 300000;
const int CONCAT_BACK_MS = 60000;

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

// What one pass of the search came back with. sigma is the one to trust: it is
// how far the winning offset sticks out of the noise, and unlike confidence it
// reads the same on every film
struct Lock {
    int offset = 0;
    double confidence = 0;
    double margin = 0;
    double sigma = 0;
};

// One slice of the subtitle measured on its own, so we can see whether the
// whole file agrees on one offset or whether it wanders
struct Chunk {
    double time;
    double offset;
    double confidence;
    double sigma;
};

std::pair<double, double> linear_regression(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& w);
std::pair<double, double> fft_crosscorrelate(const std::vector<int>& activity_profile, const std::vector<int>& srt_profile);
double score_calculator(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans, int x);
Lock best_offset(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans, const std::vector<float>& reference_weights = {}, double coverage = 1.0, int max_offset = MAX_OFFSET_MS);
std::tuple<double, int, double, double> best_framerate(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans, const std::vector<float>& reference_weights = {}, double coverage = 1.0);
std::vector<Chunk> chunk_offsets(const std::vector<std::pair<int, int>>& read_srt, const std::vector<std::pair<int, int>>& reference_spans, const std::vector<float>& reference_weights, int count, double coverage = 1.0, int max_offset = MAX_OFFSET_MS);
std::vector<int> backward_jumps(const std::vector<std::pair<int,int>>& read_srt);
std::vector<int> offsets_for_cuts(const std::vector<std::pair<int,int>>& read_srt, const std::vector<std::pair<int,int>>& reference_spans, const std::vector<float>& reference_weights, const std::vector<int>& cuts, double coverage, double* worst_sigma);
std::vector<int> concat_offsets(const std::vector<std::pair<int,int>>& read_srt, const std::vector<std::pair<int,int>>& reference_spans, const std::vector<float>& reference_weights, double coverage = 1.0, double* worst_sigma = nullptr);
std::pair<double, double> robust_line(const std::vector<Chunk>& chunks);
double snap_ratio(double ratio);
double peak_sigma(const std::vector<double>& peak, int best_bucket, double fmax);
std::vector<double> score_curve(const std::pair<int,int>& span, const std::vector<std::pair<int,int>>& reference_spans, const std::vector<float>& reference_weights, int lo, int hi, int step);
std::vector<int> split_alignment(const std::vector<std::pair<int,int>>& read_srt, const std::vector<std::pair<int,int>>& reference_spans, const std::vector<float>& reference_weights, float p, int base_offset, int window_ms = SPLIT_WINDOW_MS, int step_ms = SPLIT_COARSE_MS);
